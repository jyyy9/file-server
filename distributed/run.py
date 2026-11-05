import argparse
import os
import shutil
import threading
import time
from .config import Config
from .gateway import Gateway
from .metadata import Metadata
from .storage import Storage, ID
from .wire import Server, call

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('role', choices=['gateway','file','node'])
    parser.add_argument('--node-id')
    parser.add_argument('--root')
    args = parser.parse_args()
    c = Config()
    if args.role == 'gateway':
        service, address = Gateway(c), c.gateway_address
    elif args.role == 'file':
        service, address = Metadata(c), c.file_address
        from .worker import Worker
        if 'no_worker' not in c.faults:
            Worker(service).start()
    else:
        if args.node_id not in c.nodes or not args.root:
            parser.error('node requires configured --node-id and independent --root')
        service = Storage(args.root, args.node_id, c.secret, c.capacity, c.faults)
        (service.root / '.instance').write_text(service.instance)
        address = c.nodes[args.node_id]
        def heartbeats():
            while True:
                try:
                    used = sum(p.stat().st_size for p in service.root.iterdir() if ID.fullmatch(p.name))
                    call(c.file_address, dict(cmd='heartbeat', service_key=c.secret, node_id=args.node_id,
                        instance=service.instance, volume=service.volume, capacity=c.capacity,
                        free_bytes=max(0,min(c.capacity-used, shutil.disk_usage(service.root).free))))
                except Exception as exc:
                    print('heartbeat:', type(exc).__name__, str(exc), flush=True)
                time.sleep(c.heartbeat)
        threading.Thread(target=heartbeats, daemon=True).start()
    host, port = address.rsplit(':',1)
    print(args.role, address, 'ready', flush=True)
    with Server((host,int(port)), service.dispatch) as server:
        server.serve_forever()

if __name__ == '__main__':
    main()
