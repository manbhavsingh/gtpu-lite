#include "config.h"
#include "gtpu.h"
#include "log.h"
#include "session.h"
#include "stats.h"
#include "tun.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define BUF_SZ  4096
#define POLL_MS 500

#define ECHO_INTERVAL_MS 2000ULL
#define ECHO_TIMEOUT_MS  1000ULL
#define ECHO_MAX_MISSES  3U

enum role { ROLE_GNB, ROLE_UPF };

struct ctx {
    enum role            role;
    int                  tun_fd;
    int                  udp_fd;
    struct sockaddr_in   peer;
    struct session_table sessions;
    struct stats         stats;
    atomic_int           running;

    struct {
        pthread_mutex_t lock;
        uint16_t next_seq;
        uint16_t pending_seq;
        uint64_t deadline_ms;
        uint64_t next_send_ms;
        unsigned misses;
        int pending;
        int peer_up;
    } echo;
};

/* Worker hit an unrecoverable error: ask main thread to shut down. */
static void request_stop(void)
{
    kill(getpid(), SIGTERM);
}

static uint64_t monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;

    return (uint64_t)ts.tv_sec * 1000ULL +
           (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int same_peer(const struct ctx *c, const struct sockaddr_in *from)
{
    return from->sin_family == c->peer.sin_family &&
           from->sin_port == c->peer.sin_port &&
           from->sin_addr.s_addr == c->peer.sin_addr.s_addr;
}

/* Periodically send GTP-U Echo Requests and track peer liveness. */
static void *echo_keepalive(void *arg)
{
    struct ctx *c = arg;
    uint8_t out[64];

    while (atomic_load(&c->running)) {
        struct timespec sleep_for = {
            .tv_sec = 0,
            .tv_nsec = 100000000L
        };

        nanosleep(&sleep_for, NULL);

        if (!atomic_load(&c->running))
            break;

        uint64_t now = monotonic_ms();
        uint16_t seq = 0;
        int send_probe = 0;
        int peer_down = 0;
        int timed_out = 0;

        pthread_mutex_lock(&c->echo.lock);

        if (c->echo.pending && now >= c->echo.deadline_ms) {
            c->echo.pending = 0;
            c->echo.misses++;
            timed_out = 1;

            if (c->echo.misses >= ECHO_MAX_MISSES &&
                c->echo.peer_up) {
                c->echo.peer_up = 0;
                peer_down = 1;
            }
        }

        if (!c->echo.pending && now >= c->echo.next_send_ms) {
            seq = ++c->echo.next_seq;
            c->echo.pending = 1;
            c->echo.pending_seq = seq;
            c->echo.deadline_ms = now + ECHO_TIMEOUT_MS;
            c->echo.next_send_ms = now + ECHO_INTERVAL_MS;
            send_probe = 1;
        }

        pthread_mutex_unlock(&c->echo.lock);

        if (timed_out)
            STAT_INC(&c->stats, echo_timeout);

        if (peer_down) {
            STAT_INC(&c->stats, peer_down);
            LOGW("peer down: %u consecutive GTP-U Echo timeouts",
                 ECHO_MAX_MISSES);
        }

        if (!send_probe)
            continue;

        int len = gtpu_build_echo(out, sizeof out,
                                  GTPU_MSG_ECHO_REQ, seq);

        if (len < 0) {
            LOGE("failed to build Echo Request");

            pthread_mutex_lock(&c->echo.lock);
            if (c->echo.pending && c->echo.pending_seq == seq)
                c->echo.pending = 0;
            pthread_mutex_unlock(&c->echo.lock);
            continue;
        }

        ssize_t sent = sendto(c->udp_fd, out, (size_t)len, 0,
                              (struct sockaddr *)&c->peer,
                              sizeof c->peer);

        if (sent < 0) {
            STAT_INC(&c->stats, send_err);
            LOGW("Echo Request sendto: %s", strerror(errno));

            int down = 0;

            pthread_mutex_lock(&c->echo.lock);

            if (c->echo.pending && c->echo.pending_seq == seq) {
                c->echo.pending = 0;
                c->echo.misses++;

                if (c->echo.misses >= ECHO_MAX_MISSES &&
                    c->echo.peer_up) {
                    c->echo.peer_up = 0;
                    down = 1;
                }
            }

            pthread_mutex_unlock(&c->echo.lock);

            if (down) {
                STAT_INC(&c->stats, peer_down);
                LOGW("peer down: Echo Request transmission failed");
            }

            continue;
        }

        STAT_INC(&c->stats, echo_tx);
        LOGD("echo tx: seq=%u", seq);
    }

    return NULL;
}

/* UE-side packets read from TUN -> encapsulate -> UDP to peer */
static void *tun_to_udp(void *arg)
{
    struct ctx *c = arg;
    uint8_t in[BUF_SZ];
    uint8_t out[BUF_SZ + GTPU_HDR_LEN];
    struct pollfd pfd = { .fd = c->tun_fd, .events = POLLIN };

    while (atomic_load(&c->running)) {
        int r = poll(&pfd, 1, POLL_MS);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            LOGE("poll(tun): %s", strerror(errno));
            request_stop();
            break;
        }
        if (r == 0)
            continue;

        ssize_t n = read(c->tun_fd, in, sizeof in);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            LOGE("read(tun): %s", strerror(errno));
            request_stop();
            break;
        }
        if (n < 20 || (in[0] >> 4) != 4) {
            STAT_INC(&c->stats, drop_non_ipv4);
            continue;
        }

        /* gNB keys on inner source IP, UPF on inner destination IP */
        uint32_t key;
        memcpy(&key, in + (c->role == ROLE_GNB ? 12 : 16), sizeof key);

        struct session s;
        if (session_find_ue(&c->sessions, key, &s) < 0) {
            STAT_INC(&c->stats, drop_no_session);
            LOGD("no session for UE ip, dropping %zd B", n);
            continue;
        }

        uint32_t teid = (c->role == ROLE_GNB) ? s.teid_ul : s.teid_dl;
        int len = gtpu_encap(out, sizeof out, teid, in, (size_t)n);
        if (len < 0) {
            STAT_INC(&c->stats, drop_malformed);
            continue;
        }

        ssize_t sent = sendto(c->udp_fd, out, (size_t)len, 0,
                              (struct sockaddr *)&c->peer, sizeof c->peer);
        if (sent < 0) {
            STAT_INC(&c->stats, send_err);
            LOGW("sendto: %s", strerror(errno));
            continue;
        }
        STAT_INC(&c->stats, tx_pkts);
        STAT_ADD(&c->stats, tx_bytes, n);
        LOGD("tun->udp: %zd B inner, teid=%u", n, teid);
    }
    return NULL;
}

/* UDP from peer -> validate -> decapsulate -> write inner packet to TUN */
static void *udp_to_tun(void *arg)
{
    struct ctx *c = arg;
    uint8_t buf[BUF_SZ];
    uint8_t out[64];
    struct pollfd pfd = { .fd = c->udp_fd, .events = POLLIN };

    while (atomic_load(&c->running)) {
        int r = poll(&pfd, 1, POLL_MS);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            LOGE("poll(udp): %s", strerror(errno));
            request_stop();
            break;
        }
        if (r == 0)
            continue;

        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        ssize_t n = recvfrom(c->udp_fd, buf, sizeof buf, 0,
                             (struct sockaddr *)&from, &fl);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            LOGE("recvfrom: %s", strerror(errno));
            request_stop();
            break;
        }

        struct gtpu_view v;
        int rc = gtpu_parse(buf, (size_t)n, &v);
        if (rc != GTPU_OK) {
            STAT_INC(&c->stats, drop_malformed);
            LOGD("drop: %s (%zd B)", gtpu_strerror(rc), n);
            continue;
        }

        if (v.msg_type == GTPU_MSG_ECHO_REQ) {
            uint16_t seq = 0;

            if ((buf[0] & 0x02) && n >= 12)
                seq = (uint16_t)((buf[8] << 8) | buf[9]);

            int len = gtpu_build_echo(out, sizeof out,
                                      GTPU_MSG_ECHO_RESP, seq);

            if (len > 0)
                sendto(c->udp_fd, out, (size_t)len, 0,
                       (struct sockaddr *)&from, fl);

            STAT_INC(&c->stats, echo_rx);
            continue;
        }

        if (v.msg_type == GTPU_MSG_ECHO_RESP) {
            if (!same_peer(c, &from)) {
                LOGD("ignoring Echo Response from unexpected peer");
                continue;
            }

            if (!(buf[0] & 0x02) || n < 12) {
                STAT_INC(&c->stats, drop_malformed);
                LOGD("drop: Echo Response missing sequence number");
                continue;
            }

            uint16_t seq = (uint16_t)((buf[8] << 8) | buf[9]);
            int matched = 0;
            int recovered = 0;

            pthread_mutex_lock(&c->echo.lock);

            if (c->echo.pending && c->echo.pending_seq == seq) {
                c->echo.pending = 0;
                c->echo.misses = 0;
                matched = 1;

                if (!c->echo.peer_up) {
                    c->echo.peer_up = 1;
                    recovered = 1;
                }
            }

            pthread_mutex_unlock(&c->echo.lock);

            STAT_INC(&c->stats, echo_resp_rx);

            if (matched)
                LOGD("echo rx: seq=%u matched", seq);
            else
                LOGD("echo rx: seq=%u unmatched", seq);

            if (recovered) {
                STAT_INC(&c->stats, peer_recovered);
                LOGI("peer recovered: Echo Response seq=%u", seq);
            }

            continue;
        }

        if (v.msg_type != GTPU_MSG_GPDU) {
            STAT_INC(&c->stats, drop_unsupported);
            continue;
        }

        struct session s;
        int found = (c->role == ROLE_GNB)
                        ? session_find_dl(&c->sessions, v.teid, &s)
                        : session_find_ul(&c->sessions, v.teid, &s);
        if (found < 0) {
            STAT_INC(&c->stats, drop_unknown_teid);
            LOGD("unknown teid %u", v.teid);
            continue;
        }

        if (v.payload_len < 20 || (v.payload[0] >> 4) != 4) {
            STAT_INC(&c->stats, drop_non_ipv4);
            continue;
        }
        /* UPF: inner source must be the UE this TEID belongs to */
        if (c->role == ROLE_UPF && memcmp(v.payload + 12, &s.ue_ip, 4) != 0) {
            STAT_INC(&c->stats, drop_spoofed);
            LOGW("spoofed inner source on teid %u", v.teid);
            continue;
        }

        ssize_t w = write(c->tun_fd, v.payload, v.payload_len);
        if (w < 0) {
            STAT_INC(&c->stats, tun_write_err);
            LOGW("write(tun): %s", strerror(errno));
            continue;
        }
        STAT_INC(&c->stats, rx_pkts);
        STAT_ADD(&c->stats, rx_bytes, v.payload_len);
        LOGD("udp->tun: %zu B inner, teid=%u", v.payload_len, v.teid);
    }
    return NULL;
}

static void usage(const char *p)
{
    fprintf(stderr,
            "usage: %s -m gnb|upf -t <tun> -l <local_ip> -p <peer_ip> "
            "-c <config> [-v]\n", p);
}

int main(int argc, char **argv)
{
    const char *mode = NULL, *tun = NULL, *local = NULL;
    const char *peer = NULL, *conf = NULL;
    int verbose = 0, opt;

    while ((opt = getopt(argc, argv, "m:t:l:p:c:v")) != -1) {
        switch (opt) {
        case 'm': mode = optarg;  break;
        case 't': tun = optarg;   break;
        case 'l': local = optarg; break;
        case 'p': peer = optarg;  break;
        case 'c': conf = optarg;  break;
        case 'v': verbose = 1;    break;
        default:  usage(argv[0]); return 2;
        }
    }
    if (!mode || !tun || !local || !peer || !conf) {
        usage(argv[0]);
        return 2;
    }

    struct ctx c;
    memset(&c, 0, sizeof c);
    atomic_init(&c.running, 1);

    if (strcmp(mode, "gnb") == 0) {
        c.role = ROLE_GNB;
    } else if (strcmp(mode, "upf") == 0) {
        c.role = ROLE_UPF;
    } else {
        usage(argv[0]);
        return 2;
    }
    log_set_level(verbose ? LVL_DEBUG : LVL_INFO);

    if (session_table_init(&c.sessions) != 0) {
        LOGE("session table init failed");
        return 1;
    }
    int ns = config_load(conf, &c.sessions);
    if (ns <= 0) {
        LOGE("no sessions loaded from %s", conf);
        return 1;
    }

    c.tun_fd = tun_open(tun);
    if (c.tun_fd < 0) {
        LOGE("tun_open(%s): %s", tun, strerror(errno));
        return 1;
    }

    c.udp_fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (c.udp_fd < 0) {
        LOGE("socket: %s", strerror(errno));
        return 1;
    }

    struct sockaddr_in la;
    memset(&la, 0, sizeof la);
    la.sin_family = AF_INET;
    la.sin_port   = htons(GTPU_PORT);
    memset(&c.peer, 0, sizeof c.peer);
    c.peer.sin_family = AF_INET;
    c.peer.sin_port   = htons(GTPU_PORT);
    if (inet_pton(AF_INET, local, &la.sin_addr) != 1 ||
        inet_pton(AF_INET, peer, &c.peer.sin_addr) != 1) {
        LOGE("bad local/peer IP address");
        return 2;
    }
    if (bind(c.udp_fd, (struct sockaddr *)&la, sizeof la) < 0) {
        LOGE("bind %s:%d: %s", local, GTPU_PORT, strerror(errno));
        return 1;
    }

    /* Block signals before creating threads so they inherit the mask;
     * main thread receives them synchronously via sigwait(). */
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    int e = pthread_mutex_init(&c.echo.lock, NULL);
    if (e) {
        LOGE("pthread_mutex_init: %s", strerror(e));
        close(c.udp_fd);
        close(c.tun_fd);
        session_table_destroy(&c.sessions);
        return 1;
    }

    c.echo.peer_up = 1;
    c.echo.next_seq = 0;
    c.echo.next_send_ms = 0;
    c.echo.pending = 0;
    c.echo.pending_seq = 0;
    c.echo.deadline_ms = 0;
    c.echo.misses = 0;

    pthread_t t_up, t_down, t_echo;

    e = pthread_create(&t_up, NULL, tun_to_udp, &c);
    if (e) {
        LOGE("pthread_create: %s", strerror(e));
        pthread_mutex_destroy(&c.echo.lock);
        close(c.udp_fd);
        close(c.tun_fd);
        session_table_destroy(&c.sessions);
        return 1;
    }

    e = pthread_create(&t_down, NULL, udp_to_tun, &c);
    if (e) {
        LOGE("pthread_create: %s", strerror(e));
        atomic_store(&c.running, 0);
        pthread_join(t_up, NULL);
        pthread_mutex_destroy(&c.echo.lock);
        close(c.udp_fd);
        close(c.tun_fd);
        session_table_destroy(&c.sessions);
        return 1;
    }

    e = pthread_create(&t_echo, NULL, echo_keepalive, &c);
    if (e) {
        LOGE("pthread_create: %s", strerror(e));
        atomic_store(&c.running, 0);
        pthread_join(t_up, NULL);
        pthread_join(t_down, NULL);
        pthread_mutex_destroy(&c.echo.lock);
        close(c.udp_fd);
        close(c.tun_fd);
        session_table_destroy(&c.sessions);
        return 1;
    }

    LOGI("%s up: tun=%s local=%s peer=%s sessions=%d (SIGUSR1=stats)",
         mode, tun, local, peer, ns);

    for (;;) {
        int sig;
        if (sigwait(&set, &sig) != 0)
            continue;
        if (sig == SIGUSR1) {
            stats_print(&c.stats);
            continue;
        }
        break;
    }

    LOGI("shutting down");
    atomic_store(&c.running, 0);

    pthread_join(t_up, NULL);
    pthread_join(t_down, NULL);
    pthread_join(t_echo, NULL);

    stats_print(&c.stats);

    close(c.udp_fd);
    close(c.tun_fd);
    session_table_destroy(&c.sessions);
    pthread_mutex_destroy(&c.echo.lock);

    return 0;
}