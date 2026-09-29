#include "stats.h"

#include "log.h"

#define G(f) ((unsigned long long)atomic_load_explicit(&s->f, memory_order_relaxed))

void stats_print(const struct stats *s)
{
    LOGI("stats: tx=%llu pkts (%llu B)  rx=%llu pkts (%llu B)  echo_rx=%llu",
         G(tx_pkts), G(tx_bytes), G(rx_pkts), G(rx_bytes), G(echo_rx));
    LOGI("drops: no_session=%llu unknown_teid=%llu malformed=%llu "
         "non_ipv4=%llu unsupported=%llu spoofed=%llu",
         G(drop_no_session), G(drop_unknown_teid), G(drop_malformed),
         G(drop_non_ipv4), G(drop_unsupported), G(drop_spoofed));
    LOGI("errors: send=%llu tun_write=%llu", G(send_err), G(tun_write_err));
}