#include "config.h"

#include "log.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int config_load(const char *path, struct session_table *t)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        LOGE("cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    char line[256];
    int lineno = 0, count = 0;

    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *p = line;
        while (isspace((unsigned char)*p))
            p++;
        if (*p == '\0' || *p == '#')
            continue;

        unsigned ul, dl;
        char ip[INET_ADDRSTRLEN];
        struct in_addr a;

        if (sscanf(p, "session %u %u %15s", &ul, &dl, ip) != 3 ||
            inet_pton(AF_INET, ip, &a) != 1) {
            LOGE("%s:%d: malformed line", path, lineno);
            fclose(f);
            return -1;
        }
        if (session_add(t, ul, dl, a.s_addr) != 0) {
            LOGE("%s:%d: duplicate session or table full", path, lineno);
            fclose(f);
            return -1;
        }
        count++;
    }
    fclose(f);
    return count;
}