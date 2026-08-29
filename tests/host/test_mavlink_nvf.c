#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mavlink_nvf.h"

/* Cross-check: pack then verify STX/len/msgid and CRC recompute. */
static uint16_t crc_accumulate(uint8_t data, uint16_t crc)
{
    uint8_t tmp = data ^ (uint8_t)(crc & 0xFFU);
    tmp ^= (uint8_t)(tmp << 4);
    crc = (uint16_t)((crc >> 8)
                     ^ ((uint16_t)tmp << 8)
                     ^ ((uint16_t)tmp << 3)
                     ^ ((uint16_t)tmp >> 4));
    return crc;
}

static void test_pack_basic(void)
{
    uint8_t frame[MAVLINK_NVF_FRAME_LEN];
    uint16_t crc = 0xFFFFU;
    size_t n;
    size_t i;
    union {
        float f;
        uint8_t b[4];
    } conf;

    n = mavlink_nvf_pack(frame, sizeof(frame), 7U, 1U, 191U,
                         1234U, 42.5f, "fold_pct");
    assert(n == MAVLINK_NVF_FRAME_LEN);
    assert(frame[0] == 0xFEU);
    assert(frame[1] == 18U);
    assert(frame[2] == 7U);
    assert(frame[3] == 1U);
    assert(frame[4] == 191U);
    assert(frame[5] == 251U);
    assert(frame[6] == (uint8_t)(1234U & 0xFFU));
    assert(frame[7] == (uint8_t)((1234U >> 8) & 0xFFU));
    conf.f = 42.5f;
    assert(memcmp(&frame[10], conf.b, 4) == 0);
    assert(memcmp(&frame[14], "fold_pct\0\0", 10) == 0);

    for (i = 1U; i < 24U; ++i) {
        crc = crc_accumulate(frame[i], crc);
    }
    crc = crc_accumulate((uint8_t)MAVLINK_NVF_CRC_EXTRA, crc);
    assert(frame[24] == (uint8_t)(crc & 0xFFU));
    assert(frame[25] == (uint8_t)((crc >> 8) & 0xFFU));
}

static void test_rejects_small_buffer(void)
{
    uint8_t tiny[8];
    assert(mavlink_nvf_pack(tiny, sizeof(tiny), 0, 1, 1, 0, 0.f, "x") == 0U);
}

int main(void)
{
    test_pack_basic();
    test_rejects_small_buffer();
    printf("test_mavlink_nvf: OK\n");
    return 0;
}
