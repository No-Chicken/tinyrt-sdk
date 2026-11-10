"""PC half of the EEDB USB debug bridge (tools/docs/EEDB.md).

Frame layout and command sizes must match components/svc/svc_eedb_frame.h; the
C and Python unit tests check the same test vector.
"""
import json
import struct
import time
import zlib

MAGIC = b"EEDB"
VERSION = 1
HEADER = 14
DATA_MAX = 4096

HELLO, STATUS, REBOOT = 0x01, 0x02, 0x03
TOUCH, TAP, SWIPE = 0x10, 0x11, 0x12
KEY, GAMEPAD, MOTION = 0x20, 0x21, 0x22
CAPTURE, READ, RELEASE = 0x30, 0x31, 0x32
UI_TREE, UI_PAGE = 0x40, 0x41
APP_LIST, APP_LAUNCH, APP_STOP, APP_REMOVE = 0x50, 0x51, 0x52, 0x53

STATUS_NAMES = {0: "OK", 1: "BAD_LEN", 2: "BAD_ARG", 3: "BUSY", 4: "NOT_SUPPORTED", 5: "PRIVATE",
                6: "RATE_LIMIT", 7: "NOT_FOUND", 8: "TIMEOUT", 9: "FAILED"}
KEY_ACTIONS = {"down": 0, "up": 1, "short": 2, "double": 3, "long": 4}
# 手柄按键按位置定义（W3C Standard Gamepad）；bit16..23 由宿主保留，不能注入。
PAD_BUTTONS = {
    "a": 0, "b": 1, "x": 2, "y": 3, "lb": 4, "rb": 5, "lt": 6, "rt": 7, "select": 8, "start": 9,
    "l3": 10, "r3": 11, "up": 12, "down": 13, "left": 14, "right": 15,
}
PAD_BUTTONS.update({f"fn{n}": 23 + n for n in range(1, 9)})


class EedbError(Exception):
    def __init__(self, status, command):
        super().__init__(f"{command}: {STATUS_NAMES.get(status, status)}")
        self.status = status


def encode(msg_type: int, seq: int, payload: bytes = b"") -> bytes:
    body = struct.pack("<BBII", VERSION, msg_type, seq, len(payload)) + payload
    return MAGIC + body + struct.pack("<I", zlib.crc32(body))


class FrameReader:
    """Pulls EEDB frames out of a byte stream that also carries plain log text."""

    def __init__(self):
        self.buf = bytearray()
        self.log = bytearray()

    def feed(self, data: bytes):
        self.buf += data
        frames = []
        while True:
            start = self.buf.find(MAGIC)
            if start < 0:
                keep = min(len(self.buf), len(MAGIC)-1)
                while keep and self.buf[-keep:] != MAGIC[:keep]:
                    keep -= 1
                self.log += self.buf[:len(self.buf) - keep]
                del self.buf[:len(self.buf) - keep]
                return frames
            self.log += self.buf[:start]
            del self.buf[:start]
            if len(self.buf) < HEADER:
                return frames
            version, msg_type, seq, length = struct.unpack_from("<BBII", self.buf, 4)
            if version != VERSION or length > DATA_MAX + 2:
                del self.buf[:1]
                continue
            total = HEADER + length + 4
            if len(self.buf) < total:
                return frames
            body = bytes(self.buf[4:HEADER + length])
            crc = struct.unpack_from("<I", self.buf, HEADER + length)[0]
            if crc != zlib.crc32(body):
                del self.buf[:1]
                continue
            payload = bytes(self.buf[HEADER:HEADER + length])
            del self.buf[:total]
            frames.append((msg_type, seq, payload))

    def take_log(self) -> str:
        text = self.log.decode("utf-8", "replace")
        self.log.clear()
        return text


def pad_buttons(names) -> int:
    value = 0
    for name in names:
        if name.lower() not in PAD_BUTTONS:
            raise ValueError(f"unknown gamepad button {name}")
        value |= 1 << PAD_BUTTONS[name.lower()]
    return value


def app_id_field(app_id: str) -> bytes:
    raw = app_id.encode("ascii")
    if not raw or len(raw) >= 64:
        raise ValueError("app_id must be 1..63 ASCII bytes")
    return raw.ljust(64, b"\0")


def rgb565_to_rgb(data: bytes) -> bytes:
    out = bytearray(len(data) // 2 * 3)
    for i in range(len(data) // 2):
        v = data[2 * i] | data[2 * i + 1] << 8
        r, g, b = v >> 11, (v >> 5) & 0x3F, v & 0x1F
        out[3 * i:3 * i + 3] = bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    return bytes(out)


class Bridge:
    """One request at a time over an open pyserial port; logs collect in reader.log."""

    def __init__(self, port, timeout=3.0):
        self.port, self.timeout = port, timeout
        self.reader = FrameReader()
        self.seq = int(time.time()) & 0xFFFF
        self._last_send = 0.0

    # S31 的 USB Serial/JTAG 驱动会把每条回复留在发送端，直到设备下一次写出才一起发出。
    # 所以每个请求后紧跟一个只读的 HELLO（不同序号）：设备回复 HELLO 时就把上一条推出来。
    # 多出的 HELLO 回复按序号忽略；长时间没回复时再补推。请求之间至少隔 21 ms，不触发限速。
    NUDGE_AFTER = 0.25
    MIN_GAP = 0.021

    def _send(self, frame: bytes):
        wait = self._last_send + self.MIN_GAP - time.monotonic()
        if wait > 0:
            time.sleep(wait)
        deadline = time.monotonic() + self.timeout
        sent = 0
        while sent < len(frame):
            if time.monotonic() >= deadline:
                raise TimeoutError('EEDB write timed out')
            written = self.port.write(frame[sent:])
            if not isinstance(written, int) or not 0 <= written <= len(frame)-sent:
                raise OSError('invalid EEDB serial write count')
            sent += written
            if not written: time.sleep(.01)
        self._last_send = time.monotonic()

    def _nudge(self):
        self._send(encode(HELLO, (self.seq - 0x80000000) & 0xFFFFFFFF))

    def _reply(self, msg_type: int, body: bytes) -> bytes:
        status = struct.unpack_from("<H", body)[0] if len(body) >= 2 else 9
        if status:
            raise EedbError(status, f"0x{msg_type:02x}")
        return body[2:]

    def _wait(self, wanted: dict, end: float):
        """Waits for a reply whose seq is in wanted ({seq: msg_type}); returns (seq, body)."""
        next_nudge = time.monotonic() + self.NUDGE_AFTER
        while time.monotonic() < end:
            data = self.port.read(self.port.in_waiting or 1)
            for reply_type, seq, body in self.reader.feed(data):
                if seq in wanted and reply_type == (wanted[seq] | 0x80):
                    return seq, body
            if time.monotonic() >= next_nudge:
                self._nudge()
                next_nudge = time.monotonic() + 1.0
        raise TimeoutError("no EEDB reply (development firmware with EEDB?)")

    def call(self, msg_type: int, payload: bytes = b"", timeout=None) -> bytes:
        self.seq = (self.seq + 1) & 0xFFFFFFFF
        seq = self.seq
        self._send(encode(msg_type, seq, payload))
        self._nudge()
        _, body = self._wait({seq: msg_type}, time.monotonic() + (timeout or self.timeout))
        return self._reply(msg_type, body)

    def json(self, msg_type: int, payload: bytes = b"") -> dict:
        return json.loads(self.call(msg_type, payload).decode("utf-8"))

    def tap(self, x, y, hold_ms=0):
        self.call(TAP, struct.pack("<hhH", x, y, hold_ms), timeout=self.timeout + hold_ms / 1000)

    def swipe(self, x1, y1, x2, y2, ms):
        self.call(SWIPE, struct.pack("<hhhhH", x1, y1, x2, y2, ms), timeout=self.timeout + ms / 1000)

    def touch(self, action, x, y):
        self.call(TOUCH, struct.pack("<Bhh", action, x, y))

    def key(self, action, power=False):
        self.call(KEY, bytes([2 if power else 1, KEY_ACTIONS[action]]), timeout=self.timeout + 2)

    def gamepad(self, buttons=0, lx=0, ly=0, rx=0, ry=0, lt=0, rt=0, connected=True):
        self.call(GAMEPAD, struct.pack("<IbbbbBBB", buttons, lx, ly, rx, ry, lt, rt, 1 if connected else 0))

    def motion(self, enable, x=0, y=0, z=0):
        self.call(MOTION, struct.pack("<Bhhh", 1 if enable else 0, x, y, z))

    def capture(self, scale=1):
        """Returns (width, height, rgb565 little-endian bytes)."""
        w, h, size, fmt = struct.unpack("<HHIB", self.call(CAPTURE, bytes([scale]), timeout=self.timeout + 3))
        if fmt != 0:
            raise ValueError(f"unknown capture format {fmt}")
        # 流水线拉取：始终两个 READ 在途，后一个请求的回复把前一块推出来。
        offsets = list(range(0, size, DATA_MAX))
        chunks, pending = {}, {}
        try:
            index = 0
            while len(chunks) < len(offsets):
                while index < len(offsets) and len(pending) < 2:
                    offset = offsets[index]
                    self.seq = (self.seq + 1) & 0xFFFFFFFF
                    pending[self.seq] = offset
                    self._send(encode(READ, self.seq, struct.pack("<IH", offset, min(DATA_MAX, size - offset))))
                    index += 1
                if index == len(offsets):
                    self._nudge()
                seq, body = self._wait({q: READ for q in pending}, time.monotonic() + self.timeout)
                chunks[pending.pop(seq)] = self._reply(READ, body)
        finally:
            try:
                self.call(RELEASE)
            except (EedbError, TimeoutError):
                pass
        data = b"".join(chunks[o] for o in offsets)
        return w, h, bytes(data)
