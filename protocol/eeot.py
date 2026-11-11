"""USB firmware protocol. No BLE or serial imports until selected."""
import struct
import secrets
import time
import zlib
import json

MAGIC = b'EEOT'
VERSION = 1
BLOCK_MAX = 32768
PAYLOAD_MAX = BLOCK_MAX + 12
# S31 的 USB 接收环为 4096 B。把整帧（含 30 B 开销）放进环内再等 ACK。
USB_DATA_BYTES = 3840
HEADER = struct.Struct('<4sBBII')
ACK = struct.Struct('<IIII')
QUERY, START, DATA, FINISH, CANCEL, STATUS, INFO, ERASE, MANAGEMENT, RESULT_ACK = range(1, 11)


def encode_frame(kind, sequence, payload):
    if len(payload) > PAYLOAD_MAX:
        raise ValueError('frame too large')
    head = HEADER.pack(MAGIC, VERSION, kind, sequence, len(payload))
    return head + payload + struct.pack('<I', zlib.crc32(head[4:] + payload))


class FrameDecoder:
    def __init__(self): self.buffer = bytearray()
    def feed(self, data):
        self.buffer.extend(data)
        found = []
        while True:
            at = self.buffer.find(MAGIC)
            if at < 0:
                self.buffer[:] = self.buffer[-3:]
                break
            if at: del self.buffer[:at]
            if len(self.buffer) < HEADER.size: break
            _, version, kind, sequence, length = HEADER.unpack_from(self.buffer)
            if version != VERSION or length > PAYLOAD_MAX:
                del self.buffer[0]; continue
            size = HEADER.size + length + 4
            if len(self.buffer) < size: break
            crc, = struct.unpack_from('<I', self.buffer, size - 4)
            if zlib.crc32(self.buffer[4:size - 4]) != crc:
                del self.buffer[0]; continue
            found.append((kind, sequence, bytes(self.buffer[HEADER.size:size - 4])))
            del self.buffer[:size]
        return found


def transfer_image(request, image, progress=None, *, target=0):
    if not image: raise ValueError('镜像为空')
    session = 0
    try:
        nonce = secrets.randbits(32) or 1
        err, session, offset, state = request(START, struct.pack('<BII', target, len(image), nonce))
        if err or not session: raise RuntimeError(f'设备拒绝开始: err=0x{err:04x}（设备忙或页面类型不符；请退出接收页后重试）')
        if offset != 0: raise RuntimeError('设备存在未完成会话，请退出接收页后重试')
        for offset in range(0, len(image), USB_DATA_BYTES):
            block = image[offset:offset + USB_DATA_BYTES]
            payload = struct.pack('<III', session, offset, zlib.crc32(block)) + block
            err, token, received, state = request(DATA, payload)
            if err or token != session or received != offset + len(block):
                raise RuntimeError(f'设备拒绝数据: err=0x{err:04x}, offset={received}')
            if progress: progress(received, len(image))
        err, token, received, state = request(FINISH, struct.pack('<I', session))
        if err or token != session or received != len(image) or state != 2:
            raise RuntimeError(f'设备拒绝镜像: err=0x{err:04x}, received={received}')
        return received
    except BaseException:
        if session:
            try: request(CANCEL, struct.pack('<I', session))
            except Exception: pass
        raise


class USBTransport:
    def __init__(self, port=None):
        import serial
        if port is None:
            ports = usb_ports()
            if len(ports) != 1: raise RuntimeError('请用 --port 指定 USB 端口；匹配端口: ' + ', '.join(ports))
            port = ports[0]
        self.serial = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=5)
        # 打开之前设置，避免默认控制线在打开/改值时触发复位。
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = port
        self.serial.open()
        self.sequence = 0
        self.decoder = FrameDecoder()
    def close(self): self.serial.close()
    def _send_frame(self, frame):
        deadline = time.monotonic() + 5
        sent = 0
        while sent < len(frame):
            remaining = deadline - time.monotonic()
            if remaining <= 0: raise TimeoutError('USB 帧发送超时')
            # write_timeout 只约束 write；Windows flush 的等待队列循环没有截止时间。
            # ACK 已证明帧被设备处理，无需 flush。每次短写沿原偏移续发。
            self.serial.write_timeout = remaining
            written = self.serial.write(frame[sent:])
            if not isinstance(written, int) or not 0 <= written <= len(frame) - sent:
                raise OSError('USB 返回非法写入长度')
            sent += written
            if written == 0: time.sleep(min(0.01, remaining))
    # S31 的 USB Serial/JTAG 驱动有时把一条回复留在发送端，直到设备下一次写出才一起发出。
    # 迟迟没有回复时补发一个无副作用的 INFO（不同序号）把它推出来；多出的 INFO 回复按序号忽略。
    NUDGE_AFTER = 0.25

    def request_payload(self, kind, payload, *, timeout=None, attempts=3):
        self.sequence = (self.sequence + 1) & 0xffffffff
        frame = encode_frame(kind, self.sequence, payload)
        for attempt in range(attempts):
            self._send_frame(frame)
            start = time.monotonic()
            deadline = start + (timeout if timeout is not None else 100 if kind == START else 45 if kind in (FINISH, MANAGEMENT, ERASE) else 12)
            next_nudge = start + self.NUDGE_AFTER
            while time.monotonic() < deadline:
                # 无数据时只等首字节；短 ACK 已到齐时立即解码。
                size = min(self.serial.in_waiting, 4096) or 1
                for rkind, seq, data in self.decoder.feed(self.serial.read(size)):
                    if rkind == kind | 0x80 and seq == self.sequence:
                        return data
                if time.monotonic() >= next_nudge:
                    self._send_frame(encode_frame(INFO, (self.sequence - 0x80000000) & 0xffffffff, b''))
                    next_nudge = time.monotonic() + 1.0
        raise TimeoutError('USB 确认超时，请核对设备处于对应的接收页')

    def request(self, kind, payload):
        data = self.request_payload(kind, payload)
        if len(data) != ACK.size: raise RuntimeError('USB response size mismatch')
        return ACK.unpack(data)
    def device_info(self):
        data = self.request_payload(INFO, b'')
        if len(data) < 4: raise ValueError('invalid device info response')
        error, = struct.unpack_from('<I', data)
        if error: raise RuntimeError(f'device info rejected: err=0x{error:04x}')
        value = json.loads(data[4:].decode('utf-8'))
        if not isinstance(value, dict) or value.get('schema') != 1:
            raise ValueError('unsupported device info schema')
        return value


def usb_ports():
    from serial.tools.list_ports import comports
    return [p.device for p in comports() if p.vid == 0x303a and p.pid == 0x1001]


def resolve_transport(mode, port=None, address=None, *, scan=False, debug_probe=None):
    """只探测 EEBadge 协议身份；一旦选定介质，传输失败不切换。"""
    if port and address:
        raise ValueError('--port 与 --address 不能同时使用')
    if mode != 'auto':
        if mode == 'ble' and port: raise ValueError('--port 仅用于 USB')
        if mode == 'usb' and address: raise ValueError('--address 仅用于 BLE')
        return mode, port
    if address or scan: return 'ble', None
    if port: return 'usb', port
    try: candidates = usb_ports()
    except ImportError: return 'ble', None
    matches = []
    for candidate in candidates:
        if debug_probe and debug_probe(candidate):
            matches.append(candidate)
            continue
        link = None
        try:
            link = USBTransport(candidate)
            raw = link.request_payload(INFO, b'', timeout=1, attempts=1)
            if len(raw) < 4 or struct.unpack_from('<I', raw)[0]: continue
            info = json.loads(raw[4:])
            if isinstance(info, dict) and info.get('schema') == 1 and all(k in info for k in ('firmware','tinyrt','sdk','transports')):
                matches.append(candidate)
        except (OSError, ValueError, TimeoutError): pass
        finally:
            if link: link.close()
    if len(matches) > 1:
        raise RuntimeError('发现多台 USB 吧唧，请用 --port 指定：' + ', '.join(matches))
    return ('usb', matches[0]) if matches else ('ble', None)


class USBApplicationLink:
    """复用 SDK 的安装、精确身份查询和结果恢复流程。"""
    def __init__(self, port): self.port = port
    async def __aenter__(self):
        import asyncio
        self.link = await asyncio.to_thread(USBTransport, self.port)
        return self
    async def __aexit__(self, *_): self.link.close()
    async def management(self, opcode, payload=b''):
        import asyncio
        from client import GET_DEVICE_INFO, RemoteError
        if opcode == GET_DEVICE_INFO and not payload:
            return json.dumps(await asyncio.to_thread(self.link.device_info)).encode('utf-8')
        raw = await asyncio.to_thread(self.link.request_payload, MANAGEMENT, bytes([opcode]) + payload,
                                     timeout=45 if opcode in (0x13,0x20) else 12)
        if len(raw) < 4: raise ValueError('invalid USB management response')
        status, = struct.unpack_from('<I', raw)
        if status: raise RemoteError(status)
        result=raw[4:]
        if opcode==0x12 and len(result)==72 and result[:68]==payload:
            # 先收到完整身份才通知设备允许返回；ACK 自身丢失不推翻已确认结果。
            try:
                await asyncio.to_thread(self.link.request_payload, RESULT_ACK, result, timeout=1, attempts=1)
            except (OSError, RuntimeError, TimeoutError): pass
        return result
    async def transfer(self, data, progress=None):
        import asyncio
        return await asyncio.to_thread(transfer_image, self.link.request, data, progress, target=2)
