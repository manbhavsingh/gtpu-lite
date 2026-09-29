#include "gtpu.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void test_encap_vector(void)
{
    const uint8_t pay[4] = {0xde, 0xad, 0xbe, 0xef};
    const uint8_t want[12] = {0x30, 0xff, 0x00, 0x04, 0x11, 0x22, 0x33, 0x44,
                              0xde, 0xad, 0xbe, 0xef};
    uint8_t out[64];
    int n = gtpu_encap(out, sizeof out, 0x11223344, pay, sizeof pay);
    CHECK(n == 12);
    CHECK(memcmp(out, want, 12) == 0);
}

static void test_roundtrip_and_errors(void)
{
    const uint8_t pay[4] = {1, 2, 3, 4};
    uint8_t out[64];
    struct gtpu_view v;
    int n = gtpu_encap(out, sizeof out, 0xAABBCCDD, pay, sizeof pay);

    CHECK(gtpu_parse(out, (size_t)n, &v) == GTPU_OK);
    CHECK(v.msg_type == GTPU_MSG_GPDU);
    CHECK(v.teid == 0xAABBCCDD);
    CHECK(v.payload_len == 4);
    CHECK(memcmp(v.payload, pay, 4) == 0);

    CHECK(gtpu_parse(out, 7, &v) == GTPU_ERR_SHORT);
    CHECK(gtpu_parse(out, (size_t)n - 1, &v) == GTPU_ERR_TRUNC);

    out[0] = 0x50;              /* version 2 */
    CHECK(gtpu_parse(out, (size_t)n, &v) == GTPU_ERR_VERSION);
    out[0] = 0x20;              /* version 1, PT=0 (GTP') */
    CHECK(gtpu_parse(out, (size_t)n, &v) == GTPU_ERR_PT);

    CHECK(gtpu_encap(out, 8, 1, pay, sizeof pay) == GTPU_ERR_NOSPACE);
}

static void test_extension_header(void)
{
    uint8_t pkt[18] = {0x34, 0xff, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x05,
                       0x00, 0x00, 0x00, 0x85,
                       0x01, 0x00, 0x00, 0x00,
                       0xaa, 0xbb};
    struct gtpu_view v;

    CHECK(gtpu_parse(pkt, sizeof pkt, &v) == GTPU_OK);
    CHECK(v.teid == 5);
    CHECK(v.payload_len == 2);
    CHECK(v.payload[0] == 0xaa && v.payload[1] == 0xbb);

    pkt[12] = 0;                /* extension length of 0 is illegal */
    CHECK(gtpu_parse(pkt, sizeof pkt, &v) == GTPU_ERR_EXT);
}

static void test_echo(void)
{
    const uint8_t want_req[12] = {0x32, 0x01, 0x00, 0x04, 0, 0, 0, 0,
                                  0x01, 0x02, 0x00, 0x00};
    uint8_t out[32];
    struct gtpu_view v;

    int n = gtpu_build_echo(out, sizeof out, GTPU_MSG_ECHO_REQ, 0x0102);
    CHECK(n == 12);
    CHECK(memcmp(out, want_req, 12) == 0);
    CHECK(gtpu_parse(out, (size_t)n, &v) == GTPU_OK);
    CHECK(v.msg_type == GTPU_MSG_ECHO_REQ && v.teid == 0);

    n = gtpu_build_echo(out, sizeof out, GTPU_MSG_ECHO_RESP, 7);
    CHECK(n == 14);
    CHECK(out[3] == 6);
    CHECK(gtpu_parse(out, (size_t)n, &v) == GTPU_OK);
    CHECK(v.msg_type == GTPU_MSG_ECHO_RESP);
}

int main(void)
{
    test_encap_vector();
    test_roundtrip_and_errors();
    test_extension_header();
    test_echo();
    if (fails) {
        printf("%d check(s) failed\n", fails);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}