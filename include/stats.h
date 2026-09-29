#ifndef GTPU_STATS_H
#define GTPU_STATS_H

#include <stdatomic.h>

struct stats {
    atomic_ullong tx_pkts, tx_bytes, rx_pkts, rx_bytes;
    atomic_ullong echo_tx, echo_rx, echo_resp_rx, echo_timeout;
    atomic_ullong peer_down, peer_recovered;
    atomic_ullong drop_no_session, drop_unknown_teid, drop_malformed;
    atomic_ullong drop_non_ipv4, drop_unsupported, drop_spoofed;
    atomic_ullong send_err, tun_write_err;
};

#define STAT_INC(sp, f) \
    atomic_fetch_add_explicit(&(sp)->f, 1ULL, memory_order_relaxed)
#define STAT_ADD(sp, f, n) \
    atomic_fetch_add_explicit(&(sp)->f, (unsigned long long)(n), \
                              memory_order_relaxed)

void stats_print(const struct stats *s);

#endif