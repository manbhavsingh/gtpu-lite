# gtpu-lite Architecture

## 1. Topology

~~~mermaid
flowchart LR
    UE["ns-ue<br/>10.60.0.2"]
    GNB["ns-gnb<br/>ue0-gnb: 10.60.0.1<br/>n3-gnb: 192.168.10.1<br/>tun0"]
    UPF["ns-upf<br/>n3-upf: 192.168.10.2<br/>n6-upf: 10.70.0.1<br/>tun0"]
    DN["ns-dn<br/>10.70.0.2"]

    UE -->|"ue0 ↔ ue0-gnb"| GNB
    GNB -->|"N3: GTP-U over UDP/2152"| UPF
    UPF -->|"N6: n6-upf ↔ dn0"| DN
~~~

## 2. Namespace roles

| Namespace | Role |
|---|---|
| ns-ue | Generates UE-side IPv4 traffic |
| ns-gnb | Encapsulates uplink traffic and decapsulates downlink traffic |
| ns-upf | Decapsulates uplink traffic and encapsulates downlink traffic |
| ns-dn | Represents the external data network |

## 3. Interface map

| Namespace | Interface | Address | Connected to / purpose |
|---|---|---|---|
| ns-ue | ue0 | 10.60.0.2/24 | ns-gnb/ue0-gnb |
| ns-gnb | ue0-gnb | 10.60.0.1/24 | UE-facing link |
| ns-gnb | n3-gnb | 192.168.10.1/24 | ns-upf/n3-upf |
| ns-gnb | tun0 | MTU 1464 | gNB packet boundary |
| ns-upf | n3-upf | 192.168.10.2/24 | ns-gnb/n3-gnb |
| ns-upf | n6-upf | 10.70.0.1/24 | ns-dn/dn0 |
| ns-upf | tun0 | MTU 1464 | UPF packet boundary |
| ns-dn | dn0 | 10.70.0.2/24 | ns-upf/n6-upf |

## 4. Routing

| Namespace | Route |
|---|---|
| ns-ue | default via 10.60.0.1 |
| ns-gnb | 10.70.0.0/24 via tun0 |
| ns-upf | 10.60.0.0/24 via tun0 |
| ns-dn | default via 10.70.0.1 |

The setup script also enables IPv4 forwarding on gNB and UPF, disables reverse-path filtering there, and disables IPv6 in all four namespaces.

## 5. Uplink data path

~~~text
10.60.0.2
   |
   v
ns-ue / ue0
   |
   v
ns-gnb / tun0
   |
   | session lookup by UE source IP
   | UL TEID = 100 / 0x64
   | GTP-U encapsulation
   v
192.168.10.1:2152
   |
   | UDP / GTP-U
   v
192.168.10.2:2152
   |
   v
ns-upf / UDP socket
   |
   | GTP-U parse
   | TEID lookup
   | IPv4 validation
   | inner-source ownership check
   | decapsulation
   v
ns-upf / tun0
   |
   v
10.70.0.2
~~~

## 6. Downlink data path

~~~text
10.70.0.2
   |
   v
ns-upf / tun0
   |
   | session lookup
   | DL TEID = 200 / 0xc8
   | GTP-U encapsulation
   v
192.168.10.2:2152
   |
   | UDP / GTP-U
   v
192.168.10.1:2152
   |
   v
ns-gnb / UDP socket
   |
   | GTP-U parse
   | TEID lookup
   | decapsulation
   v
ns-gnb / tun0
   |
   v
ns-ue
~~~

## 7. Session model

Configuration:

~~~text
session 100 200 10.60.0.2
~~~

Each session contains:

- UL TEID
- DL TEID
- UE IP

The session table is limited to 64 entries. Lookups are protected by <code>pthread_rwlock_t</code>, and a matching entry is copied before the lock is released.

## 8. GTP-U codec

<code>src/gtpu.c</code> provides:

1. G-PDU encapsulation with flags <code>0x30</code>
2. GTP-U parsing and validation
3. version and protocol-type checks
4. advertised-length validation
5. optional-field/extension-chain validation
6. Echo Request construction
7. Echo Response construction
8. parser error codes

Mandatory GTP-U header length: 8 bytes.

## 9. Thread model

Each endpoint starts three workers:

~~~text
main thread
    |
    +--> TUN -> UDP worker
    |       read TUN
    |       session lookup
    |       GTP-U encapsulation
    |       send UDP
    |
    +--> UDP -> TUN worker
    |       recv UDP
    |       GTP-U parse
    |       session lookup
    |       decapsulation
    |       write TUN
    |
    +--> Echo worker
            periodic Echo Request
            pending request tracking
            timeout handling
            peer state transitions
~~~

Shared-state protection:

- session table: <code>pthread_rwlock_t</code>
- Echo state: <code>pthread_mutex_t</code>
- packet/error counters: C11 atomics

The main thread blocks SIGINT, SIGTERM, and SIGUSR1 before creating workers and then handles them through <code>sigwait()</code>.

## 10. Keepalive state machine

~~~text
PEER UP
  |
  | 3 consecutive missed Echo Responses
  v
PEER DOWN
  |
  | matching Echo Response
  v
PEER RECOVERED
~~~

Parameters:

| Parameter | Value |
|---|---|
| Echo interval | 2 seconds |
| Echo timeout | 1 second |
| Down threshold | 3 misses |

A matching response resets the miss counter. The implementation records peer-down and peer-recovered events.

## 11. Receive-side validation

A received UDP datagram is checked for:

1. minimum GTP-U header size
2. version 1
3. GTP protocol type
4. advertised payload length
5. optional/extension-field correctness
6. supported message type
7. known TEID/session
8. inner IPv4 format
9. UPF inner-source ownership

Drop and error counters are kept separately for each class.

## 12. MTU calculation

The project reserves:

~~~text
Outer IPv4   20 bytes
UDP           8 bytes
GTP-U         8 bytes
-------------------
Total        36 bytes
~~~

Therefore a 1500-byte underlay leaves:

~~~text
1500 - 36 = 1464
~~~

The setup script configures both TUN devices with MTU 1464.

## 13. Packet-capture shape

~~~text
UDP/2152
└── GTP-U
    ├── flags = 0x30
    ├── message type = 0xff
    ├── TEID = 0x00000064 or 0x000000c8
    └── inner IPv4 packet
        ├── UL: 10.60.0.2 -> 10.70.0.2
        └── DL: 10.70.0.2 -> 10.60.0.2
~~~

Echo messages use message types 1 and 2 and carry the sequence field.

## 14. Validation evidence

Observed validation included:

- unit tests: all passed
- ASan + UBSan: all tests passed
- TSan: all tests passed using <code>scripts/tsan_wrap.sh</code> / <code>setarch -R</code> in the current runtime
- Valgrind: 0 errors and 0 leaks observed
- 1392-byte payload ping: 20/20 received
- 1400-byte payload ping: 20/20 received
- 500-packet flood: 500/500 received
- fresh packet capture: UDP checksum reported <code>sum ok</code>
- peer-stop/restart test: peer-down and recovery events observed
- malformed 4-byte GTP-U packet: rejected and endpoint remained alive

## 15. Source responsibilities

| File | Responsibility |
|---|---|
| src/main.c | Endpoint lifecycle, packet workers, keepalive, signal handling |
| src/gtpu.c | GTP-U encoding/decoding/validation |
| src/session.c | Session table and synchronized lookups |
| src/config.c | Session configuration loading |
| src/stats.c | Statistics reporting |
| src/log.c | Logging |
| src/tun.c | TUN device opening |
| scripts/setup_ns.sh | Namespace, links, addresses, routing |
| scripts/run_tests.sh | Automated integration validation |
| scripts/tsan_wrap.sh | TSan runtime workaround |
