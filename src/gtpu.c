#include "gtpu.h"

#include <string.h>

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int gtpu_encap(uint8_t *out, size_t cap, uint32_t teid,
               const uint8_t *payload, size_t len)
{
    if (len > 0xFFFF || cap < GTPU_HDR_LEN + len)
        return GTPU_ERR_NOSPACE;

    out[0] = 0x30;              /* ver=1, PT=1, no E/S/PN */
    out[1] = GTPU_MSG_GPDU;
    put_be16(out + 2, (uint16_t)len);
    put_be32(out + 4, teid);
    if (len)
        memcpy(out + GTPU_HDR_LEN, payload, len);
    return (int)(GTPU_HDR_LEN + len);
}

int gtpu_parse(const uint8_t *pkt, size_t len, struct gtpu_view *v)
{
    if (len < GTPU_HDR_LEN)
        return GTPU_ERR_SHORT;

    uint8_t flags = pkt[0];
    if ((flags >> 5) != 1)
        return GTPU_ERR_VERSION;
    if (!(flags & 0x10))
        return GTPU_ERR_PT;

    /* length = bytes after the first 8 (includes optional fields) */
    uint16_t plen = get_be16(pkt + 2);
    if ((size_t)GTPU_HDR_LEN + plen > len)
        return GTPU_ERR_TRUNC;

    size_t end = GTPU_HDR_LEN + plen;
    size_t off = GTPU_HDR_LEN;

    if (flags & 0x07) {         /* any of E(0x04), S(0x02), PN(0x01) */
        if (end - off < GTPU_OPT_LEN)
            return GTPU_ERR_EXT;
        uint8_t next = (flags & 0x04) ? pkt[off + 3] : 0;
        off += GTPU_OPT_LEN;

        while (next != 0) {     /* walk extension header chain */
            if (off >= end)
                return GTPU_ERR_EXT;
            size_t ext_len = (size_t)pkt[off] * 4; /* units of 4 bytes */
            if (ext_len == 0 || ext_len > end - off)
                return GTPU_ERR_EXT;
            next = pkt[off + ext_len - 1];
            off += ext_len;
        }
    }

    v->msg_type    = pkt[1];
    v->teid        = get_be32(pkt + 4);
    v->payload     = pkt + off;
    v->payload_len = end - off;
    return GTPU_OK;
}

int gtpu_build_echo(uint8_t *out, size_t cap, uint8_t msg_type, uint16_t seq)
{
    size_t body  = GTPU_OPT_LEN + (msg_type == GTPU_MSG_ECHO_RESP ? 2 : 0);
    size_t total = GTPU_HDR_LEN + body;
    if (cap < total)
        return GTPU_ERR_NOSPACE;

    out[0] = 0x32;              /* ver=1, PT=1, S=1 */
    out[1] = msg_type;
    put_be16(out + 2, (uint16_t)body);
    put_be32(out + 4, 0);       /* echo uses TEID 0 */
    put_be16(out + 8, seq);
    out[10] = 0;                /* N-PDU */
    out[11] = 0;                /* no extension */
    if (msg_type == GTPU_MSG_ECHO_RESP) {
        out[12] = 14;           /* Recovery IE */
        out[13] = 0;
    }
    return (int)total;
}

const char *gtpu_strerror(int err)
{
    switch (err) {
    case GTPU_OK:          return "ok";
    case GTPU_ERR_SHORT:   return "packet shorter than GTP-U header";
    case GTPU_ERR_VERSION: return "unsupported GTP version";
    case GTPU_ERR_PT:      return "protocol type is not GTP";
    case GTPU_ERR_TRUNC:   return "length field exceeds datagram";
    case GTPU_ERR_EXT:     return "malformed extension header";
    case GTPU_ERR_NOSPACE: return "output buffer too small";
    default:               return "unknown error";
    }
}