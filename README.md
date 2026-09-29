# gtpu-lite

Lightweight C/Linux GTP-U user-plane lab built with Linux network namespaces, TUN devices, UDP/2152, TEID-based session mapping, and Echo keepalive/failure detection.

> A focused networking/systems lab — not a complete production 5G Core or radio stack.

## Architecture

~~~mermaid
flowchart LR
    UE["ns-ue<br/>UE<br/>10.60.0.2"]
    GNB["ns-gnb<br/>gNB<br/>10.60.0.1 / 192.168.10.1<br/>tun0"]
    UPF["ns-upf<br/>UPF<br/>192.168.10.2 / 10.70.0.1<br/>tun0"]
    DN["ns-dn<br/>Data Network<br/>10.70.0.2"]

    UE -->|"ue0 ↔ ue0-gnb<br/>10.60.0.0/24"| GNB
    GNB -->|"N3: GTP-U / UDP :2152<br/>n3-gnb ↔ n3-upf"| UPF
    UPF -->|"N6-side link<br/>10.70.0.0/24"| DN
~~~

The lab contains four Linux namespaces: <code>ns-ue</code>, <code>ns-gnb</code>, <code>ns-upf</code>, and <code>ns-dn</code>. The same <code>build/gtpu</code> binary runs as either a gNB or UPF endpoint.

## What it demonstrates

- C11 packet processing on Linux
- Linux network namespaces and veth links
- TUN devices with <code>IFF_TUN | IFF_NO_PI</code>
- UDP transport on GTP-U port <code>2152</code>
- GTP-U G-PDU encapsulation/decapsulation
- TEID-based session lookup
- File-based session configuration
- Separate TUN→UDP and UDP→TUN forwarding workers
- <code>pthread_rwlock_t</code> protected session state
- C11 atomic counters
- Echo Request/Response keepalive
- Peer-down detection after three consecutive missed Echo Responses
- Peer recovery detection
- malformed, unknown-TEID, non-IPv4, unsupported-message, and spoofed-source handling
- logging and graceful signal-driven shutdown

## Packet flow

### Uplink

<code>10.60.0.2 → 10.70.0.2</code>

1. UE traffic enters the gNB TUN device.
2. The gNB finds the session using the inner UE source IP.
3. It selects UL TEID <code>100</code> (<code>0x64</code>).
4. It wraps the packet in a GTP-U G-PDU.
5. It sends UDP to <code>192.168.10.2:2152</code>.
6. The UPF validates the GTP-U datagram, looks up the TEID, validates the inner IPv4 source, decapsulates, and writes the packet to its TUN device.
7. Linux forwards the packet toward the data network.

The reverse path uses DL TEID <code>200</code> (<code>0xc8</code>) and sends the GTP-U datagram to <code>192.168.10.1:2152</code>.

## Addressing

| Namespace | Interface | Address | Role |
|---|---|---|---|
| ns-ue | ue0 | 10.60.0.2/24 | UE side |
| ns-gnb | ue0-gnb | 10.60.0.1/24 | UE ↔ gNB |
| ns-gnb | n3-gnb | 192.168.10.1/24 | N3 underlay |
| ns-gnb | tun0 | MTU 1464 | gNB packet boundary |
| ns-upf | n3-upf | 192.168.10.2/24 | N3 underlay |
| ns-upf | n6-upf | 10.70.0.1/24 | UPF ↔ DN |
| ns-upf | tun0 | MTU 1464 | UPF packet boundary |
| ns-dn | dn0 | 10.70.0.2/24 | Data network |

The setup script enables IPv4 forwarding on gNB/UPF, disables reverse-path filtering there, and disables IPv6 in the four namespaces.

## GTP-U implementation

The codec in <code>src/gtpu.c</code> implements the subset required by this lab.

| Field | Value / behavior |
|---|---|
| Version | GTP version 1 |
| Protocol type | GTP |
| G-PDU flags | 0x30 |
| Echo flags | 0x32 |
| G-PDU type | 255 |
| Echo Request | 1 |
| Echo Response | 2 |
| TEID | 32-bit |
| Mandatory header | 8 bytes |
| Echo TEID | 0 |
| Optional fields | S/E/PN parsing and extension-chain validation |
| UDP port | 2152 |

Current session configuration:

~~~text
session 100 200 10.60.0.2
~~~

The session table is bounded to 64 entries and rejects duplicate TEIDs or UE IPs.

## Keepalive and peer failure detection

Each endpoint independently sends GTP-U Echo Requests.

- interval: 2 seconds
- response timeout: 1 second
- peer-down threshold: 3 consecutive misses

A matching Echo Response resets the miss count. If the peer was down, the matching response records recovery.

## Error handling and observability

Statistics include:

~~~text
tx_pkts / tx_bytes
rx_pkts / rx_bytes

echo_tx
echo_rx
echo_resp_rx
echo_timeout

peer_down
peer_recovered

drop_no_session
drop_unknown_teid
drop_malformed
drop_non_ipv4
drop_unsupported
drop_spoofed

send_err
tun_write_err
~~~

<code>SIGUSR1</code> prints the current counters.

## Build

~~~bash
make clean && make
~~~

Unit tests:

~~~bash
./build/test_gtpu
~~~

Expected:

~~~text
all tests passed
~~~

Automated integration test:

~~~bash
sudo scripts/run_tests.sh
~~~

The integration script creates the namespace lab, launches both endpoints, checks traffic and counters, runs protocol probes and fuzzing, verifies the tunnel remains usable, and checks clean shutdown.

## Run the lab manually

Create the lab:

~~~bash
sudo scripts/setup_ns.sh
~~~

Terminal 1 — UPF:

~~~bash
sudo ip netns exec ns-upf ./build/gtpu \
  -m upf -t tun0 -l 192.168.10.2 -p 192.168.10.1 \
  -c configs/sessions.conf -v
~~~

Terminal 2 — gNB:

~~~bash
sudo ip netns exec ns-gnb ./build/gtpu \
  -m gnb -t tun0 -l 192.168.10.1 -p 192.168.10.2 \
  -c configs/sessions.conf -v
~~~

Terminal 3 — end-to-end traffic:

~~~bash
sudo ip netns exec ns-ue ping -c 5 10.70.0.2
~~~

Remove the lab:

~~~bash
sudo scripts/teardown_ns.sh
~~~

## Packet capture

Capture N3 traffic:

~~~bash
sudo ip netns exec ns-gnb tcpdump -i n3-gnb -nn -s 0 -w gtpu_capture.pcap udp port 2152
~~~

Inspect it:

~~~bash
sudo ip netns exec ns-gnb tcpdump -nn -vv -r gtpu_capture.pcap 'udp port 2152'
~~~

Filter G-PDUs:

~~~bash
sudo ip netns exec ns-gnb tcpdump -nn -vv -r gtpu_capture.pcap \
  'udp port 2152 and udp[8] = 0x30 and udp[9] = 0xff'
~~~

TShark:

~~~bash
sudo ip netns exec ns-gnb tshark -r gtpu_capture.pcap \
  -T fields -e frame.number -e ip.src -e ip.dst -e udp.payload
~~~

Observed G-PDUs used flags <code>0x30</code>, message type <code>0xff</code>, TEIDs <code>0x64</code> and <code>0xc8</code>, and carried the inner flow <code>10.60.0.2 ↔ 10.70.0.2</code>.

## Validation

| Check | Observed result |
|---|---|
| Normal build | PASS |
| Unit tests | PASS |
| AddressSanitizer + UBSan | PASS |
| ThreadSanitizer | PASS using <code>scripts/tsan_wrap.sh</code> / <code>setarch -R</code> in the current runtime |
| Valgrind | 0 errors, 0 leaks observed |
| 5-packet ping | 0% packet loss |
| 20 × 1392-byte payload ping | 0% packet loss |
| 20 × 1400-byte payload ping | 0% packet loss |
| 500-packet flood | 500/500 received |
| GTP-U packet capture | Valid G-PDUs observed |
| Fresh checksum capture | UDP checksum reported <code>sum ok</code> |
| Echo keepalive | PASS |
| Peer failure/recovery | PASS |
| Malformed 4-byte GTP-U input | Rejected; endpoint remained alive |

The TSan workaround reflects the current Codespace/runtime environment.

## MTU design

The project accounts for:

~~~text
Outer IPv4  20 bytes
UDP          8 bytes
GTP-U        8 bytes
------------------
Total        36 bytes
~~~

Therefore:

~~~text
1500 - 36 = 1464
~~~

and both TUN devices use MTU <code>1464</code>.

## Design decisions

**TUN:** exposes IP packets directly to the program.

**Namespaces:** provide a reproducible UE/gNB/UPF/DN topology on one Linux host.

**TEID:** provides the tunnel/session key used to map GTP-U traffic to a UE context.

**Read/write lock:** protects the shared session table while allowing concurrent lookups.

**Atomic counters:** allow safe statistics updates from multiple worker threads.

**Echo keepalive:** provides an observable liveness and recovery state machine.

## Project structure

~~~text
gtpu-lite/
├── configs/
│   └── sessions.conf
├── include/
├── scripts/
│   ├── run_tests.sh
│   ├── setup_ns.sh
│   ├── teardown_ns.sh
│   └── tsan_wrap.sh
├── src/
└── tests/
    ├── fuzz_gtpu.py
    ├── test_gtpu.c
    └── tun_probe.c
~~~

## Limitations

- Lightweight lab implementation with file-based session configuration
- Session table limited to 64 entries
- IPv4-focused forwarding path
- No real UE/gNB radio stack
- No full 5G Core control plane
- Not intended to replace a production UPF

## Future extensions

Natural next steps include dynamic session management, hash-based session lookup, richer GTP-U extension support, packet-rate/latency benchmarking, CI for sanitizer builds, PCAP regression tests, and PFCP/control-plane integration.

See the [architecture document](docs/architecture.md) for the detailed topology and packet paths.
