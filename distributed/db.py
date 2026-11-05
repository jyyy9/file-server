"""Small libmysqlclient binding, one connection per short transaction.

Values use hex-encoded UTF-8 SQL literals, never string interpolation of input SQL.
No optional Python packages and no mysql CLI subprocess per query.
"""
import ctypes as C
import ctypes.util
import os
from contextlib import contextmanager

lib = C.CDLL(ctypes.util.find_library('mysqlclient'))
for name, args, result in [
    ('mysql_init', [C.c_void_p], C.c_void_p),
    ('mysql_real_connect', [C.c_void_p, C.c_char_p, C.c_char_p, C.c_char_p, C.c_char_p, C.c_uint, C.c_char_p, C.c_ulong], C.c_void_p),
    ('mysql_query', [C.c_void_p, C.c_char_p], C.c_int),
    ('mysql_store_result', [C.c_void_p], C.c_void_p),
    ('mysql_num_fields', [C.c_void_p], C.c_uint),
    ('mysql_fetch_row', [C.c_void_p], C.POINTER(C.c_char_p)),
    ('mysql_free_result', [C.c_void_p], None),
    ('mysql_close', [C.c_void_p], None),
    ('mysql_error', [C.c_void_p], C.c_char_p),
    ('mysql_options', [C.c_void_p, C.c_int, C.c_void_p], C.c_int),
]:
    fn = getattr(lib, name)
    fn.argtypes, fn.restype = args, result

def literal(value):
    if value is None:
        return 'NULL'
    if isinstance(value, (int, float)):
        return str(value)
    return "CONVERT(X'%s' USING utf8mb4)" % str(value).encode().hex()

class DB:
    def __init__(self):
        self.ptr = lib.mysql_init(None)
        timeout = C.c_uint(5)
        for option in (0, 11, 12):  # connect/read/write timeout
            lib.mysql_options(self.ptr, option, C.byref(timeout))
        values = [os.getenv(k, d).encode() for k, d in (
            ('FS_DB_HOST', '127.0.0.1'), ('FS_DB_USER', 'root'),
            ('FS_DB_PASS', ''), ('FS_DB_NAME', 'fileserver'))]
        if not lib.mysql_real_connect(self.ptr, *values, int(os.getenv('FS_DB_PORT', '3306')),
                                      os.getenv('FS_DB_SOCKET', '').encode() or None, 0):
            message = lib.mysql_error(self.ptr).decode()
            self.close()
            raise RuntimeError(message)
        self.q('SET NAMES utf8mb4')
        self.q('SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED')

    def q(self, sql, *values):
        parts = sql.split('?')
        if len(parts) != len(values) + 1:
            raise ValueError('SQL parameter mismatch')
        sql = ''.join(p + (literal(values[i]) if i < len(values) else '') for i, p in enumerate(parts))
        if lib.mysql_query(self.ptr, sql.encode()):
            raise RuntimeError(lib.mysql_error(self.ptr).decode())
        result = lib.mysql_store_result(self.ptr)
        if not result:
            return []
        try:
            count = lib.mysql_num_fields(result)
            rows = []
            while True:
                row = lib.mysql_fetch_row(result)
                if not row:
                    break
                rows.append(tuple(None if row[i] is None else row[i].decode() for i in range(count)))
            return rows
        finally:
            lib.mysql_free_result(result)

    def close(self):
        if self.ptr:
            lib.mysql_close(self.ptr)
            self.ptr = None

@contextmanager
def transaction():
    db = DB()
    try:
        db.q('START TRANSACTION')
        yield db
        db.q('COMMIT')
    except BaseException:
        db.q('ROLLBACK')
        raise
    finally:
        db.close()
