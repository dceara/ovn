#!/bin/bash

dest="$1"

# Check if the user provided an argument
if [ -z "$dest" ]; then
    echo "Error: No destination directory provided." >&2
    echo "Usage: $0 <destination_directory>" >&2
    exit 1
fi

# Check if the directory already exists
if [ -d "$dest" ]; then
    echo "Error: Directory '$dest' already exists." >&2
    exit 1
fi

mkdir $dest

# Base network (as fast as possible).
ovsdb-replay --db /tmp/collected-dbs/ovnnb_db.db,$OVN_NB_DB --db /tmp/collected-dbs/conf.db,unix:db.sock --speed 0 --verbose  --stop-txn 94900

# Wait for everything to be up:
time until [ "$(ovn-sbctl show | grep Port_Binding -c)" -eq 6014 ]; do sleep 5; done
ovn-nbctl --wait=hv sync
sleep 30

(
    for i in $(seq 100); do
        perf record -o $dest/perf.data.$i -g -p $(pidof ovs-vswitchd) sleep 60
    done
) &
PERF_LOOP_PID=$!

cleanup() {
    kill -TERM $PERF_LOOP_PID 2>/dev/null
    pkill -INT -P $PERF_LOOP_PID 2>/dev/null
}

trap cleanup EXIT INT TERM

# UDN pods.
ovsdb-replay --db /tmp/collected-dbs/ovnnb_db.db,$OVN_NB_DB --db /tmp/collected-dbs/conf.db,unix:db.sock --speed 2.0 --verbose  --start-txn 94901 --stop-txn 121200

# Wait for everything to be up:
time until [ "$(ovn-sbctl show | grep Port_Binding -c)" -eq 6350 ]; do sleep 5; done
sleep 30

# Get times:
./ovn-lsp-install-time.sh --log sandbox/ovn-controller.log > $dest/lsp-times.txt

touch $dest/config
echo "OVN: " > $dest/config
git rev-parse HEAD >> $dest/config
echo "OVS: " > $dest/config
(cd ../ovs/ && git rev-parse HEAD >> $dest/config)

cp sandbox/ovn-controller.log $dest/
cp sandbox/ovn-northd.log $dest/
cp sandbox/nb1.log $dest/
cp sandbox/sb1.log $dest/
cp sandbox/ovs-vswitchd.log $dest/

killall -9 perf
