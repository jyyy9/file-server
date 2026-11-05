import hashlib
import hmac
import json
import os
import re
import tempfile
import threading
import time
import uuid
import fcntl
from pathlib import Path
from .wire import Error, require

ID = re.compile(r'^[a-f0-9]{32}$')

def digest(data):
    return hashlib.sha256(data).hexdigest()

def ticket(secret, claims):
    raw = json.dumps(claims, sort_keys=True, separators=(',', ':'))
    return {'claims': claims, 'mac': hmac.new(secret.encode(), raw.encode(), hashlib.sha256).hexdigest()}

def verify(secret, authorization):
    claims = authorization['claims']
    require(hmac.compare_digest(ticket(secret, claims)['mac'], authorization['mac']), 'AUTH', 'bad authorization')
    require(claims['expires'] >= time.time(), 'EXPIRED', 'authorization expired')
    return claims

def syncdir(path):
    fd = os.open(str(path), os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)

class Storage:
    def __init__(self, root, node_id, secret, capacity=1024**3, faults=None):
        require(bool(re.fullmatch(r'[a-zA-Z0-9_-]{1,64}', node_id)))
        self.root = Path(root)
        self.root.mkdir(parents=True, exist_ok=True)
        self.lockfile = open(self.root / '.lock', 'a')
        fcntl.flock(self.lockfile, fcntl.LOCK_EX | fcntl.LOCK_NB)
        self.node_id, self.secret, self.capacity = node_id, secret, capacity
        self.instance = uuid.uuid4().hex
        identity = self.root / '.identity'
        if identity.exists():
            record = json.loads(identity.read_text())
            require(record['node_id'] == node_id, 'IDENTITY', 'data directory belongs to another node')
            self.volume = record['volume']
        else:
            self.volume = uuid.uuid4().hex
            with open(identity, 'x') as out:
                json.dump({'node_id': node_id, 'volume': self.volume}, out)
                out.flush()
                os.fsync(out.fileno())
            syncdir(self.root)
        self.locks = [threading.Lock() for _ in range(64)]
        self.capacity_lock = threading.Lock()
        self.faults = faults or set()
        for path in self.root.glob('.tmp-*'):
            path.unlink()
        syncdir(self.root)

    def dispatch(self, req, data):
        require(hmac.compare_digest(req.get('service_key', ''), self.secret), 'AUTH', 'internal credential required')
        cmd = req['cmd']
        if cmd == 'inventory':
            cursor = req.get('cursor', '')
            require(isinstance(cursor, str) and len(cursor) <= 32)
            # Bounded output; directory iteration itself does not load all content.
            import heapq
            names = heapq.nsmallest(257, (p.name for p in self.root.iterdir() if ID.fullmatch(p.name) and p.name > cursor))
            return {'ids': names[:256], 'more': len(names) > 256}, b''
        cid = req.get('chunk_id', '')
        require(isinstance(cid, str) and ID.fullmatch(cid), 'INVALID', 'invalid chunk ID')
        path = self.root / cid
        with self.capacity_lock, self.locks[int(cid[:2], 16) % len(self.locks)]:
            if cmd == 'put':
                claims = verify(self.secret, req['authorization'])
                require(claims['node_id'] == self.node_id and claims['chunk_id'] == cid and
                        claims['volume'] == self.volume and claims['instance'] == self.instance and
                        ID.fullmatch(claims['upload_id']), 'AUTH', 'wrong write target')
                require(len(data) == claims['length'] and digest(data) == claims['sha256'], 'CHECKSUM', 'content mismatch')
                if path.exists():
                    existing = path.read_bytes()
                    require(len(existing) == len(data) and digest(existing) == claims['sha256'], 'CONFLICT', 'immutable chunk conflict')
                    # Retry also synchronizes directory after an uncertain prior publication.
                    syncdir(self.root)
                    return {'length': len(data), 'sha256': digest(data)}, b''
                used = sum(p.stat().st_size for p in self.root.iterdir() if ID.fullmatch(p.name))
                require(used + len(data) <= self.capacity, 'CAPACITY', 'node full')
                fd, tmp = tempfile.mkstemp(prefix='.tmp-', dir=self.root)
                try:
                    view = memoryview(data)
                    while view:
                        if 'write' in self.faults:
                            raise OSError('injected write failure')
                        try:
                            count = os.write(fd, view)
                        except InterruptedError:
                            continue
                        require(count > 0, 'IO', 'short write')
                        view = view[count:]
                    if 'sync' in self.faults:
                        raise OSError('injected fsync failure')
                    os.fsync(fd)
                    os.close(fd)
                    fd = -1
                    # link is atomic and refuses replacement, even across processes.
                    os.link(tmp, path)
                    os.unlink(tmp)
                    syncdir(self.root)
                finally:
                    if fd >= 0:
                        os.close(fd)
                    if os.path.exists(tmp):
                        os.unlink(tmp)
                return {'length': len(data), 'sha256': digest(data)}, b''
            if cmd in ('get', 'stat'):
                require(path.exists(), 'MISSING', 'chunk absent')
                require(path.stat().st_size <= 4 * 1024 * 1024, 'CORRUPT', 'oversized chunk')
                content = path.read_bytes()
                require(len(content) == req['length'] and digest(content) == req['sha256'], 'CORRUPT', 'chunk checksum mismatch')
                return {'length': len(content), 'sha256': digest(content)}, content if cmd == 'get' else b''
            if cmd == 'remove':
                require(req.get('instance') == self.instance and req.get('volume') == self.volume, 'STALE', 'delete target changed')
                path.unlink(missing_ok=True)
                syncdir(self.root)
                return {}, b''
            raise Error('INVALID', 'unknown storage command')
