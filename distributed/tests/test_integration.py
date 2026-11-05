"""Real subprocess services + real MySQL. DS_TEST_DATABASE must be an isolated database."""
import concurrent.futures
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from distributed.client import Client
from distributed.db import transaction
from distributed.storage import digest
from distributed.wire import call, Error

TOKEN='test-user-token-at-least-24-chars'
SECRET='test-service-key-at-least-24-chars'

class Cluster(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        name=os.getenv('DS_TEST_DATABASE','')
        if not name.startswith('ds_test_'):
            raise RuntimeError('DS_TEST_DATABASE must name a dedicated ds_test_* database (no existing tables)')
        os.environ['FS_DB_NAME']=name
        with transaction() as db:
            if db.q('SHOW TABLES'):
                raise RuntimeError('test database must be empty; refusing to touch existing tables')
        cls.tmp=tempfile.TemporaryDirectory(prefix='ds-test-')
        cls.root=Path(cls.tmp.name)
        sockets=[]
        ports=[]
        for _ in range(5):
            sock=socket.socket();sock.bind(('127.0.0.1',0));sockets.append(sock);ports.append(sock.getsockname()[1])
        for sock in sockets:sock.close()
        cls.nodes={f'n{i+1}':f'127.0.0.1:{ports[i]}' for i in range(3)}
        cls.file=f'127.0.0.1:{ports[3]}'
        cls.gateway=f'127.0.0.1:{ports[4]}'
        cls.env=dict(os.environ,DS_SERVICE_KEY=SECRET,DS_USERS=json.dumps({'alice':TOKEN,'bob':TOKEN+'b'}),
                     DS_NODES=json.dumps(cls.nodes),DS_FILE_ADDRESS=cls.file,DS_GATEWAY_ADDRESS=cls.gateway,
                     DS_CHUNK_SIZE='4096',DS_CAPACITY='1048576',DS_AUTH_TTL='2',DS_SUSPECT_SECONDS='3',DS_DEAD_SECONDS='5')
        subprocess.run([sys.executable,'-m','distributed.migrate'],env=cls.env,check=True)
        cls.processes={};cls.logs={}
        cls.start('file');cls.start('gateway')
        for nid in cls.nodes:cls.start(nid)
        cls.client=Client(cls.gateway,'alice',TOKEN)
        cls.wait_healthy()

    @classmethod
    def start(cls,role,faults=''):
        args=[sys.executable,'-m','distributed.run']
        if role.startswith('n'):
            args+=['node','--node-id',role,'--root',str(cls.root/role)]
        else:args+=[role]
        log=open(cls.root/(role+'.log'),'ab');cls.logs[role]=log
        cls.processes[role]=subprocess.Popen(args,env=dict(cls.env,DS_FAULTS=faults),stdout=log,stderr=log)
        address=cls.nodes.get(role,cls.file if role=='file' else cls.gateway)
        deadline=time.time()+10
        while time.time()<deadline:
            try:
                with socket.create_connection((address.split(':')[0],int(address.split(':')[1])),timeout=.2):return
            except OSError:time.sleep(.05)
        raise RuntimeError('service startup failed: '+role+' '+(cls.root/(role+'.log')).read_text())

    @classmethod
    def stop(cls,role):
        p=cls.processes.pop(role,None)
        if p:
            p.terminate()
            try:p.wait(5)
            except subprocess.TimeoutExpired:p.kill();p.wait()
            cls.logs.pop(role).close()

    @classmethod
    def wait_healthy(cls):
        deadline=time.time()+15
        while time.time()<deadline:
            with transaction() as db:
                count=db.q("SELECT COUNT(*) FROM ds_nodes WHERE state='healthy' AND heartbeat>?",time.time()-3)[0][0]
            if int(count)==3:
                matches=True
                with transaction() as db:
                    identities=dict(db.q('SELECT node_id,instance FROM ds_nodes'))
                for nid in cls.nodes:
                    instance_file=cls.root/nid/'.instance'
                    if not instance_file.exists() or identities.get(nid)!=instance_file.read_text(): matches=False
                if matches:return
            time.sleep(.2)
        raise RuntimeError('nodes failed to register')

    @classmethod
    def tearDownClass(cls):
        for role in list(cls.processes):cls.stop(role)
        # Database is intentionally retained for diagnosis. No DROP DATABASE in this test.
        cls.tmp.cleanup()

    def init(self,data):
        return self.client.request('init',name='test.bin',size=len(data))[0]

    def upload_chunk(self,u,seq,data):
        return self.client.request('upload',data,upload_id=u['upload_id'],seq=seq,sha256=digest(data))[0]

    def roundtrip(self,data):
        path=self.root/'source';path.write_bytes(data)
        result=self.client.upload(path)
        out=self.root/'result';self.client.download(result['object_id'],out)
        self.assertEqual(digest(out.read_bytes()),digest(data))
        return result['object_id']

    def test_01_boundaries(self):
        for length in (0,1,4096,8192,8193):
            with self.subTest(length=length):self.roundtrip(os.urandom(length))

    def test_02_parallel_idempotency_conflict(self):
        data=os.urandom(12289);u=self.init(data)
        pieces=[data[i:i+4096] for i in range(0,len(data),4096)]
        with concurrent.futures.ThreadPoolExecutor(8) as pool:
            list(pool.map(lambda pair:self.upload_chunk(u,*pair),list(enumerate(pieces))*2))
        status,_=self.client.request('status',upload_id=u['upload_id'])
        self.assertEqual(status['confirmed_bytes'],len(data));self.assertEqual(status['missing'],[])
        with self.assertRaises(Error) as err:self.upload_chunk(u,0,b'X'*4096)
        self.assertEqual(err.exception.code,'CONFLICT')
        self.client.request('complete',upload_id=u['upload_id'])
        self.client.request('complete',upload_id=u['upload_id'])

    def test_03_partial_failure_and_resume(self):
        data=os.urandom(8193);u=self.init(data)
        self.upload_chunk(u,1,data[4096:8192])
        self.stop('file');self.start('file')
        status,_=self.client.request('status',upload_id=u['upload_id'])
        self.assertEqual(status['missing'],[0,2]);self.assertEqual(status['confirmed_bytes'],4096)
        path=self.root/'resume';path.write_bytes(data)
        self.client.upload(path,u['upload_id'])

    def test_04_metadata_and_response_failure(self):
        data=os.urandom(10);u=self.init(data)
        self.stop('file');self.start('file','confirm')
        with self.assertRaises(Error):self.upload_chunk(u,0,data)
        status,_=self.client.request('status',upload_id=u['upload_id'])
        self.assertEqual(status['confirmed_bytes'],0)
        self.stop('file');self.start('file')
        self.stop('gateway');self.start('gateway','response')
        with self.assertRaises(Error):self.upload_chunk(u,0,data)
        status,_=self.client.request('status',upload_id=u['upload_id'])
        self.assertEqual(status['confirmed_bytes'],len(data))
        self.stop('gateway');self.start('gateway')
        self.upload_chunk(u,0,data)
        self.assertEqual(self.client.request('status',upload_id=u['upload_id'])[0]['confirmed_bytes'],len(data))

    def test_05_auth_delete_cancel(self):
        u=self.init(b'hello')
        bob=Client(self.gateway,'bob',TOKEN+'b')
        with self.assertRaises(Error):bob.request('status',upload_id=u['upload_id'])
        with self.assertRaises(Error):call(self.file,dict(cmd='heartbeat'))
        self.client.request('cancel',upload_id=u['upload_id'])
        self.client.request('cancel',upload_id=u['upload_id'])
        with self.assertRaises(Error):self.upload_chunk(u,0,b'hello')
        oid=self.roundtrip(b'hello')
        self.client.request('delete',object_id=oid);self.client.request('delete',object_id=oid)
        with self.assertRaises(Error):self.client.request('manifest',object_id=oid)

    def test_06_node_failure_repair_and_recovery(self):
        oid=self.roundtrip(b'failure-test')
        with transaction() as db:
            cid=db.q('SELECT c.chunk_id FROM ds_chunks c JOIN ds_uploads u ON u.upload_id=c.upload_id WHERE u.object_id=?',oid)[0][0]
            old=db.q("SELECT node_id FROM ds_replicas WHERE chunk_id=? AND state='valid'",cid)[0][0]
        self.stop(old)
        try:
            out=self.root/'failover';self.client.download(oid,out)
            self.assertEqual(out.read_bytes(),b'failure-test')
            deadline=time.time()+15
            while time.time()<deadline:
                with transaction() as db:
                    n=db.q("SELECT COUNT(*) FROM ds_replicas r JOIN ds_nodes n ON n.node_id=r.node_id WHERE r.chunk_id=? AND r.state='valid' AND n.state='healthy' AND n.heartbeat>?",cid,time.time()-3)[0][0]
                if int(n)>=2:break
                time.sleep(.3)
            self.assertEqual(int(n),2)
        finally:
            self.start(old);self.wait_healthy()

    def test_07_corruption_and_unavailable(self):
        oid=self.roundtrip(b'corrupt-me')
        with transaction() as db:
            cid=db.q('SELECT c.chunk_id FROM ds_chunks c JOIN ds_uploads u ON u.upload_id=c.upload_id WHERE u.object_id=?',oid)[0][0]
            copies=db.q("SELECT node_id FROM ds_replicas WHERE chunk_id=? AND state='valid' ORDER BY node_id",cid)
        (self.root/copies[0][0]/cid).write_bytes(b'broken')
        out=self.root/'fallback';self.client.download(oid,out);self.assertEqual(out.read_bytes(),b'corrupt-me')
        # Stop maintenance so the all-corrupt condition is deterministic.
        self.stop('file');self.start('file','no_worker')
        for nid in self.nodes:
            p=self.root/nid/cid
            if p.exists():p.write_bytes(b'broken')
        with self.assertRaises(Error) as err:self.client.download(oid,self.root/'must-not-exist')
        self.assertEqual(err.exception.code,'DATA_UNAVAILABLE')
        self.assertFalse((self.root/'must-not-exist').exists())
        self.stop('file');self.start('file')

    def test_08_capacity_cancel_expire(self):
        with transaction() as db:
            before=db.q('SELECT SUM(reserved) FROM ds_nodes')[0][0]
        u=self.init(b'x'*8192)
        self.client.request('cancel',upload_id=u['upload_id'])
        with transaction() as db:
            self.assertEqual(db.q('SELECT SUM(reserved) FROM ds_nodes')[0][0],before)
        u=self.init(b'x')
        with transaction() as db:
            db.q('UPDATE ds_uploads SET expires=? WHERE upload_id=?',time.time()-1,u['upload_id'])
        with self.assertRaises(Error):self.upload_chunk(u,0,b'x')
        deadline=time.time()+5
        while time.time()<deadline:
            with transaction() as db:
                state=db.q('SELECT state FROM ds_uploads WHERE upload_id=?',u['upload_id'])[0][0]
            if state=='expired':break
            time.sleep(.2)
        self.assertEqual(state,'expired')

    def test_09_duplicate_instance_and_length(self):
        import uuid
        from distributed.wire import HEADER,receive
        with transaction() as db:
            nid,volume=db.q('SELECT node_id,volume FROM ds_nodes LIMIT 1')[0]
        with self.assertRaises(Error) as err:
            call(self.file,dict(cmd='heartbeat',service_key=SECRET,node_id=nid,instance=uuid.uuid4().hex,volume=volume,capacity=1048576,free_bytes=1048576))
        self.assertEqual(err.exception.code,'DUPLICATE')
        with socket.create_connection(('127.0.0.1',int(self.gateway.split(':')[1]))) as sock:
            sock.sendall(HEADER.pack(0x4D46,1,1,0xffffffff,0xffffffff,1,0))
            result,_,_=receive(sock)
        self.assertEqual(result['code'],'LIMIT')

    def test_10_delete_task_restart(self):
        u=self.init(b'task-restart');self.upload_chunk(u,0,b'task-restart')
        self.client.request('delete',object_id=u['object_id'])
        self.stop('file')
        with transaction() as db:
            db.q("UPDATE ds_tasks t JOIN ds_chunks c ON c.chunk_id=t.chunk_id SET t.state='running',t.lease=?,t.token='abandoned' WHERE c.upload_id=? AND t.kind='delete'",time.time()-1,u['upload_id'])
        self.start('file')
        deadline=time.time()+6
        while time.time()<deadline:
            with transaction() as db:
                pending=db.q("SELECT COUNT(*) FROM ds_tasks t JOIN ds_chunks c ON c.chunk_id=t.chunk_id WHERE c.upload_id=? AND t.kind='delete' AND t.state<>'done'",u['upload_id'])[0][0]
            if pending=='0':break
            time.sleep(.2)
        self.assertEqual(pending,'0')
        with self.assertRaises(Error):self.client.request('complete',upload_id=u['upload_id'])

if __name__=='__main__':unittest.main(verbosity=2)
