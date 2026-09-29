#include "stats.h"

#include "log.h"

#define G(f) ((unsigned long long)atomic_load_explicit(&s->f, memory_order_relaxed))

void stats_print(const struct stats *s)
{
    LOGI("stats: tx=%llu pkts (%llu B)  rx=%llu pkts (%llu B)",
         G(tx_pkts), G(tx_bytes), G(rx_pkts), G(rx_bytes));
    LOGI("echo: tx=%llu req_rx=%llu resp_rx=%llu timeouts=%llu",
         G(echo_tx), G(echo_rx), G(echo_resp_rx), G(echo_timeout));
    LOGI("peer: down=%llu recovered=%llu",
         G(peer_down), G(peer_recovered));
    LOGI("drops: no_session=%llu unknown_teid=%llu malformed=%llu "
         "non_ipv4=%llu unsupported=%llu spoofed=%llu",
         G(drop_no_session), G(drop_unknown_teid), G(drop_malformed),
         G(drop_non_ipv4), G(drop_unsupported), G(drop_spoofed));
    LOGI("errors: send=%llu tun_write=%llu", G(send_err), G(tun_write_err));
}