"""Recreate the two shipped resource layouts from their source art and sounds."""
import runpy
import shutil
import struct
import sys
import tempfile
import unittest
import wave
from pathlib import Path

from PIL import Image

SDK = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SDK / 'tools'))
import tinyrt


class AssetPipelineTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name).resolve()

    def check(self, name, assets):
        path = tinyrt.resource_file(self.root, assets)
        expected = (SDK / 'examples' / name / 'resources.bin').read_bytes()
        self.assertEqual(path.read_bytes(), expected)

    def test_pixel_quest(self):
        art = runpy.run_path(str(SDK / 'examples/pixel_quest/prepare_assets.py'))
        atlas = art['build_atlas']()
        themes = art['THEMES']
        assets = []
        for i, theme in enumerate(themes):
            colors = dict(art['COLORS'], **theme)
            palette = [(0, 0, 0), *(colors[key] for key in art['KEYS'])]
            if i == 0:
                atlas.putpalette(bytes(v for color in palette for v in color))
                atlas.save(self.root / 'atlas.png', transparency=0)
                assets.append({'path': 'atlas.png', 'format': 'index8'})
            else:
                image = Image.new('RGB', (len(palette), 1))
                image.putdata(palette)
                image.save(self.root / f'theme{i}.png')
                assets.append(f'theme{i}.png')
        for name, samples in art['SOUNDS'].items():
            with wave.open(str(self.root / (name+'.wav')), 'wb') as out:
                out.setparams((1, 2, 16000, 0, 'NONE', ''))
                out.writeframes(struct.pack(f'<{len(samples)}h', *samples))
            assets.append(name+'.wav')
        self.check('pixel_quest', assets)

    def test_flappy(self):
        # The original layout includes packed opaque spans followed by audio
        # and a second, indexed representation of the same transformed sprites.
        source = SDK / 'examples/flappy'
        shutil.copytree(source / 'assets', self.root / 'assets')
        script = self.root / 'prepare_assets.py'
        shutil.copyfile(source / script.name, script)
        art = runpy.run_path(str(script))
        assets = []
        for name, image in art['resident']:
            if name != 'background':
                opaque = [p for p in image.getdata() if p[3] >= 128]
                image = Image.new('RGBA', (len(opaque), 1))
                image.putdata(opaque)
            image.save(self.root / (name+'.png'))
            assets.append(name+'.png')
        for name in ('wing', 'point', 'hit', 'die', 'swoosh'):
            assets.append({'path': f'assets/audio/{name}.wav', 'clip': 'effect'})
        # Preserve the original artist's palette order across sprite placements.
        indexed = Image.new('P', (art['atlas_width'], art['atlas_height']))
        colors = {(0,): 0}
        palette = [0, 0, 0]
        for _, image, x, y in art['placed']:
            for j, (r, g, b, a) in enumerate(image.getdata()):
                if a < 128:
                    continue
                key = (r >> 3, g >> 2, b >> 3)
                if key not in colors:
                    colors[key] = len(colors)
                    palette.extend((r, g, b))
                indexed.putpixel((x + j % image.width, y + j // image.width), colors[key])
        indexed.putpalette(bytes(palette))
        indexed.save(self.root / 'atlas.png', transparency=0)
        assets.append({'path': 'atlas.png', 'format': 'index8'})
        self.check('flappy', assets)

    def test_offsets_and_transparency(self):
        image = Image.new('RGBA', (3, 1))
        image.putdata([(255, 0, 0, 0), (0, 0, 0, 255), (255, 0, 0, 255)])
        image.save(self.root / 'sprite.png')
        path = tinyrt.resource_file(self.root, [{'path': 'sprite.png', 'format': 'index8'}])
        self.assertEqual(path.read_bytes(), bytes.fromhex('00 01 02 0000 0000 00f8'))
        header = path.with_name('assets_generated.h').read_text()
        self.assertIn('#define ASSET_SPRITE_PALETTE_OFFSET 3u', header)


if __name__ == '__main__':
    unittest.main()
