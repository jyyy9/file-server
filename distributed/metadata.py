"""FileService: metadata only. No access to any StorageNode directory."""
import hmac
import re
import time
import uuid
from .db import transaction
from .storage import ticket
from .wire import Error, require, call


def ident():
    return uuid.uuid4().hex

class Metadata:
    def __init__(self, config):
        self.c = config

    def internal(self, req):
        require(hmac.compare_digest(req.get('service_key', ''), self.c.secret), 'AUTH', 'service credential required')

    def active(self, db, upload, owner=None, writing=True):
        rows = db.q('SELECT u.object_id,u.state,u.expires,o.owner,o.size,o.chunk_size,o.replicas,o.state FROM ds_uploads u JOIN ds_objects o ON o.object_id=u.object_id WHERE u.upload_id=? FOR UPDATE', upload)
        require(rows, 'NOT_FOUND', 'upload not found')
        row = rows[0]
        require(owner is None or row[3] == owner, 'FORBIDDEN', 'object belongs to another user')
        require(row[7] != 'deleted', 'DELETED', 'object deleted')
        if writing:
            require(row[1] == 'uploading' and float(row[2]) > time.time(), 'EXPIRED', 'upload not active')
        return row

    def object(self, db, oid, owner, published=True):
        rows = db.q('SELECT object_id,owner,name,size,chunk_size,replicas,state FROM ds_objects WHERE object_id=? FOR UPDATE', oid)
        require(rows, 'NOT_FOUND', 'object not found')
        r = rows[0]
        require(r[1] == owner, 'FORBIDDEN', 'object belongs to another user')
        require(r[6] != 'deleted', 'DELETED', 'object deleted')
        if published:
            require(r[6] == 'published', 'NOT_PUBLISHED', 'object not published')
        return r

    def reserve(self, db, cid, length, count, excluded=()):
        # Single persistent allocator row serializes every capacity mutation.
        # Ordering is shared by foreground and workers: allocator -> upload -> nodes.
        nodes = db.q("SELECT node_id,volume,capacity,free_bytes,reserved,used_bytes,heartbeat,state FROM ds_nodes ORDER BY reserved+used_bytes,node_id FOR UPDATE")
        chosen = []
        for nid, volume, capacity, free, reserved, used, heartbeat, state in nodes:
            if nid in excluded or state != 'healthy' or time.time()-float(heartbeat) >= self.c.suspect:
                continue
            if min(int(capacity)-int(used)-int(reserved), int(free)-int(reserved)) < length:
                continue
            db.q("INSERT INTO ds_replicas VALUES(?,?,?,'reserved') ON DUPLICATE KEY UPDATE volume=VALUES(volume),state='reserved'", cid, nid, volume)
            db.q('UPDATE ds_nodes SET reserved=reserved+? WHERE node_id=?', length, nid)
            chosen.append(nid)
            if len(chosen) == count:
                break
        require(len(chosen) == count, 'CAPACITY', 'not enough healthy distinct nodes with free capacity')
        return chosen

    def positions(self, db, cid, upload, length, sha):
        rows = db.q("SELECT n.node_id,n.address,n.volume,n.instance,n.heartbeat,n.state,r.state FROM ds_replicas r JOIN ds_nodes n ON n.node_id=r.node_id WHERE r.chunk_id=? AND r.volume=n.volume AND r.state IN ('reserved','valid')", cid)
        result = []
        for nid, address, volume, instance, heartbeat, health, state in rows:
            require(health == 'healthy' and time.time()-float(heartbeat) < self.c.suspect, 'UNAVAILABLE', 'assigned node unavailable; retry after recovery')
            claims = dict(upload_id=upload, chunk_id=cid, length=length, sha256=sha,
                          node_id=nid, volume=volume, instance=instance,
                          expires=time.time()+self.c.auth_ttl)
            result.append(dict(node_id=nid, address=address, authorization=ticket(self.c.secret, claims)))
        return result

    def dispatch(self, req, data):
        self.internal(req)
        cmd = req.get('cmd')
        if cmd == 'heartbeat':
            return self.heartbeat(req), b''
        if cmd == 'confirm':
            return self.confirm(req), b''
        owner = req.get('owner')
        require(isinstance(owner, str) and owner in self.c.users, 'AUTH', 'unknown owner')
        if cmd == 'init':
            size, name = req.get('size'), req.get('name')
            require(type(size) is int and 0 <= size <= self.c.chunk*self.c.max_chunks)
            require(isinstance(name, str) and 0 < len(name) <= 255)
            count = (size+self.c.chunk-1)//self.c.chunk
            oid, upload = ident(), ident()
            with transaction() as db:
                db.q('SELECT id FROM ds_allocator WHERE id=1 FOR UPDATE')
                db.q("INSERT INTO ds_objects VALUES(?,?,?,?,?,?,'staging')", oid, owner, name, size, self.c.chunk, self.c.replicas)
                db.q("INSERT INTO ds_uploads VALUES(?,?,'uploading',?)", upload, oid, time.time()+self.c.ttl)
                for seq in range(count):
                    cid = ident()
                    length = min(self.c.chunk, size-seq*self.c.chunk)
                    db.q("INSERT INTO ds_chunks(chunk_id,upload_id,seq,length,state) VALUES(?,?,?,?,'pending')", cid, upload, seq, length)
                    self.reserve(db, cid, length, self.c.replicas)
            return dict(object_id=oid, upload_id=upload, chunk_size=self.c.chunk, chunks=count), b''
        if cmd in ('status', 'prepare', 'complete', 'cancel'):
            with transaction() as db:
                db.q('SELECT id FROM ds_allocator WHERE id=1 FOR UPDATE')
                row = self.active(db, req.get('upload_id'), owner, writing=False)
                oid, state, expires, _, size, chunk_size, replicas, object_state = row
                upload = req['upload_id']
                if cmd == 'status':
                    cursor = req.get('cursor', 0)
                    require(type(cursor) is int and cursor >= 0)
                    missing = db.q("SELECT seq FROM ds_chunks WHERE upload_id=? AND state<>'confirmed' AND seq>=? ORDER BY seq LIMIT 257", upload, cursor)
                    progress = db.q("SELECT COALESCE(SUM(length),0) FROM ds_chunks WHERE upload_id=? AND state='confirmed'", upload)[0][0]
                    return dict(object_id=oid, state=state, expires=float(expires), confirmed_bytes=int(progress),
                                chunk_size=int(chunk_size), size=int(size), missing=[int(r[0]) for r in missing[:256]],
                                next_cursor=int(missing[255][0])+1 if len(missing)>256 else None), b''
                if cmd == 'complete' and state == 'completed' and object_state == 'published':
                    return {'object_id': oid, 'state': 'published'}, b''
                if cmd == 'cancel' and state in ('cancelled', 'expired'):
                    return {'state': state}, b''
                require(state == 'uploading' and float(expires) > time.time(), 'EXPIRED', 'upload not active')
                if cmd == 'cancel':
                    self.retire(db, upload, 'cancelled')
                    return {'state': 'cancelled'}, b''
                if cmd == 'complete':
                    chunks = db.q('SELECT chunk_id,length,state,seq FROM ds_chunks WHERE upload_id=? ORDER BY seq FOR UPDATE', upload)
                    expected = (int(size)+int(chunk_size)-1)//int(chunk_size)
                    require(len(chunks) == expected, 'INCOMPLETE', 'chunk count mismatch')
                    for index, (cid, length, cstate, seq) in enumerate(chunks):
                        require(int(seq)==index and int(length)==min(int(chunk_size), int(size)-index*int(chunk_size)) and cstate == 'confirmed', 'INCOMPLETE', 'missing confirmed chunk')
                        valid = db.q("SELECT node_id FROM ds_replicas WHERE chunk_id=? AND state='valid'", cid)
                        require(len(valid) >= int(replicas), 'INCOMPLETE', 'replica confirmation missing')
                    db.q("UPDATE ds_objects SET state='published' WHERE object_id=? AND state='staging'", oid)
                    db.q("UPDATE ds_uploads SET state='completed' WHERE upload_id=? AND state='uploading'", upload)
                    return {'object_id': oid, 'state': 'published'}, b''
                seq, sha = req.get('seq'), req.get('sha256')
                require(type(seq) is int and seq >= 0 and isinstance(sha, str) and re.fullmatch('[a-f0-9]{64}', sha))
                chunks = db.q('SELECT chunk_id,length,sha256,state FROM ds_chunks WHERE upload_id=? AND seq=? FOR UPDATE', upload, seq)
                require(chunks, 'NOT_FOUND', 'chunk not found')
                cid, length, old_sha, cstate = chunks[0]
                require(not old_sha or old_sha == sha, 'CONFLICT', 'checksum already fixed for this chunk')
                db.q('UPDATE ds_chunks SET sha256=? WHERE chunk_id=?', sha, cid)
                positions = self.positions(db, cid, upload, int(length), sha)
                require(len(positions) == int(replicas), 'UNAVAILABLE', 'replica locations unavailable')
                return dict(chunk_id=cid, length=int(length), sha256=sha, positions=positions), b''
        if cmd in ('manifest', 'locations', 'delete'):
            with transaction() as db:
                db.q('SELECT id FROM ds_allocator WHERE id=1 FOR UPDATE')
                if cmd == 'delete':
                    rows = db.q('SELECT owner,state FROM ds_objects WHERE object_id=? FOR UPDATE', req.get('object_id'))
                    require(rows, 'NOT_FOUND', 'object not found')
                    require(rows[0][0] == owner, 'FORBIDDEN', 'object belongs to another user')
                    if rows[0][1] != 'deleted':
                        upload = db.q('SELECT upload_id FROM ds_uploads WHERE object_id=?', req['object_id'])[0][0]
                        self.retire(db, upload, 'cancelled')
                    return {'state': 'deleted'}, b''
                obj = self.object(db, req.get('object_id'), owner)
                upload = db.q('SELECT upload_id FROM ds_uploads WHERE object_id=?', obj[0])[0][0]
                if cmd == 'manifest':
                    return dict(object_id=obj[0], name=obj[2], size=int(obj[3]), chunk_size=int(obj[4]),
                                chunks=(int(obj[3])+int(obj[4])-1)//int(obj[4])), b''
                seq = req.get('seq')
                require(type(seq) is int and seq >= 0)
                chunks = db.q('SELECT chunk_id,length,sha256 FROM ds_chunks WHERE upload_id=? AND seq=?', upload, seq)
                require(chunks, 'NOT_FOUND', 'chunk absent')
                cid, length, sha = chunks[0]
                positions = db.q("SELECT n.node_id,n.address FROM ds_replicas r JOIN ds_nodes n ON n.node_id=r.node_id WHERE r.chunk_id=? AND r.state='valid' AND r.volume=n.volume AND n.state='healthy' AND n.heartbeat>?", cid, time.time()-self.c.suspect)
                return dict(chunk_id=cid, length=int(length), sha256=sha,
                            positions=[dict(node_id=n, address=a) for n,a in positions]), b''
        if cmd == 'bad_replica':
            with transaction() as db:
                db.q('SELECT id FROM ds_allocator WHERE id=1 FOR UPDATE')
                self.object(db, req.get('object_id'), owner)
                cid, nid = req['chunk_id'], req['node_id']
                require(db.q('SELECT c.chunk_id FROM ds_chunks c JOIN ds_uploads u ON u.upload_id=c.upload_id WHERE c.chunk_id=? AND u.object_id=?', cid, req['object_id']), 'INVALID', 'unrelated chunk')
                db.q("UPDATE ds_replicas SET state='bad' WHERE chunk_id=? AND node_id=? AND state='valid'", cid, nid)
                self.enqueue(db, 'repair', cid)
            return {}, b''
        raise Error('INVALID', 'unknown metadata command')

    def confirm(self, req):
        # Snapshot under short transaction; network verification is outside all DB locks.
        with transaction() as db:
            db.q('SELECT id FROM ds_allocator WHERE id=1 FOR UPDATE')
            row = self.active(db, req['upload_id'], req.get('owner'))
            chunk = db.q('SELECT chunk_id,length,sha256,state FROM ds_chunks WHERE upload_id=? AND seq=?', req['upload_id'], req['seq'])
            require(chunk, 'NOT_FOUND', 'chunk absent')
            cid, length, sha, state = chunk[0]
            require(sha, 'INVALID', 'prepare required')
            positions = self.positions(db, cid, req['upload_id'], int(length), sha)
            require(len(positions) == int(row[6]), 'INCOMPLETE', 'replicas missing')
        for position in positions:
            call(position['address'], dict(cmd='stat', service_key=self.c.secret, chunk_id=cid, length=int(length), sha256=sha))
        require('confirm' not in self.c.faults, 'INJECTED', 'metadata confirmation failure')
        with transaction() as db:
            db.q('SELECT id FROM ds_allocator WHERE id=1 FOR UPDATE')
            self.active(db, req['upload_id'], req.get('owner'))
            current = self.positions(db, cid, req['upload_id'], int(length), sha)
            require(current and [p['authorization']['claims']['instance'] for p in current] == [p['authorization']['claims']['instance'] for p in positions], 'STALE', 'node instance changed')
            for position in positions:
                nid = position['node_id']
                replica = db.q('SELECT state FROM ds_replicas WHERE chunk_id=? AND node_id=? FOR UPDATE', cid, nid)[0][0]
                if replica == 'reserved':
                    db.q("UPDATE ds_replicas SET state='valid' WHERE chunk_id=? AND node_id=?", cid, nid)
                    db.q('UPDATE ds_nodes SET reserved=reserved-?,used_bytes=used_bytes+? WHERE node_id=?', int(length), int(length), nid)
            db.q("UPDATE ds_chunks SET state='confirmed' WHERE chunk_id=?", cid)
            progress = int(db.q("SELECT COALESCE(SUM(length),0) FROM ds_chunks WHERE upload_id=? AND state='confirmed'", req['upload_id'])[0][0])
        return {'confirmed_bytes': progress}

    @staticmethod
    def enqueue(db, kind, cid, nid=''):
        # Completed cleanup can be reopened by a later inventory; repairing tasks deduplicate.
        db.q("INSERT INTO ds_tasks(kind,chunk_id,node_id) VALUES(?,?,?) ON DUPLICATE KEY UPDATE state=IF(state='done','pending',state),lease=IF(state='done',0,lease)", kind, cid, nid)

    def retire(self, db, upload, state):
        db.q('UPDATE ds_uploads SET state=? WHERE upload_id=?', state, upload)
        db.q("UPDATE ds_objects SET state='deleted' WHERE object_id=(SELECT object_id FROM ds_uploads WHERE upload_id=?)", upload)
        rows = db.q('SELECT r.chunk_id,r.node_id,r.state,c.length FROM ds_replicas r JOIN ds_chunks c ON c.chunk_id=r.chunk_id WHERE c.upload_id=? FOR UPDATE', upload)
        for cid,nid,rstate,length in rows:
            if rstate == 'reserved':
                db.q('UPDATE ds_nodes SET reserved=reserved-? WHERE node_id=?', int(length), nid)
            # Used accounting stays until deletion ACK (bad replicas also occupy space).
            if rstate not in ('deleting','gone'):
                db.q("UPDATE ds_replicas SET state='deleting' WHERE chunk_id=? AND node_id=?", cid,nid)
            self.enqueue(db, 'delete', cid,nid)
        # A cleanup grace exceeds any write authorization and bounded request duration.
        db.q("UPDATE ds_tasks t JOIN ds_chunks c ON c.chunk_id=t.chunk_id SET t.lease=GREATEST(t.lease,?) WHERE c.upload_id=? AND t.kind='delete'", time.time()+self.c.auth_ttl+20, upload)

    def heartbeat(self, req):
        nid = req.get('node_id')
        require(nid in self.c.nodes, 'INVALID', 'unconfigured node')
        instance, volume = req.get('instance'), req.get('volume')
        require(all(isinstance(x, str) and re.fullmatch('[a-f0-9]{32}', x) for x in (instance,volume)))
        capacity, free = req.get('capacity'), req.get('free_bytes')
        require(type(capacity) is int and type(free) is int and 0 <= free <= capacity <= 2**60)
        now = time.time()
        with transaction() as db:
            db.q('SELECT id FROM ds_allocator WHERE id=1 FOR UPDATE')
            rows = db.q('SELECT instance,volume,heartbeat FROM ds_nodes WHERE node_id=? FOR UPDATE', nid)
            if rows and rows[0][0] != instance:
                require(not db.q('SELECT instance FROM ds_instances WHERE node_id=? AND instance=?', nid, instance), 'STALE', 'retired instance')
                require(now-float(rows[0][2]) >= self.c.dead, 'DUPLICATE', 'node already has live instance')
                db.q('INSERT IGNORE INTO ds_instances VALUES(?,?)', nid, rows[0][0])
            changed = bool(rows and (rows[0][0] != instance or rows[0][1] != volume))
            db.q("INSERT INTO ds_nodes(node_id,address,instance,volume,heartbeat,capacity,free_bytes,state) VALUES(?,?,?,?,?,?,?,'healthy') ON DUPLICATE KEY UPDATE address=VALUES(address),instance=VALUES(instance),volume=VALUES(volume),heartbeat=VALUES(heartbeat),capacity=VALUES(capacity),free_bytes=VALUES(free_bytes),state='healthy'", nid,self.c.nodes[nid],instance,volume,now,capacity,free)
            if changed:
                # Never trust node_id alone: every old valid copy requires physical verification.
                db.q("UPDATE ds_replicas SET state='unverified' WHERE node_id=? AND state IN ('valid','bad')", nid)
        return {'instance': instance}

    def run_once(self):
        """Claim one durable task. Network work is outside the claim transaction."""
        with transaction() as db:
            db.q('SELECT id FROM ds_allocator WHERE id=1 FOR UPDATE')
            rows = db.q("SELECT task_id,kind,chunk_id,node_id FROM ds_tasks WHERE (state='pending' OR (state='running' AND lease<?)) AND (lease=0 OR lease<?) ORDER BY task_id LIMIT 1 FOR UPDATE", time.time(), time.time())
            if not rows:
                return False
            task, kind, cid, nid = rows[0]
            token = ident()
            db.q("UPDATE ds_tasks SET state='running',lease=?,token=?,attempts=attempts+1 WHERE task_id=? AND (state='pending' OR lease<?)", time.time()+self.c.auth_ttl+20, token, task, time.time())
            row = db.q('SELECT c.length,c.sha256,u.upload_id,o.object_id,o.state FROM ds_chunks c JOIN ds_uploads u ON u.upload_id=c.upload_id JOIN ds_objects o ON o.object_id=u.object_id WHERE c.chunk_id=?', cid)
            require(row, 'NOT_FOUND', 'task chunk absent')
            length, sha, upload, oid, object_state = row[0]
            replicas = db.q("SELECT r.node_id,n.address,n.volume,n.instance,r.state FROM ds_replicas r JOIN ds_nodes n ON n.node_id=r.node_id WHERE r.chunk_id=? AND r.volume=n.volume", cid)
        try:
            if kind == 'delete':
                # Deletion is idempotent and only changes metadata after the node ACK.
                for node, address, volume, instance, state in replicas:
                    if node != nid: continue
                    call(address, dict(cmd='remove', service_key=self.c.secret, chunk_id=cid))
                with transaction() as db:
                    db.q("UPDATE ds_replicas SET state='gone' WHERE chunk_id=? AND node_id=? AND state='deleting'", cid,nid)
                    db.q("UPDATE ds_nodes n JOIN ds_chunks c ON c.chunk_id=? SET n.used_bytes=GREATEST(0,n.used_bytes-c.length) WHERE n.node_id=?", cid,nid)
                    db.q("UPDATE ds_tasks SET state='done',lease=0 WHERE task_id=? AND token=?", task,token)
                return True
            # Repair: select a valid source, then reserve a distinct healthy target.
            source = next((r for r in replicas if r[4]=='valid'), None)
            require(source, 'DATA_UNAVAILABLE', 'no healthy source replica')
            with transaction() as db:
                db.q('SELECT id FROM ds_allocator WHERE id=1 FOR UPDATE')
                require(db.q("SELECT state FROM ds_objects WHERE object_id=?", oid)[0][0] == 'published', 'DELETED', 'object no longer published')
                targets = self.reserve(db, cid, int(length), 1, [r[0] for r in replicas])
                target = targets[0]
                trow = db.q('SELECT address,volume,instance FROM ds_nodes WHERE node_id=?', target)[0]
            _, binary = call(source[1], dict(cmd='get', service_key=self.c.secret, chunk_id=cid, length=int(length), sha256=sha))
            claims = ticket(self.c.secret, dict(upload_id=upload,chunk_id=cid,node_id=target,volume=trow[1],instance=trow[2],length=int(length),sha256=sha,expires=time.time()+self.c.auth_ttl))
            call(trow[0], dict(cmd='put',service_key=self.c.secret,chunk_id=cid,authorization=claims), binary)
            call(trow[0], dict(cmd='stat',service_key=self.c.secret,chunk_id=cid,length=int(length),sha256=sha))
            with transaction() as db:
                db.q("UPDATE ds_replicas SET state='valid' WHERE chunk_id=? AND node_id=? AND state='reserved'", cid,target)
                db.q('UPDATE ds_nodes SET reserved=reserved-?,used_bytes=used_bytes+? WHERE node_id=?', int(length),int(length),target)
                db.q("UPDATE ds_tasks SET state='done',lease=0 WHERE task_id=? AND token=?", task,token)
            return True
        except Exception as exc:
            with transaction() as db:
                db.q("UPDATE ds_tasks SET state='pending',lease=0,reason=? WHERE task_id=? AND token=?", str(exc)[:255],task,token)
            return False
