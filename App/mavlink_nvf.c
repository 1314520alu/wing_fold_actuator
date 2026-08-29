#include "mavlink_nvf.h"

#include <string.h>

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

static uint16_t crc_calculate(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;

    for (i = 0U; i < len; ++i) {
        crc = crc_accumulate(buf[i], crc);
    }
    return crc;
}

size_t mavlink_nvf_pack(uint8_t *out,
                        size_t out_cap,
                        uint8_t seq,
                        uint8_t sysid,
                        uint8_t compid,
                        uint32_t time_boot_ms,
                        float value,
                        const char *name)
{
    uint8_t payload[18];
    uint16_t crc;
    size_t i;
    union {
        float f;
        uint32_t u;
        uint8_t b[4];
    } conf;

    if ((out == NULL) || (out_cap < MAVLINK_NVF_FRAME_LEN) || (name == NULL)) {
        return 0U;
    }

    payload[0] = (uint8_t)(time_boot_ms & 0xFFU);
    payload[1] = (uint8_t)((time_boot_ms >> 8) & 0xFFU);
    payload[2] = (uint8_t)((time_boot_ms >> 16) & 0xFFU);
    payload[3] = (uint8_t)((time_boot_ms >> 24) & 0xFFU);

    conf.f = value;
    payload[4] = conf.b[0];
    payload[5] = conf.b[1];
    payload[6] = conf.b[2];
    payload[7] = conf.b[3];

    (void)memset(&payload[8], 0, MAVLINK_NVF_NAME_LEN);
    for (i = 0U; (i < MAVLINK_NVF_NAME_LEN) && (name[i] != '\0'); ++i) {
        payload[8U + i] = (uint8_t)name[i];
    }

    out[0] = 0xFEU;
    out[1] = 18U;
    out[2] = seq;
    out[3] = sysid;
    out[4] = compid;
    out[5] = (uint8_t)MAVLINK_NVF_MSG_ID;
    (void)memcpy(&out[6], payload, sizeof(payload));

    /* CRC over len..payload (skip STX), then crc_extra */
    crc = crc_calculate(&out[1], (uint16_t)(5U + 18U));
    crc = crc_accumulate((uint8_t)MAVLINK_NVF_CRC_EXTRA, crc);
    out[24] = (uint8_t)(crc & 0xFFU);
    out[25] = (uint8_t)((crc >> 8) & 0xFFU);
    return MAVLINK_NVF_FRAME_LEN;
}
