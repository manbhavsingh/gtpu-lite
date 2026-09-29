#!/usr/bin/env python3
"""Send malformed/mutated GTP-U datagrams at a target. Reproducible via --seed."""
import argparse
import random
import socket
import struct

INNER = bytes([0x45, 0, 0, 20, 0, 0, 0, 0, 64, 1, 0, 0, 10, 60, 0, 2, 10, 70, 0, 2])


def valid(teid, inner):
    return struct.pack("!BBHI", 0x30, 0xFF, len(inner), teid) + inner


def rand_bytes(rng, n):
    return bytes(rng.randrange(256) for _ in range(n))


def mutate(rng):
    kind = rng.randrange(5)
    base = bytearray(valid(100, INNER))
    if kind == 0:                       # pure random bytes
        return rand_bytes(rng, rng.randrange(0, 200))
    if kind == 1:                       # truncated valid packet
        return bytes(base[:rng.randrange(0, len(base))])
    if kind == 2:                       # flip 1-3 random bytes
        for _ in range(rng.randrange(1, 4)):
            base[rng.randrange(len(base))] = rng.randrange(256)
        return bytes(base)
    if kind == 3:                       # random length field
        base[2:4] = struct.pack("!H", rng.randrange(0x10000))
        return bytes(base)
    base[0] = rng.randrange(256)        # random flags + trailing junk
    return bytes(base) + rand_bytes(rng, rng.randrange(0, 32))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default="192.168.10.2")
    ap.add_argument("--port", type=int, default=2152)
    ap.add_argument("--count", type=int, default=5000)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()

    rng = random.Random(a.seed)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    for _ in range(a.count):
        s.sendto(mutate(rng), (a.target, a.port))
    print(f"sent {a.count} datagrams (seed={a.seed})")


if __name__ == "__main__":
    main()