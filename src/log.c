#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static volatile int g_level = LVL_INFO;
static const char *const names[] = {"ERROR", "WARN", "INFO", "DEBUG"};

void log_set_level(enum log_level l)
{
    g_level = l;
}

void log_msg(enum log_level l, const char *fmt, ...)
{
    if ((int)l > g_level)
        return;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    char t[16];
    strftime(t, sizeof t, "%H:%M:%S", &tm);

    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    fprintf(stderr, "%s.%03ld [%s] [tid %ld] %s\n", t, ts.tv_nsec / 1000000,
            names[l], (long)syscall(SYS_gettid), msg);
}