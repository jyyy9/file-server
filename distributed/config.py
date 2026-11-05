import json
import os
from .wire import require

class Config:
    def __init__(self):
        self.secret = os.environ.get('DS_SERVICE_KEY', '')
        require(len(self.secret) >= 24, 'CONFIG', 'DS_SERVICE_KEY must contain at least 24 characters')
        self.users = json.loads(os.environ.get('DS_USERS', '{}'))
        require(isinstance(self.users, dict) and all(isinstance(k, str) and k and isinstance(v, str) and len(v) >= 24 for k, v in self.users.items()), 'CONFIG', 'invalid DS_USERS')
        self.nodes = json.loads(os.environ.get('DS_NODES', '{"n1":"127.0.0.1:9101","n2":"127.0.0.1:9102","n3":"127.0.0.1:9103"}'))
        require(len(self.nodes) >= 2 and len(set(self.nodes.values())) == len(self.nodes), 'CONFIG', 'distinct nodes required')
        self.chunk = self.number('DS_CHUNK_SIZE', 4*1024*1024, 1, 4*1024*1024)
        self.replicas = self.number('DS_REPLICAS', 2, 2, len(self.nodes))
        self.capacity = self.number('DS_CAPACITY', 1024**3, 1, 2**60)
        self.ttl = self.number('DS_UPLOAD_TTL', 86400, 5, 604800)
        self.auth_ttl = self.number('DS_AUTH_TTL', 30, 2, 300)
        self.suspect = self.number('DS_SUSPECT_SECONDS', 5, 2, 3600)
        self.dead = self.number('DS_DEAD_SECONDS', 15, self.suspect+1, 86400)
        self.heartbeat = self.number('DS_HEARTBEAT_SECONDS', 1, 1, self.suspect-1)
        self.repair_workers = self.number('DS_REPAIR_WORKERS', 1, 1, 8)
        self.bandwidth = self.number('DS_REPAIR_BPS', 8*1024*1024, 1024, 1024**3)
        self.lease = self.number('DS_TASK_LEASE', 120, 40 + (self.chunk*self.repair_workers//self.bandwidth), 86400)
        self.max_chunks = self.number('DS_MAX_CHUNKS', 8192, 1, 8192)
        self.file_address = os.getenv('DS_FILE_ADDRESS', '127.0.0.1:9200')
        self.gateway_address = os.getenv('DS_GATEWAY_ADDRESS', '127.0.0.1:9300')
        self.faults = set(os.getenv('DS_FAULTS', '').split(','))
        for address in [*self.nodes.values(), self.file_address, self.gateway_address]:
            host, port = address.rsplit(':', 1)
            require(host and 1 <= int(port) <= 65535, 'CONFIG', 'invalid address')

    @staticmethod
    def number(key, default, low, high):
        n = int(os.getenv(key, str(default)))
        require(low <= n <= high, 'CONFIG', 'invalid ' + key)
        return n
