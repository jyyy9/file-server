"""Streaming client for distributed mode; legacy FileClient remains supported separately."""
import argparse
import concurrent.futures
import json
import os
import tempfile
import time
from pathlib import Path
from .storage import digest, syncdir
from .wire import call, require, Error

class Client:
    def __init__(self, address, owner, token, concurrency=4):
        require(1 <= concurrency <= 16)
        self.address, self.owner, self.token, self.concurrency = address, owner, token, concurrency

    def request(self, cmd, data=b'', **fields):
        for attempt in range(3):
            try:
                return call(self.address, dict(fields, cmd=cmd, owner=self.owner, token=self.token), data)
            except OSError:
                if attempt == 2:
                    raise
                time.sleep(0.1*2**attempt)

    def upload(self, path, upload_id=None, checkpoint=None):
        path = Path(path)
        if upload_id is None:
            initialized,_ = self.request('init', size=path.stat().st_size, name=path.name)
            upload_id = initialized['upload_id']
            if checkpoint:
                Path(checkpoint).write_text(json.dumps(initialized))
        status,_ = self.request('status', upload_id=upload_id)
        require(path.stat().st_size == status['size'], 'CONFLICT', 'local file size changed')
        if status['state'] == 'completed':
            return {'object_id': status['object_id'], 'state':'published'}
        def send(seq):
            with open(path,'rb') as source:
                source.seek(seq*status['chunk_size'])
                data = source.read(status['chunk_size'])
            return self.request('upload', data, upload_id=upload_id, seq=seq, sha256=digest(data))[0]
        cursor = 0
        while True:
            page,_ = self.request('status', upload_id=upload_id, cursor=cursor)
            with concurrent.futures.ThreadPoolExecutor(self.concurrency) as pool:
                list(pool.map(send,page['missing']))
            if page['next_cursor'] is None:
                break
            cursor = page['next_cursor']
        return self.request('complete', upload_id=upload_id)[0]

    def download(self, object_id, target):
        meta,_ = self.request('manifest', object_id=object_id)
        target = Path(target)
        fd, temporary = tempfile.mkstemp(prefix='.download-', dir=target.resolve().parent)
        try:
            with os.fdopen(fd, 'wb') as out:
                total = 0
                for seq in range(meta['chunks']):
                    chunk,data = self.request('download', object_id=object_id, seq=seq)
                    expected = min(meta['chunk_size'],meta['size']-seq*meta['chunk_size'])
                    require(len(data)==expected and chunk['length']==expected and digest(data)==chunk['sha256'], 'CORRUPT', 'invalid downloaded chunk')
                    out.write(data)
                    total += len(data)
                require(total==meta['size'], 'CORRUPT', 'download length mismatch')
                out.flush()
                os.fsync(out.fileno())
            os.replace(temporary,target)
            syncdir(target.resolve().parent)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)

def main():
    p = argparse.ArgumentParser()
    p.add_argument('command',choices=['upload','resume','status','complete','cancel','download','delete'])
    p.add_argument('value')
    p.add_argument('extra',nargs='?')
    p.add_argument('--checkpoint')
    a = p.parse_args()
    c = Client(os.getenv('DS_GATEWAY_ADDRESS','127.0.0.1:9300'),os.environ['DS_OWNER'],os.environ['DS_TOKEN'])
    if a.command in ('upload','resume'):
        result = c.upload(a.value,a.extra if a.command=='resume' else None,a.checkpoint)
    elif a.command=='download':
        require(a.extra is not None)
        c.download(a.value,a.extra)
        result = {'downloaded':a.extra}
    else:
        key = 'object_id' if a.command=='delete' else 'upload_id'
        result,_ = c.request(a.command,**{key:a.value})
    print(json.dumps(result))

if __name__=='__main__':
    main()
