"""Encode ordered manifest assets into TinyRT's resource section."""
from array import array
import hashlib
import re
import struct
import wave

from package import MAX_PACKAGE_SIZE


def entries(root, spec, local_file):
    if not isinstance(spec, list) or not spec:
        raise ValueError('asset list must contain PNG or WAV files')
    names = set()
    for item in spec:
        item = {'path': item} if isinstance(item, str) else item
        if not isinstance(item, dict) or set(item) - {'path', 'name', 'format', 'clip'}:
            raise ValueError('invalid asset entry')
        path = local_file(root, item.get('path'))
        name = item.get('name', re.sub('[^A-Za-z0-9_]', '_', path.stem))
        if not isinstance(name, str) or not re.fullmatch('[A-Za-z0-9_]+', name) or name.upper() in names:
            raise ValueError('asset names must be unique ASCII identifiers')
        names.add(name.upper())
        formats = {'.png': ('rgb565', 'index8'), '.wav': ('pcm16',)}.get(path.suffix.lower(), ())
        if not formats or item.get('format', formats[0]) not in formats:
            raise ValueError(f'unsupported asset format for {path.name}')
        if 'clip' in item and (formats[0] != 'pcm16' or item['clip'] != 'effect'):
            raise ValueError('clip must be effect and requires WAV')
        yield path, 'ASSET_' + name.upper(), item.get('format', formats[0]), item.get('clip')


def color565(rgb):
    r, g, b = rgb[:3]
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


def png(path, fmt):
    from PIL import Image
    with Image.open(path, formats=['PNG']) as image:
        if image.width * image.height * (2 if fmt == 'rgb565' else 1) > MAX_PACKAGE_SIZE:
            raise ValueError('image exceeds resource limit')
        dimensions = {'WIDTH': image.width, 'HEIGHT': image.height}
        rgba = image.convert('RGBA')
        if fmt == 'rgb565':
            encoded = bytearray(image.width * image.height * 2)
            for i, pixel in enumerate(rgba.getdata()):
                struct.pack_into('<H', encoded, i * 2, color565(pixel))
            return encoded, b'', dimensions
        # Indexed PNGs with transparent index zero preserve the artist's palette order.
        if image.mode == 'P' and image.info.get('transparency') == 0:
            palette = image.getpalette()
            colors = [0] + [color565(palette[i:i+3]) for i in range(3, len(palette), 3)]
            indices = image.tobytes()
            if max(indices) >= len(colors):
                raise ValueError('indexed PNG refers to missing palette entry')
        else:
            lookup = {}
            indices = bytearray()
            for pixel in rgba.getdata():
                index = 0
                if pixel[3] >= 128:
                    value = color565(pixel)
                    index = lookup.setdefault(value, len(lookup) + 1)
                    if index > 255:
                        raise ValueError('INDEX8 supports at most 255 opaque RGB565 colors')
                indices.append(index)
            colors = [0, *lookup]
        return indices, struct.pack(f'<{len(colors)}H', *colors), dimensions


def wav(path, clip):
    try:
        with wave.open(str(path), 'rb') as source:
            channels, width, rate, frames, _, _ = source.getparams()
            size = frames * channels * width
            if width not in (1, 2, 3, 4) or size > MAX_PACKAGE_SIZE:
                raise ValueError('unsupported or oversized PCM WAV')
            raw = source.readframes(frames)
    except (wave.Error, EOFError) as error:
        raise ValueError(f'invalid PCM WAV: {path.name}') from error
    if len(raw) != size:
        raise ValueError('truncated PCM WAV')
    mono = array('h')
    for start in range(0, size, channels * width):
        total = 0
        for pos in range(start, start + channels * width, width):
            value = int.from_bytes(raw[pos:pos+width], 'little', signed=width != 1)
            total += (value - 128) << 8 if width == 1 else value >> (8 * width - 16)
        mono.append(total // channels)
    if clip:
        active = (i for i, value in enumerate(mono) if abs(value) > 64)
        first = next(active, None)
        if first is not None:
            last = max(active, default=first)
            mono = mono[max(0, first-rate//100):last+rate//50]
    count = len(mono) * 16000 // rate
    if clip:
        count = min(16000, count)
    if count * 2 > MAX_PACKAGE_SIZE:
        raise ValueError('resampled WAV exceeds resource limit')
    result = bytearray(count * 2)
    for i in range(count):
        pos = i * rate / 16000
        j = int(pos)
        fraction = pos - j
        value = round(mono[j]*(1-fraction) + mono[min(j+1, len(mono)-1)]*fraction)
        if clip and i >= count-160:
            value = value * (count-i) // 160
        struct.pack_into('<h', result, 2*i, value)
    return result


def generate(root, spec, local_file, write_atomic):
    data = bytearray()
    header = ['#pragma once']
    for path, name, fmt, clip in entries(root, spec, local_file):
        if fmt == 'pcm16':
            pixels, palette, fields = wav(path, clip), b'', {}
        else:
            pixels, palette, fields = png(path, fmt)
        fields.update(OFFSET=len(data), LENGTH=len(pixels))
        data.extend(pixels)
        if palette:
            fields.update(PALETTE_OFFSET=len(data), PALETTE_COUNT=len(palette)//2)
            data.extend(palette)
        if len(data) > MAX_PACKAGE_SIZE:
            raise ValueError('assets exceed resource limit')
        header.extend(f'#define {name}_{key} {value}u' for key, value in fields.items())
    definitions = ('\n'.join(header) + '\n').encode()
    digest = hashlib.sha256(definitions + data).hexdigest()
    directory = root / 'build' / 'assets' / digest
    if directory.resolve() != directory:
        raise ValueError('generated asset directory must not follow links')
    for filename, content in [('resources.bin', data), ('assets_generated.h', definitions)]:
        output = directory / filename
        if output.is_symlink():
            raise ValueError('generated assets must not follow links')
        if not output.exists() or output.read_bytes() != content:
            write_atomic(output, content)
    return directory / 'resources.bin'
