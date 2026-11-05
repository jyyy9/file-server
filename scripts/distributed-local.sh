#!/bin/sh
set -eu
ROOT=${DS_LOCAL_ROOT:-.distributed-local}
mkdir -p "$ROOT"/node1 "$ROOT"/node2 "$ROOT"/node3
: "${DS_SERVICE_KEY:?set DS_SERVICE_KEY (24+ characters)}"
: "${DS_USERS:?set DS_USERS JSON}"
export DS_SERVICE_KEY DS_USERS
export FS_DB_HOST=${FS_DB_HOST:-127.0.0.1} FS_DB_NAME=${FS_DB_NAME:-fileserver}
python3 -m distributed.migrate
pids=""
cleanup() { kill $pids 2>/dev/null || true; }
trap cleanup INT TERM EXIT
python3 -m distributed.run node --node-id n1 --root "$ROOT/node1" & pids="$pids $!"
python3 -m distributed.run node --node-id n2 --root "$ROOT/node2" & pids="$pids $!"
python3 -m distributed.run node --node-id n3 --root "$ROOT/node3" & pids="$pids $!"
python3 -m distributed.run file & pids="$pids $!"
python3 -m distributed.run gateway & pids="$pids $!"
wait
