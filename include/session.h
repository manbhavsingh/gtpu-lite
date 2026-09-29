#ifndef GTPU_SESSION_H
#define GTPU_SESSION_H

#include <pthread.h>
#include <stdint.h>

#define SESSION_MAX 64

struct session {
    uint32_t teid_ul;  /* gNB -> UPF direction */
    uint32_t teid_dl;  /* UPF -> gNB direction */
    uint32_t ue_ip;    /* network byte order */
};

struct session_table {
    pthread_rwlock_t lock;
    struct session   s[SESSION_MAX];
    int              count;
};

int  session_table_init(struct session_table *t);
void session_table_destroy(struct session_table *t);

/* 0 on success, -1 if full or any key already exists */
int session_add(struct session_table *t, uint32_t teid_ul, uint32_t teid_dl,
                uint32_t ue_ip);

/* Lookups copy the entry into *out. 0 = found, -1 = not found. */
int session_find_ul(struct session_table *t, uint32_t teid, struct session *out);
int session_find_dl(struct session_table *t, uint32_t teid, struct session *out);
int session_find_ue(struct session_table *t, uint32_t ue_ip, struct session *out);

#endif