import hmac
from .wire import Error, call, require
from .storage import digest

class Gateway:
    def __init__(self, config):
        self.c = config

    def metadata(self, req):
        return call(self.c.file_address, dict(req, service_key=self.c.secret))

    def dispatch(self, req, data):
        owner = req.get('owner', '')
        token = req.get('token', '')
        require(owner in self.c.users and isinstance(token, str) and hmac.compare_digest(token, self.c.users[owner]), 'AUTH', 'invalid user credential')
        cmd = req.get('cmd')
        allowed = {'init','status','complete','cancel','manifest','delete'}
        # Explicit field allowlist: external callers cannot inject an internal command/address/path.
        fields = {'owner': owner, 'cmd': cmd}
        for key in ('upload_id','object_id','seq','sha256','size','name','cursor'):
            if key in req:
                fields[key] = req[key]
        if cmd in allowed:
            require(not data)
            return self.metadata(fields)
        if cmd == 'upload':
            require(digest(data) == req.get('sha256'), 'CHECKSUM', 'upload checksum mismatch')
            prepared, _ = self.metadata(dict(fields, cmd='prepare'))
            require(len(data) == prepared['length'], 'INVALID', 'wrong chunk length')
            for position in prepared['positions']:
                call(position['address'], dict(cmd='put', chunk_id=prepared['chunk_id'],
                     authorization=position['authorization'], service_key=self.c.secret), data)
            result = self.metadata(dict(fields, cmd='confirm'))
            require('response' not in self.c.faults, 'INJECTED', 'confirmation response lost')
            return result
        if cmd == 'download':
            located, _ = self.metadata(dict(fields, cmd='locations'))
            for position in located['positions']:
                try:
                    _, binary = call(position['address'], dict(cmd='get', service_key=self.c.secret,
                        chunk_id=located['chunk_id'], length=located['length'], sha256=located['sha256']))
                    require(len(binary) == located['length'] and digest(binary) == located['sha256'], 'CORRUPT', 'download checksum mismatch')
                    # Revalidate logical state after the data request.
                    self.metadata(dict(fields, cmd='manifest'))
                    return {'length':len(binary), 'sha256':located['sha256']}, binary
                except (Error, OSError):
                    self.metadata(dict(fields, cmd='bad_replica', chunk_id=located['chunk_id'], node_id=position['node_id']))
            raise Error('DATA_UNAVAILABLE', 'no readable verified replica; metadata retained')
        raise Error('INVALID', 'unsupported user command')
