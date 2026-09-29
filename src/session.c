#include "session.h"

#include <string.h>

enum key { KEY_UL, KEY_DL, KEY_UE };

int session_table_init(struct session_table *t)
{
    memset(t, 0, sizeof *t);
    return pthread_rwlock_init(&t->lock, NULL);
}

void session_table_destroy(struct session_table *t)
{
    pthread_rwlock_destroy(&t->lock);
}

int session_add(struct session_table *t, uint32_t ul, uint32_t dl,
                uint32_t ue_ip)
{
    int rc = -1;

    pthread_rwlock_wrlock(&t->lock);
    if (t->count < SESSION_MAX) {
        int dup = 0;
        for (int i = 0; i < t->count; i++) {
            if (t->s[i].teid_ul == ul || t->s[i].teid_dl == dl ||
                t->s[i].ue_ip == ue_ip)
                dup = 1;
        }
        if (!dup) {
            t->s[t->count].teid_ul = ul;
            t->s[t->count].teid_dl = dl;
            t->s[t->count].ue_ip   = ue_ip;
            t->count++;
            rc = 0;
        }
    }
    pthread_rwlock_unlock(&t->lock);
    return rc;
}

static int find(struct session_table *t, enum key k, uint32_t v,
                struct session *out)
{
    int rc = -1;

    pthread_rwlock_rdlock(&t->lock);
    for (int i = 0; i < t->count; i++) {
        const struct session *s = &t->s[i];
        uint32_t f = (k == KEY_UL) ? s->teid_ul
                   : (k == KEY_DL) ? s->teid_dl
                   : s->ue_ip;
        if (f == v) {
            *out = *s;
            rc = 0;
            break;
        }
    }
    pthread_rwlock_unlock(&t->lock);
    return rc;
}

int session_find_ul(struct session_table *t, uint32_t teid, struct session *out)
{
    return find(t, KEY_UL, teid, out);
}

int session_find_dl(struct session_table *t, uint32_t teid, struct session *out)
{
    return find(t, KEY_DL, teid, out);
}

int session_find_ue(struct session_table *t, uint32_t ue_ip, struct session *out)
{
    return find(t, KEY_UE, ue_ip, out);
}