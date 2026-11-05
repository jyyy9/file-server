"""The existing v1 24-byte network header, JSON body and raw binary field."""
import json
import socket
import socketserver
import struct
import threading
import zlib

HEADER = struct.Struct('!HHIIIII')
MAX_JSON = 1024 * 1024
MAX_DATA = 4 * 1024 * 1024
TIMEOUT = 10

class Error(Exception):
    def __init__(self, code, message):
        self.code = code
        super().__init__(message)

def require(condition, code='INVALID', message='invalid request'):
    if not condition:
        raise Error(code, message)

def exact(sock, size):
    out = bytearray()
    while len(out) < size:
        part = sock.recv(min(size - len(out), 65536))
        if not part:
            raise Error('TRUNCATED', 'connection ended before complete frame')
        out.extend(part)
    return bytes(out)

def receive(sock):
    raw = exact(sock, HEADER.size)
    magic, version, kind, jl, bl, rid, crc = HEADER.unpack(raw)
    require(magic == 0x4D46 and version == 1 and kind in (1, 2), 'PROTOCOL', 'invalid header')
    require(jl <= MAX_JSON and bl <= MAX_DATA, 'LIMIT', 'frame too large')
    body = exact(sock, jl + bl)
    require(zlib.crc32(raw[:20] + b'\0' * 4 + body) == crc, 'PROTOCOL', 'CRC mismatch')
    try:
        meta = json.loads(body[:jl])
    except (ValueError, UnicodeError):
        raise Error('INVALID', 'invalid JSON')
    require(isinstance(meta, dict))
    return meta, body[jl:], rid

def send(sock, meta, data=b'', rid=1, kind=2):
    body = json.dumps(meta, separators=(',', ':')).encode()
    require(len(body) <= MAX_JSON and len(data) <= MAX_DATA, 'LIMIT', 'frame too large')
    raw = HEADER.pack(0x4D46, 1, kind, len(body), len(data), rid, 0)
    crc = zlib.crc32(raw + body + data)
    sock.sendall(raw[:20] + struct.pack('!I', crc) + body + data)

def call(address, meta, data=b'', timeout=TIMEOUT):
    host, port = address.rsplit(':', 1)
    with socket.create_connection((host, int(port)), timeout=timeout) as sock:
        sock.settimeout(timeout)
        send(sock, meta, data, kind=1)
        result, binary, _ = receive(sock)
    if result.get('code') != 'OK':
        raise Error(result.get('code', 'REMOTE'), result.get('message', 'remote error'))
    return result, binary

class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True
    request_queue_size = 32

    def __init__(self, address, dispatch, concurrency=32):
        self.slots = threading.BoundedSemaphore(concurrency)
        self.dispatch = dispatch
        super().__init__(address, Handler)

    def process_request(self, request, client_address):
        if not self.slots.acquire(False):
            request.close()
            return
        try:
            super().process_request(request, client_address)
        except BaseException:
            self.slots.release()
            raise

    def process_request_thread(self, request, address):
        try:
            super().process_request_thread(request, address)
        finally:
            self.slots.release()

class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(TIMEOUT)
        rid = 1
        try:
            meta, binary, rid = receive(self.request)
            result, data = self.server.dispatch(meta, binary)
            send(self.request, dict(result, code='OK'), data, rid)
        except Exception as exc:
            try:
                send(self.request, {'code': getattr(exc, 'code', 'INTERNAL'),
                                    'message': str(exc) if isinstance(exc, Error) else 'internal service error'}, rid=rid)
            except OSError:
                pass
