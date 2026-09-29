#!/usr/bin/env bash
set -euo pipefail
[ "$EUID" -eq 0 ] || { echo "run as root: sudo $0"; exit 1; }

NSS="ns-ue ns-gnb ns-upf ns-dn"
for ns in $NSS; do ip netns add "$ns"; done

# UE <-> gNB
ip link add ue0 type veth peer name ue0-gnb
ip link set ue0 netns ns-ue
ip link set ue0-gnb netns ns-gnb
# gNB <-> UPF (N3 underlay, carries GTP-U)
ip link add n3-gnb type veth peer name n3-upf
ip link set n3-gnb netns ns-gnb
ip link set n3-upf netns ns-upf
# UPF <-> data network (N6)
ip link add n6-upf type veth peer name dn0
ip link set n6-upf netns ns-upf
ip link set dn0 netns ns-dn

ip -n ns-ue  addr add 10.60.0.2/24 dev ue0
ip -n ns-gnb addr add 10.60.0.1/24 dev ue0-gnb
ip -n ns-gnb addr add 192.168.10.1/24 dev n3-gnb
ip -n ns-upf addr add 192.168.10.2/24 dev n3-upf
ip -n ns-upf addr add 10.70.0.1/24 dev n6-upf
ip -n ns-dn  addr add 10.70.0.2/24 dev dn0

# persistent TUN devices; our C program attaches to them later
ip -n ns-gnb tuntap add dev tun0 mode tun
ip -n ns-upf tuntap add dev tun0 mode tun

for ns in $NSS; do ip -n "$ns" link set lo up; done
ip -n ns-ue  link set ue0 up
ip -n ns-gnb link set ue0-gnb up
ip -n ns-gnb link set n3-gnb up
ip -n ns-gnb link set tun0 up
ip -n ns-upf link set n3-upf up
ip -n ns-upf link set n6-upf up
ip -n ns-upf link set tun0 up
ip -n ns-dn  link set dn0 up

# routing: uplink enters tun0 on gNB, downlink enters tun0 on UPF
ip -n ns-ue  route add default via 10.60.0.1
ip -n ns-dn  route add default via 10.70.0.1
ip -n ns-gnb route add 10.70.0.0/24 dev tun0
ip -n ns-upf route add 10.60.0.0/24 dev tun0

for ns in ns-gnb ns-upf; do
  ip netns exec "$ns" sysctl -qw net.ipv4.ip_forward=1
done
# disable reverse-path filtering (tunnelled traffic confuses it)
for ns in ns-gnb ns-upf; do
  ip netns exec "$ns" bash -c 'for f in /proc/sys/net/ipv4/conf/*/rp_filter; do echo 0 > $f; done'
done
# disable IPv6 so the kernel doesn't push v6 noise into our TUN
for ns in ns-ue ns-gnb ns-upf ns-dn; do
  ip netns exec "$ns" sysctl -qw net.ipv6.conf.all.disable_ipv6=1
  ip netns exec "$ns" sysctl -qw net.ipv6.conf.default.disable_ipv6=1
done
echo "lab ready"