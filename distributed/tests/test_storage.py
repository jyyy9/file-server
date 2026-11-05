import concurrent.futures
import socket
import tempfile
import time
import unittest
import uuid
from distributed.storage import Storage, digest, ticket
from distributed.wire import Error, HEADER, receive

class StorageTest(unittest.TestCase):
    def test_immutable_concurrent_and_restart(self):
        with tempfile.TemporaryDirectory() as root:
            store = Storage(root, 'n1', 'secret')
            data = b'hello\x00world'
            cid = uuid.uuid4().hex
            auth = ticket('secret', dict(upload_id=uuid.uuid4().hex, chunk_id=cid, node_id='n1',
                        volume=store.volume, instance=store.instance, length=len(data), sha256=digest(data), expires=time.time()+60))
            req = dict(cmd='put', chunk_id=cid, authorization=auth, service_key='secret')
            with concurrent.futures.ThreadPoolExecutor(8) as pool:
                list(pool.map(lambda _: store.dispatch(req, data), range(16)))
            self.assertEqual((store.root / cid).read_bytes(), data)
            with self.assertRaises(Error):
                store.dispatch(req, b'wrong')
            with self.assertRaises(Error):
                store.dispatch(dict(req, chunk_id='../escape'), data)
            with self.assertRaises(Error):
                store.dispatch(dict(req, service_key='bad'), data)
            volume = store.volume
            store.lockfile.close()
            again = Storage(root, 'n1', 'secret')
            self.assertEqual(volume, again.volume)
            self.assertNotEqual(store.instance, again.instance)
            again.lockfile.close()

    def test_sync_failure_not_published(self):
        with tempfile.TemporaryDirectory() as root:
            store = Storage(root, 'n1', 'secret', faults={'sync'})
            cid = uuid.uuid4().hex
            claims = dict(upload_id=uuid.uuid4().hex, chunk_id=cid, node_id='n1', volume=store.volume,
                          instance=store.instance, length=1, sha256=digest(b'a'), expires=time.time()+60)
            with self.assertRaises(OSError):
                store.dispatch(dict(cmd='put', chunk_id=cid, service_key='secret', authorization=ticket('secret', claims)), b'a')
            self.assertFalse((store.root/cid).exists())
            store.lockfile.close()

    def test_length_bound(self):
        a, b = socket.socketpair()
        try:
            a.sendall(HEADER.pack(0x4D46, 1, 1, 0xffffffff, 0xffffffff, 1, 0))
            with self.assertRaises(Error):
                receive(b)
        finally:
            a.close()
            b.close()
