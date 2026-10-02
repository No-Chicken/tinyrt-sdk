"""TinyRT v1 transport. Success requires an exact committed identity query."""
import asyncio
import hashlib
import re
import struct
import zlib
HELLO, LIST, QUERY, PREPARE, UNINSTALL, LAUNCH, STOP = range(0x10, 0x17)
LIST_QUARANTINED = 0x17
LIST_PAGE = 0x18
STORAGE, APP_INFO, RUNTIME_INFO = 0x19, 0x1a, 0x1b
CAP_QUARANTINE = 8
CAP_PAGED_LIST = 16
CAP_STORAGE = 32
CAP_PACKAGE_V2 = 64
MAX_APPS = 16
MAX_PACKAGE_SIZE = 0x200000
SVC = '5377e411-0c9d-42a7-194b-5e8c71d23a6f'
CTRL = SVC.replace('e411-', 'e412-')
DATA = SVC.replace('e411-', 'e413-')
MGMT = SVC.replace('e411-', 'e414-')

class RemoteError(RuntimeError):
    def __init__(self, status):
        self.status = status
        super().__init__(f'device rejected operation: status={status}')

def frames(op, request_id, payload, response=False):
    if not 1 <= request_id <= 65535 or len(payload) > 256:
        raise ValueError('invalid request id or message length')
    return [struct.pack('<BBBBHHH', 0xa7, 1, op, int(response), request_id,
                        len(payload), off) + payload[off:off+10]
            for off in range(0, max(1, len(payload)), 10)]

class Decoder:
    def __init__(self): self.reset()
    def reset(self): self.state = None; self.data = bytearray()
    def feed(self, packet):
        try:
            if not 10 <= len(packet) <= 20: raise ValueError('bad frame length')
            magic, version, op, flags, rid, total, offset = struct.unpack('<BBBBHHH', packet[:10])
            size = len(packet)-10
            if magic != 0xa7 or version != 1 or flags > 1 or not rid or total > 256:
                raise ValueError('bad frame header')
            if offset > total or size > total-offset or (total and not size):
                raise ValueError('bad payload length')
            state = (op, rid, bool(flags), total)
            if self.state is None:
                if offset: raise ValueError('missing initial fragment')
                self.state = state
            if self.state != state or offset != len(self.data): raise ValueError('out of order fragment')
            self.data.extend(packet[10:])
            if len(self.data) == total:
                result = (op, rid, bool(flags), bytes(self.data)); self.reset(); return result
            return None
        except (ValueError, struct.error):
            self.reset(); raise ValueError('invalid management frame') from None

def package_info(data):
    if len(data) < 256 or len(data) > MAX_PACKAGE_SIZE or data[:8] not in (b'TRPKG001',b'TRPKG002'):
        raise ValueError('invalid TinyRT package header or size')
    if data[:8]==b'TRPKG002' and struct.unpack_from('<HH',data,8)!=(2,256):
        raise ValueError('invalid v2 package header')
    total, = struct.unpack_from('<I', data, 12)
    version, = struct.unpack_from('<I', data, 32)
    appid = data[56:88]; name = appid.split(b'\0', 1)[0]
    if total != len(data) or not version or not re.fullmatch(rb'[a-z0-9._-]{1,31}', name):
        raise ValueError('invalid package identity')
    if appid != name + bytes(32-len(name)): raise ValueError('noncanonical app id')
    return appid + struct.pack('<I', version) + hashlib.sha256(data).digest() + struct.pack('<I', total)

def validate_info(raw, maximum=MAX_PACKAGE_SIZE):
    """Validate the canonical identity and size of a standard info72 record."""
    if len(raw) != 72:
        raise ValueError('invalid directory record length')
    name = raw[:32].split(b'\0', 1)[0]
    version, = struct.unpack_from('<I', raw, 32)
    size, = struct.unpack_from('<I', raw, 68)
    if (not re.fullmatch(rb'[a-z0-9._-]{1,31}', name)
            or raw[:32] != name.ljust(32, b'\0') or not version
            or not 256 <= size <= min(maximum, MAX_PACKAGE_SIZE)):
        raise ValueError('invalid directory record identity or size')
    return name

async def directory_records(link, opcode=LIST, *, hello=None):
    """Read one sorted inventory; restart at most twice on a catalog conflict.

    Generation is an opaque u64, including zero for an empty store. Never mix
    pages from different generations or retry malformed data as a race.
    """
    if opcode not in (LIST, LIST_QUARANTINED):
        raise ValueError('invalid directory kind')
    if hello is None:
        hello = await link.management(HELLO)
    if len(hello) != 8:
        raise ValueError('unsupported HELLO')
    abi, maximum, max_apps, flags = struct.unpack('<HIBB', hello)
    if abi != 1 or maximum < 256 or not 1 <= max_apps <= MAX_APPS:
        raise ValueError('unsupported HELLO limits')
    if opcode == LIST_QUARANTINED and not flags & CAP_QUARANTINE:
        raise ValueError('host does not support quarantine diagnostics')

    def append_records(records, payload):
        previous = records[-1][:32].split(b'\0', 1)[0] if records else b''
        for off in range(0, len(payload), 72):
            raw = payload[off:off + 72]
            name = validate_info(raw, maximum)
            if name <= previous:
                raise ValueError('directory records are duplicated or unsorted')
            records.append(raw)
            previous = name

    if not flags & CAP_PAGED_LIST:
        raw = await link.management(opcode)
        if not raw or raw[0] > min(2, max_apps) or len(raw) != 1 + raw[0] * 72:
            raise ValueError('invalid directory reply')
        records = []
        append_records(records, raw[1:])
        return records

    for attempt in range(3):
        records = []
        generation = 0
        total = None
        try:
            # Even a peer sending only one record per page cannot loop forever.
            for _ in range(MAX_APPS):
                offset = len(records)
                request = struct.pack('<BBQ', int(opcode == LIST_QUARANTINED), offset, generation)
                raw = await link.management(LIST_PAGE, request)
                if len(raw) < 11:
                    raise ValueError('short directory page')
                current, advertised, cursor, count = struct.unpack_from('<QBBB', raw)
                if (advertised > max_apps or cursor != offset or count > 3
                        or offset + count > advertised or len(raw) != 11 + count * 72
                        or (count == 0 and offset != advertised)):
                    raise ValueError('invalid directory page bounds')
                if total is not None and (current != generation or advertised != total):
                    raise ValueError('directory generation or total changed without conflict')
                generation, total = current, advertised
                append_records(records, raw[11:])
                if len(records) == total:
                    return records
        except RemoteError as exc:
            if exc.status != 7 or attempt == 2:
                raise
            continue
        raise ValueError('directory pagination did not terminate')

async def _query_hello(link, required=CAP_STORAGE):
    raw = await link.management(HELLO)
    if len(raw) != 8:
        raise ValueError('unsupported HELLO')
    abi, maximum, count, flags = struct.unpack('<HIBB', raw)
    if abi != 1 or not 256 <= maximum <= MAX_PACKAGE_SIZE or not 1 <= count <= MAX_APPS or flags & required != required:
        raise ValueError('host does not support the requested query or limits')
    return maximum, count, flags


async def storage_info(link):
    """Validated schema-1 counters. Opaque generation is 16 hex digits.

    Unavailable RAM counters are None, never misleading zero measurements.
    """
    maximum, max_apps, _ = await _query_hello(link)
    raw = await link.management(STORAGE)
    if len(raw) != 92:
        raise ValueError('invalid storage reply length')
    schema, flags, generation = struct.unpack_from('<HHQ', raw)
    if schema != 1 or flags & ~1 or struct.unpack_from('<H', raw, 46)[0]:
        raise ValueError('unsupported storage schema, flags or reserved bytes')
    result = dict(schema=schema, flags=flags, generation=f'{generation:016x}', ram_valid=bool(flags & 1))
    names = ('package_total_bytes','data_bytes','package_bytes','allocated_bytes','free_bytes','largest_free_bytes','max_package_size')
    result.update(zip(names, struct.unpack_from('<7I', raw, 12)))
    result.update(zip(('max_apps','installed_count','quarantined_count'), struct.unpack_from('<3H',raw,40)))
    tail = ('app_data_total_bytes','internal_ram_total_bytes','internal_ram_free_bytes','internal_ram_largest_free_bytes',
            'external_ram_total_bytes','external_ram_free_bytes','external_ram_largest_free_bytes',
            'runtime_heap_limit','runtime_heap_used','runtime_heap_peak','shared_assets_total_bytes')
    result.update(zip(tail, struct.unpack_from('<11I',raw,48)))
    total,data,used,allocated,free,largest,limit = (result[n] for n in names)
    count = result['installed_count']
    if (total < 8192 or total % 4096 or data != total-8192 or allocated > data or allocated % 4096
            or free != data-allocated or largest > free or largest % 4096
            or (free > 0 and largest == 0) or largest*(count+1) < free
            or limit != maximum or result['max_apps'] != max_apps or count > max_apps
            or result['quarantined_count'] > count or not count*256 <= used <= count*limit
            or used > allocated or allocated-used > count*4095 or allocated < count*4096
            or result['app_data_total_bytes'] % 4096 or result['shared_assets_total_bytes'] % 4096):
        raise ValueError('inconsistent storage capacity or counts')
    if flags & 1:
        for prefix in ('internal_ram','external_ram'):
            if not result[prefix+'_largest_free_bytes'] <= result[prefix+'_free_bytes'] <= result[prefix+'_total_bytes']:
                raise ValueError('inconsistent RAM counters')
    else:
        for name in tail[1:7]: result[name] = None
    if not result['runtime_heap_used'] <= result['runtime_heap_peak'] <= result['runtime_heap_limit']:
        raise ValueError('inconsistent runtime heap counters')
    return result


async def app_info(link, identity):
    """Query healthy verified metadata for identity68 or an expected info72."""
    identity = bytes(identity)
    if len(identity) not in (68,72):
        raise ValueError('invalid app details identity length')
    validate_info(identity if len(identity)==72 else identity+struct.pack('<I',256))
    maximum, _, flags = await _query_hello(link)
    if flags & CAP_PACKAGE_V2:
        raw = await link.management(APP_INFO, b'\x02\0'+identity[:68])
        return _app_info_v2(raw, identity, maximum)
    raw = await link.management(APP_INFO, identity[:68])
    if len(raw) != 164:
        raise ValueError('invalid app details reply length')
    name = validate_info(raw[:72], maximum)
    if raw[:len(identity)] != identity:
        raise ValueError('app details identity or size mismatch')
    title = raw[72:136]
    end = title.find(b'\0')
    if end < 1 or any(title[end:]):
        raise ValueError('noncanonical app title')
    title = title[:end].decode('utf-8', errors='strict')
    key_id,abi,permissions,pages,budget,wasm,assets = struct.unpack_from('<7I',raw,136)
    size, = struct.unpack_from('<I', raw, 68)
    if abi != 1 or permissions & ~15 or not 1 <= pages <= 16 or not 1 <= budget <= 100000 or wasm <= 8 or wasm+assets+256 != size:
        raise ValueError('invalid app details resource policy')
    return dict(app_id=name.decode('ascii'),version=struct.unpack_from('<I',raw,32)[0],sha256=raw[36:68].hex(),
                size=size,title=title,key_id=key_id,abi_version=abi,permissions=permissions,memory_pages=pages,
                instruction_budget=budget,wasm_size=wasm,assets_size=assets)


def _target_text(raw):
    name=raw.split(b'\0',1)[0]
    if not re.fullmatch(rb'[a-z0-9_-]{1,15}',name) or raw!=name.ljust(16,b'\0'):
        raise ValueError('invalid target identifier')
    return name.decode('ascii')


def _app_info_v2(raw, identity, maximum):
    if len(raw)!=248:raise ValueError('invalid v2 app details length')
    schema,fmt,backend,fallback,sections=struct.unpack_from('<HHBBH',raw)
    if schema!=2 or fmt not in (1,2) or backend not in (1,2) or fallback>3 or sections & ~7:
        raise ValueError('unknown v2 app details schema or flags')
    name=validate_info(raw[8:80],maximum)
    if raw[8:8+len(identity)]!=identity:raise ValueError('app details identity or size mismatch')
    title=raw[80:144];end=title.find(b'\0')
    if end<1 or any(title[end:]):raise ValueError('noncanonical app title')
    title=title[:end].decode('utf-8',errors='strict')
    key_id,abi,permissions,pages,budget,wasm,assets,aot,aot_format,safety=struct.unpack_from('<10I',raw,144)
    size=struct.unpack_from('<I',raw,76)[0]
    expected_sections=int(bool(wasm)) | (2 if aot else 0) | (4 if assets else 0)
    if (abi!=1 or permissions & ~15 or not 1<=pages<=16 or not 1<=budget<=100000
            or (wasm and wasm<=8) or not (wasm or aot) or sections!=expected_sections):
        raise ValueError('invalid v2 app details policy or sections')
    if aot:
        arch=_target_text(raw[184:200]);cpu=_target_text(raw[200:216])
        if not aot_format or safety!=7 or not any(raw[216:248]):raise ValueError('invalid AOT metadata')
    else:
        arch=cpu=''
        if any(raw[176:248]):raise ValueError('unexpected AOT metadata')
    if (backend==2 and (not aot or fallback) or backend==1 and (not wasm or bool(aot)!=bool(fallback))):
        raise ValueError('inconsistent selected backend or fallback')
    if fmt==1:
        if aot or backend!=1 or fallback:raise ValueError('invalid v1 execution metadata')
        calculated=256+wasm+assets
    else:
        calculated=256+16*sections.bit_count()
        for length in (wasm,256+aot if aot else 0,assets):
            if length:calculated=(calculated+3)//4*4+length
    if calculated!=size:raise ValueError('inconsistent package section lengths')
    return dict(schema=2,package_format=fmt,selected_backend=backend,fallback_reason=fallback,section_bits=sections,
        app_id=name.decode('ascii'),version=struct.unpack_from('<I',raw,40)[0],sha256=raw[44:76].hex(),
        size=size,title=title,key_id=key_id,abi_version=abi,permissions=permissions,memory_pages=pages,
        instruction_budget=budget,wasm_size=wasm,assets_size=assets,aot_size=aot,aot_format=aot_format,
        safety_flags=safety,target_arch=arch,target_cpu=cpu,compat_id=raw[216:248].hex())


async def runtime_info(link):
    """Read the enabled firmware profile; never infer AOT support from a CPU name."""
    maximum,max_apps,capabilities=await _query_hello(link,CAP_PACKAGE_V2)
    raw=await link.management(RUNTIME_INFO)
    if len(raw)!=136:raise ValueError('invalid runtime info length')
    schema,formats,backends,aot_format,revision=struct.unpack_from('<HHIII',raw)
    if schema!=1 or formats!=3 or backends & ~7 or not backends & 1 or backends & 4 and not backends & 2:
        raise ValueError('unsupported runtime schema, formats or backends')
    safety=struct.unpack_from('<I',raw,132)[0]
    if backends & 2:
        arch=_target_text(raw[16:32]);cpu=_target_text(raw[32:48])
        if not aot_format or not revision or safety!=7 or any(not any(raw[start:end]) for start,end in ((48,80),(80,112),(112,132))):
            raise ValueError('incomplete enabled AOT profile')
    else:
        arch=cpu=''
        if any(raw[8:]):raise ValueError('disabled AOT profile must be zero')
    return dict(schema=schema,package_formats=formats,backends=backends,capabilities=capabilities,package_v2=True,
        max_package_size=maximum,max_apps=max_apps,aot_enabled=bool(backends & 2),development_aot=bool(backends & 4),
        aot_format=aot_format,runtime_abi_revision=revision,target_arch=arch,target_cpu=cpu,
        compat_id=raw[48:80].hex(),options_sha256=raw[80:112].hex(),wamr_commit=raw[112:132].hex(),required_safety_flags=safety)


async def installed(link, info, *, allow_repair=False):
    try: result = await link.management(QUERY, info[:68])
    except RemoteError as exc:
        if exc.status == 8: return False
        # Only the initial install probe may treat quarantined identity as
        # replaceable. Final confirmation and uninstall queries stay strict.
        if exc.status == 9 and allow_repair: return False
        raise
    if result != info: raise ValueError('query returned a different identity or size')
    return True

async def install(connect, data, progress=None):
    info = package_info(data); transfers = 0; last_error = None
    # Two transfer attempts; a final connection can only resolve the durable result.
    for _ in range(3):
        try:
            async with connect() as link:
                hello = await link.management(HELLO)
                if len(hello) != 8: raise ValueError('unsupported HELLO')
                abi, maximum, count, flags = struct.unpack('<HIBB', hello)
                if abi != 1 or maximum < len(data) or count < 1 or not flags & 1 or data[:8]==b'TRPKG002' and not flags & CAP_PACKAGE_V2:
                    raise ValueError('device does not support this package')
                if await installed(link, info, allow_repair=True): return 'installed' if transfers else 'already-installed'
                if isinstance(last_error, RemoteError) or transfers >= 2: raise last_error
                await link.management(PREPARE, info)
                transfers += 1
                await link.transfer(data, progress)
                if await installed(link, info): return 'installed'
                raise RuntimeError('FINISH acknowledged but package is not committed')
        except (TimeoutError, ConnectionError, RemoteError, RuntimeError) as exc:
            last_error = exc
    raise last_error or RuntimeError('install failed')

class Link:
    """One BLE connection, serialized calls. Any timeout requires disconnect."""
    def __init__(self, device, timeout=30, fragment=20):
        if not 1 <= fragment <= 480: raise ValueError('bad fragment size')
        self.device=device;self.timeout=timeout;self.fragment=fragment
        self.decoder=Decoder();self.pending=None;self.next_id=1;self.acks=asyncio.Queue();self.failed=False
    async def start(self):
        await self.device.start_notify(CTRL, self._ack)
        await self.device.start_notify(MGMT, self._management)
    def _ack(self, _sender, data):
        if len(data)==8:self.acks.put_nowait(struct.unpack('<BBHI', data))
    def _management(self, _sender, data):
        try: result=self.decoder.feed(bytes(data))
        except ValueError:
            if self.pending and not self.pending[2].done():self.pending[2].set_exception(ValueError('malformed reply'))
            return
        if result and self.pending:
            op,rid,response,payload=result; wanted_op,wanted_id,future=self.pending
            if op==wanted_op and rid==wanted_id and response and not future.done():
                if len(payload)<2:future.set_exception(ValueError('short status'))
                else:
                    status,=struct.unpack('<H',payload[:2])
                    if status:future.set_exception(RemoteError(status))
                    else:future.set_result(payload[2:])
    async def management(self, op, payload=b''):
        if self.failed or self.pending: raise RuntimeError('connection unavailable; reconnect before retry')
        rid=self.next_id;self.next_id=self.next_id%65535+1
        future=asyncio.get_running_loop().create_future();self.pending=(op,rid,future)
        async def exchange():
            for packet in frames(op,rid,payload):await self.device.write_gatt_char(MGMT,packet,response=True)
            return await future
        try:return await asyncio.wait_for(exchange(),self.timeout)
        except RemoteError:raise
        except Exception:
            self.failed=True;raise
        finally:
            if not future.done():future.cancel()
            self.pending=None;self.decoder.reset()
    async def control(self, op, body=b''):
        if self.failed:raise RuntimeError('connection unavailable; reconnect before retry')
        while not self.acks.empty():self.acks.get_nowait()
        async def exchange():
            await self.device.write_gatt_char(CTRL,bytes([op])+body,response=True)
            while True:
                ack=await self.acks.get()
                if ack[0]==3 and op!=3:raise RuntimeError('device aborted transfer')
                if ack[0]==op:return ack[1:]
        try:return await asyncio.wait_for(exchange(),self.timeout)
        except Exception:
            self.failed=True;raise
    async def transfer(self, data, progress=None):
        ok,err,received=await self.control(1,struct.pack('<IB',len(data),2))
        if not ok:raise RemoteError(err)
        if received:raise ValueError('nonzero START watermark')
        sent=0
        while sent<len(data):
            block=data[sent:sent+32768]
            for attempt in range(6):
                for off in range(0,len(block),self.fragment):
                    await asyncio.wait_for(self.device.write_gatt_char(DATA,block[off:off+self.fragment],response=False),self.timeout)
                ok,err,received=await self.control(4,struct.pack('<II',len(block),zlib.crc32(block)&0xffffffff))
                if ok:
                    if received!=sent+len(block):raise ValueError('invalid committed watermark')
                    break
                if received!=sent or err not in (0x104,0x109) or attempt==5:raise RemoteError(err)
            sent+=len(block)
            if progress:progress(sent,len(data))
        ok,err,received=await self.control(2)
        if not ok:raise RemoteError(err)
        if received!=len(data):raise ValueError('invalid FINISH watermark')
