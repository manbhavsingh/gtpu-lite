#include "tun.h"

#include <arpa/inet.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "tun0";
    int fd = tun_open(dev);
    if (fd < 0) {
        perror("tun_open");
        return 1;
    }
    printf("attached to %s (fd=%d)\n", dev, fd);

    unsigned char buf[2048] __attribute__((aligned(4)));
    for (;;) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n < 0) {
            perror("read");
            return 1;
        }
        unsigned ver = buf[0] >> 4;
        if (ver != 4) {
            printf("skip non-IPv4 packet (v%u, %zd bytes)\n", ver, n);
            continue;
        }
        if (n < (ssize_t)sizeof(struct iphdr)) {
            printf("short packet: %zd bytes\n", n);
            continue;
        }
        struct iphdr *ip = (struct iphdr *)buf;
        char s[INET_ADDRSTRLEN], d[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip->saddr, s, sizeof s);
        inet_ntop(AF_INET, &ip->daddr, d, sizeof d);
        printf("read %zd bytes: v%u %s -> %s proto=%u\n",
               n, ip->version, s, d, ip->protocol);
    }
}