#!/usr/bin/env bash
# Integration test: lab + tunnel + ping + probes + fuzz + clean shutdown.
# Usage: sudo scripts/run_tests.sh            (normal build)
#        sudo BIN=build/gtpu_asan scripts/run_tests.sh   (sanitizer build)
set -uo pipefail
[ "$EUID" -eq 0 ] || { echo "run as root: sudo $0"; exit 1; }

cd "$(dirname "$0")/.."
BIN=${BIN:-build/gtpu}
CONF=configs/sessions.conf
LOGDIR=$(mktemp -d)
pass=0; fail=0

ok()  { echo "PASS: $1"; pass=$((pass + 1)); }
bad() { echo "FAIL: $1"; fail=$((fail + 1)); }
check() {   # description expected actual
    if [ "$2" = "$3" ]; then ok "$1 (=$3)"; else bad "$1 (expected $2, got $3)"; fi
}
snap() { kill -USR1 "$1"; sleep 0.3; }                       # ask for stats
val()  { grep -o "\b$2=[0-9]*" "$1" | tail -1 | cut -d= -f2; }  # last value of key

cleanup() {
    kill "${UPF_PID:-}" "${GNB_PID:-}" 2>/dev/null
    wait 2>/dev/null
    scripts/teardown_ns.sh >/dev/null 2>&1
}
trap cleanup EXIT

make -s all || { echo "build failed"; exit 1; }
[ -x "$BIN" ] || { echo "$BIN not built (try: make $BIN)"; exit 1; }
./build/test_gtpu >/dev/null && ok "unit tests" || bad "unit tests"

scripts/teardown_ns.sh >/dev/null 2>&1
scripts/setup_ns.sh >/dev/null || { echo "lab setup failed"; exit 1; }

ip netns exec ns-upf "$BIN" -m upf -t tun0 -l 192.168.10.2 -p 192.168.10.1 \
    -c $CONF 2>"$LOGDIR/upf.log" &
UPF_PID=$!
ip netns exec ns-gnb "$BIN" -m gnb -t tun0 -l 192.168.10.1 -p 192.168.10.2 \
    -c $CONF 2>"$LOGDIR/gnb.log" &
GNB_PID=$!
sleep 1
kill -0 "$UPF_PID" 2>/dev/null && kill -0 "$GNB_PID" 2>/dev/null \
    && ok "both endpoints started" \
    || { bad "endpoint failed to start"; cat "$LOGDIR"/*.log; exit 1; }

# --- 1. traffic through the tunnel ---------------------------------------
ip netns exec ns-ue ping -q -c 10 -i 0.2 10.70.0.2 >/dev/null \
    && ok "ping 10 packets, 0 loss" || bad "ping 10 packets"
# 1436 + 8 ICMP + 20 IP = 1464 = tun MTU; encapsulated = 1500 = veth MTU
ip netns exec ns-ue ping -q -c 3 -s 1436 10.70.0.2 >/dev/null \
    && ok "ping at MTU boundary (1464-byte inner packet)" \
    || bad "ping at MTU boundary"

# --- 2. counters must match exactly, no drops ----------------------------
snap "$UPF_PID"; snap "$GNB_PID"
for side in upf gnb; do
    check "$side tx" 13 "$(val "$LOGDIR/$side.log" tx)"
    check "$side rx" 13 "$(val "$LOGDIR/$side.log" rx)"
done
for k in no_session unknown_teid malformed non_ipv4 unsupported spoofed; do
    check "upf drop $k" 0 "$(val "$LOGDIR/upf.log" $k)"
done

# --- 3. protocol probes (sent from the gNB namespace) --------------------
ip netns exec ns-gnb python3 - <<'EOF'
import socket, struct
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
addr = ("192.168.10.2", 2152)
def inner(src):
    return bytes([0x45,0,0,20, 0,0,0,0, 64,1,0,0]) + bytes(src) + bytes([10,70,0,2])
def gpdu(teid, payload):
    return struct.pack("!BBHI", 0x30, 0xFF, len(payload), teid) + payload
s.sendto(gpdu(999, inner([10,60,0,2])), addr)   # unknown TEID
s.sendto(gpdu(100, inner([10,60,0,99])), addr)  # spoofed inner source
EOF
snap "$UPF_PID"
check "unknown TEID counted" 1 "$(val "$LOGDIR/upf.log" unknown_teid)"
check "spoofed source counted" 1 "$(val "$LOGDIR/upf.log" spoofed)"

echo_type=$(ip netns exec ns-gnb python3 - <<'EOF'
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(2)
s.sendto(bytes.fromhex("320100040000000000010000"), ("192.168.10.2", 2152))
print(s.recv(64)[1])
EOF
)
check "Echo Request answered with Echo Response (type)" 2 "$echo_type"

# --- 4. fuzzing -----------------------------------------------------------
ip netns exec ns-gnb python3 tests/fuzz_gtpu.py --count 5000 --seed 1
sleep 1
kill -0 "$UPF_PID" 2>/dev/null && ok "UPF alive after fuzzing" \
    || bad "UPF died during fuzzing"
snap "$UPF_PID"
m=$(val "$LOGDIR/upf.log" malformed)
[ "${m:-0}" -gt 0 ] && ok "fuzz packets rejected as malformed (malformed=$m)" \
    || bad "no malformed drops recorded"
ip netns exec ns-ue ping -q -c 3 10.70.0.2 >/dev/null \
    && ok "tunnel still forwards after fuzzing" || bad "tunnel broken after fuzzing"

# --- 5. clean shutdown (a sanitizer build fails here on leaks/UB) --------
kill -INT "$GNB_PID"; wait "$GNB_PID"; check "gnb exit code on SIGINT" 0 $?
kill -INT "$UPF_PID"; wait "$UPF_PID"; check "upf exit code on SIGINT" 0 $?
UPF_PID=""; GNB_PID=""
grep -q "shutting down" "$LOGDIR/gnb.log" && ok "gnb logged clean shutdown" \
    || bad "gnb shutdown log missing"

echo "----"
echo "passed=$pass failed=$fail   (logs: $LOGDIR)"
[ "$fail" -eq 0 ]