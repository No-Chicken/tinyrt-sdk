"""TinyRT v1 transport. Success requires an exact committed identity query."""
import asyncio
import hashlib
import re
import struct
import zlib
HELLO, LIST, QUERY, PREPARE, UNINSTALL, LAUNCH, STOP = range(0x10, 0x17)
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
    if len(data) < 256 or len(data) > 303104 or data[:8] != b'TRPKG001':
        raise ValueError('invalid TinyRT package header or size')
    total, = struct.unpack_from('<I', data, 12)
    version, = struct.unpack_from('<I', data, 32)
    appid = data[56:88]; name = appid.split(b'\0', 1)[0]
    if total != len(data) or not version or not re.fullmatch(rb'[a-z0-9._-]{1,31}', name):
        raise ValueError('invalid package identity')
    if appid != name + bytes(32-len(name)): raise ValueError('noncanonical app id')
    return appid + struct.pack('<I', version) + hashlib.sha256(data).digest() + struct.pack('<I', total)

async def installed(link, info):
    try: result = await link.management(QUERY, info[:68])
    except RemoteError as exc:
        if exc.status == 8: return False
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
                if abi != 1 or maximum < len(data) or count < 1 or not flags & 1:
                    raise ValueError('device does not support this package')
                if await installed(link, info): return 'installed' if transfers else 'already-installed'
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
