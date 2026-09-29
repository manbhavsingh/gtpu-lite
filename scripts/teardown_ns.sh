#!/usr/bin/env bash
[ "$EUID" -eq 0 ] || { echo "run as root: sudo $0"; exit 1; }
for ns in ns-ue ns-gnb ns-upf ns-dn; do ip netns del "$ns" 2>/dev/null || true; done
echo "lab removed"