#!/bin/sh
set -eu

PORT=${QUEEN_PORT:-4400}
SEED=${QUEEN_SEED:-20260811}
SPARKS=${QUEEN_SPARKS:-8}
DATA=${QUEEN_DATA:-/data}

mkdir -p "$DATA/fdb" /var/log/foundationdb /etc/foundationdb

[ -f "$DATA/fdb.cluster" ] || echo 'queen:queen@127.0.0.1:4500' > "$DATA/fdb.cluster"
ln -sf "$DATA/fdb.cluster" /etc/foundationdb/fdb.cluster

fdbserver -C /etc/foundationdb/fdb.cluster -p 127.0.0.1:4500 \
  --datadir "$DATA/fdb" --logdir /var/log/foundationdb \
  --class unset --memory 1500MiB --cache-memory 256MiB \
  >/var/log/foundationdb/fdbserver.stdout 2>&1 &

sleep 2
# Fails on every boot after the first, which is correct: the pages are on the volume.
fdbcli --exec 'configure new single ssd' >/dev/null 2>&1 || true

i=0
while [ "$i" -lt 60 ]; do
	fdbcli --exec 'status minimal' 2>/dev/null | grep -q 'The database is available' && break
	i=$((i + 1))
	sleep 2
done
fdbcli --exec 'status minimal'

exec queen serve "$PORT" "$SEED" "$SPARKS"
