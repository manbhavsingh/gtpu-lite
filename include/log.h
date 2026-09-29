#ifndef GTPU_LOG_H
#define GTPU_LOG_H

enum log_level { LVL_ERROR, LVL_WARN, LVL_INFO, LVL_DEBUG };

void log_set_level(enum log_level l);
void log_msg(enum log_level l, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#define LOGE(...) log_msg(LVL_ERROR, __VA_ARGS__)
#define LOGW(...) log_msg(LVL_WARN, __VA_ARGS__)
#define LOGI(...) log_msg(LVL_INFO, __VA_ARGS__)
#define LOGD(...) log_msg(LVL_DEBUG, __VA_ARGS__)

#endif