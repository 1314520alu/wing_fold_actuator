#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAVLINK_NVF_MSG_ID     251U
#define MAVLINK_NVF_CRC_EXTRA  170U
#define MAVLINK_NVF_NAME_LEN   10U
/* STX + len + seq + sys + comp + msgid + payload(18) + crc(2) */
#define MAVLINK_NVF_FRAME_LEN  26U

/*
 * Pack MAVLink v1 NAMED_VALUE_FLOAT into out[MAVLINK_NVF_FRAME_LEN].
 * name is truncated/padded to 10 bytes (NUL-filled).
 * Returns frame length (26) or 0 on error.
 */
size_t mavlink_nvf_pack(uint8_t *out,
                        size_t out_cap,
                        uint8_t seq,
                        uint8_t sysid,
                        uint8_t compid,
                        uint32_t time_boot_ms,
                        float value,
                        const char *name);

#ifdef __cplusplus
}
#endif
