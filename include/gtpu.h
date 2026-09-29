#ifndef GTPU_H
#define GTPU_H

#include <stddef.h>
#include <stdint.h>

#define GTPU_PORT        2152
#define GTPU_HDR_LEN     8   /* mandatory header */
#define GTPU_OPT_LEN     4   /* seq(2) + n-pdu(1) + next-ext(1) */
#define GTPU_OVERHEAD    (20 + 8 + GTPU_HDR_LEN) /* outer IP + UDP + GTP */

#define GTPU_MSG_ECHO_REQ  1
#define GTPU_MSG_ECHO_RESP 2
#define GTPU_MSG_GPDU      255

enum gtpu_err {
    GTPU_OK          = 0,
    GTPU_ERR_SHORT   = -1, /* smaller than mandatory header */
    GTPU_ERR_VERSION = -2, /* not GTP version 1 */
    GTPU_ERR_PT      = -3, /* protocol type is not GTP */
    GTPU_ERR_TRUNC   = -4, /* length field exceeds datagram */
    GTPU_ERR_EXT     = -5, /* malformed optional/extension fields */
    GTPU_ERR_NOSPACE = -6  /* output buffer too small / payload too big */
};

/* Result of parsing; payload points INTO the input buffer (no copy). */
struct gtpu_view {
    uint8_t        msg_type;
    uint32_t       teid;
    const uint8_t *payload;
    size_t         payload_len;
};

/* Wrap payload in a G-PDU. Returns total bytes written or negative error. */
int gtpu_encap(uint8_t *out, size_t cap, uint32_t teid,
               const uint8_t *payload, size_t len);

/* Validate and parse a received UDP payload. Returns GTPU_OK or error. */
int gtpu_parse(const uint8_t *pkt, size_t len, struct gtpu_view *v);

/* Build Echo Request (msg_type 1) or Response (2). Returns bytes or error. */
int gtpu_build_echo(uint8_t *out, size_t cap, uint8_t msg_type, uint16_t seq);

const char *gtpu_strerror(int err);

#endif