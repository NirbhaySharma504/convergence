#!/usr/bin/env bash
# partition_demo.sh — the interview demo: partition -> diverge -> heal -> converge.
#
# Uses an in-app `admin partition on|off` command instead of iptables, so it
# needs no root and runs deterministically on any machine.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CONV=./convergence
CLI=./convergence-cli
WAL0=/tmp/conv_demo_node0.wal
WAL1=/tmp/conv_demo_node1.wal

cleanup() {
    kill "${N0:-0}" "${N1:-0}" 2>/dev/null
    rm -f "$WAL0" "$WAL1"
}
trap cleanup EXIT

rm -f "$WAL0" "$WAL1"

echo "=== Building ==="
make --no-print-directory >/dev/null

echo "=== Starting Node 0 (port 7000) and Node 1 (port 7001) ==="
$CONV --id 0 --port 7000 --peer 127.0.0.1:7001 --nodes 2 --wal "$WAL0" >/dev/null 2>&1 &
N0=$!
$CONV --id 1 --port 7001 --peer 127.0.0.1:7000 --nodes 2 --wal "$WAL1" >/dev/null 2>&1 &
N1=$!
sleep 2
echo "    Nodes connected and gossiping."
echo

echo "=== Simulating a network partition (isolating Node 0) ==="
$CLI --port 7000 admin partition on
echo

echo "=== Writing during the partition ==="
$CLI --port 7000 gctr inc visits >/dev/null   # +2 on node 0
$CLI --port 7000 gctr inc visits >/dev/null
$CLI --port 7001 gctr inc visits >/dev/null   # +1 on node 1
sleep 3
echo "    Node 0 sees visits = $($CLI --port 7000 gctr get visits)"
echo "    Node 1 sees visits = $($CLI --port 7001 gctr get visits)"
echo "    ^ They DISAGREE. The partition is real."
echo

echo "=== Healing the partition ==="
$CLI --port 7000 admin partition off
echo "    Waiting one gossip cycle..."
sleep 3
echo "    Node 0 sees visits = $($CLI --port 7000 gctr get visits)"
echo "    Node 1 sees visits = $($CLI --port 7001 gctr get visits)"
echo "    ^ Both show 3. Convergence achieved — no coordinator, no conflict."
echo

V0=$($CLI --port 7000 gctr get visits)
V1=$($CLI --port 7001 gctr get visits)
if [ "$V0" = "3" ] && [ "$V1" = "3" ]; then
    echo "=== DEMO PASSED: both nodes converged to 3 ==="
    exit 0
else
    echo "=== DEMO FAILED: node0=$V0 node1=$V1 ==="
    exit 1
fi
