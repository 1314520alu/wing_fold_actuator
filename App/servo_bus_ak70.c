#include "servo_bus.h"

#include <stdbool.h>
#include <string.h>

/* AK70-10 / CubeMars V1.32 servo UART: 02 len payload crc_hi crc_lo 03.
 * Not the AK3.0 AA/BB frames (cmd 73), and not the R-LINK USB wrapper. */
#define AK70_FRAME_HEADER           0x02U
#define AK70_FRAME_TAIL             0x03U
/* This AK70 answers COMM_FW_VERSION (cmd 0). MIT firmware ignores GET_VALUES. */
#define AK70_CMD_FW_VERSION         0U
#define AK70_CMD_GET_VALUES         4U
#define AK70_CMD_SET_RPM            8U
#define AK70_SET_RPM_PAYLOAD_LEN    5U
#define AK70_SET_RPM_FRAME_LEN      10U

#ifndef SERVO_BUS_RAMP_STEP
#define SERVO_BUS_RAMP_STEP         40
#endif
#ifndef SERVO_BUS_KEEPALIVE_MS
#define SERVO_BUS_KEEPALIVE_MS      80U
#endif

/* Host 100 = 1 rev/s setting. Shaft must run at 2 rev/s so an 18-turn
 * fold finishes in about 9 s. AK70-10 pole pairs = 21.
 * 2 rps * 60 * 21 = 2520 ERPM at host speed 100. */
#ifndef AK70_HOST_SPEED_PER_RPS
#define AK70_HOST_SPEED_PER_RPS     100
#endif
#ifndef AK70_POLE_PAIRS
#define AK70_POLE_PAIRS             21
#endif
#ifndef AK70_SHAFT_RPS_NUM
#define AK70_SHAFT_RPS_NUM          2
#endif

static UART_HandleTypeDef *s_huart;
static int16_t s_speed_target;
static int16_t s_speed_out;
static bool s_out_valid;
static uint32_t s_last_tx_ms;

static const uint16_t s_crc16_tab[256] = {
    0x0000U, 0x1021U, 0x2042U, 0x3063U, 0x4084U, 0x50A5U, 0x60C6U, 0x70E7U,
    0x8108U, 0x9129U, 0xA14AU, 0xB16BU, 0xC18CU, 0xD1ADU, 0xE1CEU, 0xF1EFU,
    0x1231U, 0x0210U, 0x3273U, 0x2252U, 0x52B5U, 0x4294U, 0x72F7U, 0x62D6U,
    0x9339U, 0x8318U, 0xB37BU, 0xA35AU, 0xD3BDU, 0xC39CU, 0xF3FFU, 0xE3DEU,
    0x2462U, 0x3443U, 0x0420U, 0x1401U, 0x64E6U, 0x74C7U, 0x44A4U, 0x5485U,
    0xA56AU, 0xB54BU, 0x8528U, 0x9509U, 0xE5EEU, 0xF5CFU, 0xC5ACU, 0xD58DU,
    0x3653U, 0x2672U, 0x1611U, 0x0630U, 0x76D7U, 0x66F6U, 0x5695U, 0x46B4U,
    0xB75BU, 0xA77AU, 0x9719U, 0x8738U, 0xF7DFU, 0xE7FEU, 0xD79DU, 0xC7BCU,
    0x48C4U, 0x58E5U, 0x6886U, 0x78A7U, 0x0840U, 0x1861U, 0x2802U, 0x3823U,
    0xC9CCU, 0xD9EDU, 0xE98EU, 0xF9AFU, 0x8948U, 0x9969U, 0xA90AU, 0xB92BU,
    0x5AF5U, 0x4AD4U, 0x7AB7U, 0x6A96U, 0x1A71U, 0x0A50U, 0x3A33U, 0x2A12U,
    0xDBFDU, 0xCBDCU, 0xFBBFU, 0xEB9EU, 0x9B79U, 0x8B58U, 0xBB3BU, 0xAB1AU,
    0x6CA6U, 0x7C87U, 0x4CE4U, 0x5CC5U, 0x2C22U, 0x3C03U, 0x0C60U, 0x1C41U,
    0xEDAEU, 0xFD8FU, 0xCDECU, 0xDDCDU, 0xAD2AU, 0xBD0BU, 0x8D68U, 0x9D49U,
    0x7E97U, 0x6EB6U, 0x5ED5U, 0x4EF4U, 0x3E13U, 0x2E32U, 0x1E51U, 0x0E70U,
    0xFF9FU, 0xEFBEU, 0xDFDDU, 0xCFFCU, 0xBF1BU, 0xAF3AU, 0x9F59U, 0x8F78U,
    0x9188U, 0x81A9U, 0xB1CAU, 0xA1EBU, 0xD10CU, 0xC12DU, 0xF14EU, 0xE16FU,
    0x1080U, 0x00A1U, 0x30C2U, 0x20E3U, 0x5004U, 0x4025U, 0x7046U, 0x6067U,
    0x83B9U, 0x9398U, 0xA3FBU, 0xB3DAU, 0xC33DU, 0xD31CU, 0xE37FU, 0xF35EU,
    0x02B1U, 0x1290U, 0x22F3U, 0x32D2U, 0x4235U, 0x5214U, 0x6277U, 0x7256U,
    0xB5EAU, 0xA5CBU, 0x95A8U, 0x8589U, 0xF56EU, 0xE54FU, 0xD52CU, 0xC50DU,
    0x34E2U, 0x24C3U, 0x14A0U, 0x0481U, 0x7466U, 0x6447U, 0x5424U, 0x4405U,
    0xA7DBU, 0xB7FAU, 0x8799U, 0x97B8U, 0xE75FU, 0xF77EU, 0xC71DU, 0xD73CU,
    0x26D3U, 0x36F2U, 0x0691U, 0x16B0U, 0x6657U, 0x7676U, 0x4615U, 0x5634U,
    0xD94CU, 0xC96DU, 0xF90EU, 0xE92FU, 0x99C8U, 0x89E9U, 0xB98AU, 0xA9ABU,
    0x5844U, 0x4865U, 0x7806U, 0x6827U, 0x18C0U, 0x08E1U, 0x3882U, 0x28A3U,
    0xCB7DU, 0xDB5CU, 0xEB3FU, 0xFB1EU, 0x8BF9U, 0x9BD8U, 0xABBBU, 0xBB9AU,
    0x4A75U, 0x5A54U, 0x6A37U, 0x7A16U, 0x0AF1U, 0x1AD0U, 0x2AB3U, 0x3A92U,
    0xFD2EU, 0xED0FU, 0xDD6CU, 0xCD4DU, 0xBDAAU, 0xAD8BU, 0x9DE8U, 0x8DC9U,
    0x7C26U, 0x6C07U, 0x5C64U, 0x4C45U, 0x3CA2U, 0x2C83U, 0x1CE0U, 0x0CC1U,
    0xEF1FU, 0xFF3EU, 0xCF5DU, 0xDF7CU, 0xAF9BU, 0xBFBAU, 0x8FD9U, 0x9FF8U,
    0x6E17U, 0x7E36U, 0x4E55U, 0x5E74U, 0x2E93U, 0x3EB2U, 0x0ED1U, 0x1EF0U,
};

static uint16_t ak70_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t cksum = 0U;

    for (uint16_t i = 0U; i < len; ++i) {
        cksum = (uint16_t)(s_crc16_tab[((cksum >> 8) ^ data[i]) & 0xFFU]
                           ^ (cksum << 8));
    }
    return cksum;
}

static int16_t clamp_speed(int16_t speed)
{
    if (speed > SERVO_BUS_SPEED_MAX) {
        return SERVO_BUS_SPEED_MAX;
    }
    if (speed < -SERVO_BUS_SPEED_MAX) {
        return -SERVO_BUS_SPEED_MAX;
    }
    return speed;
}

static int8_t speed_sign(int16_t speed)
{
    if (speed > 0) {
        return 1;
    }
    if (speed < 0) {
        return -1;
    }
    return 0;
}

static int32_t speed_to_erpm(int16_t speed)
{
    return ((int32_t)speed * 60 * (int32_t)AK70_POLE_PAIRS
            * (int32_t)AK70_SHAFT_RPS_NUM)
           / (int32_t)AK70_HOST_SPEED_PER_RPS;
}

static int servo_bus_transmit(const uint8_t *frame, uint16_t len)
{
    if (s_huart == NULL) {
        return -1;
    }

    if (HAL_UART_Transmit(s_huart, (uint8_t *)frame, len, 20U) != HAL_OK) {
        return -1;
    }

    s_last_tx_ms = HAL_GetTick();
    return 0;
}

static int transmit_speed(int16_t speed)
{
    uint8_t payload[AK70_SET_RPM_PAYLOAD_LEN];
    uint8_t frame[AK70_SET_RPM_FRAME_LEN];
    const int32_t erpm = speed_to_erpm(speed);
    uint16_t crc;

    payload[0] = AK70_CMD_SET_RPM;
    payload[1] = (uint8_t)((uint32_t)erpm >> 24);
    payload[2] = (uint8_t)((uint32_t)erpm >> 16);
    payload[3] = (uint8_t)((uint32_t)erpm >> 8);
    payload[4] = (uint8_t)((uint32_t)erpm);

    crc = ak70_crc16(payload, AK70_SET_RPM_PAYLOAD_LEN);

    frame[0] = AK70_FRAME_HEADER;
    frame[1] = AK70_SET_RPM_PAYLOAD_LEN;
    (void)memcpy(&frame[2], payload, AK70_SET_RPM_PAYLOAD_LEN);
    frame[7] = (uint8_t)(crc >> 8);
    frame[8] = (uint8_t)(crc & 0xFFU);
    frame[9] = AK70_FRAME_TAIL;

    return servo_bus_transmit(frame, AK70_SET_RPM_FRAME_LEN);
}

void servo_bus_init(UART_HandleTypeDef *huart, uint8_t servo_id)
{
    (void)servo_id;
    s_huart = huart;
    s_speed_target = 0;
    s_speed_out = 0;
    s_out_valid = false;
    s_last_tx_ms = 0U;
}

int servo_bus_set_motor_speed(int16_t speed)
{
    if (s_huart == NULL) {
        return -1;
    }
    s_speed_target = clamp_speed(speed);
    return 0;
}

int servo_bus_set_motor_speed_immediate(int16_t speed)
{
    const int16_t clamped = clamp_speed(speed);

    if (s_huart == NULL) {
        return -1;
    }

    s_speed_target = clamped;

    if ((speed_sign(s_speed_out) != 0)
        && (speed_sign(clamped) != 0)
        && (speed_sign(s_speed_out) != speed_sign(clamped))) {
        return 0;
    }

    s_speed_out = clamped;
    s_out_valid = true;
    return transmit_speed(s_speed_out);
}

int servo_bus_motor_stop(void)
{
    s_speed_target = 0;
    s_speed_out = 0;
    s_out_valid = true;
    return transmit_speed(0);
}

int servo_bus_ramp_update(void)
{
    int16_t next = s_speed_out;
    const int16_t step = (int16_t)SERVO_BUS_RAMP_STEP;
    const uint32_t now_ms = HAL_GetTick();

    if (s_huart == NULL) {
        return -1;
    }

    if (s_speed_out < s_speed_target) {
        next = (int16_t)(s_speed_out + step);
        if (next > s_speed_target) {
            next = s_speed_target;
        }
    } else if (s_speed_out > s_speed_target) {
        next = (int16_t)(s_speed_out - step);
        if (next < s_speed_target) {
            next = s_speed_target;
        }
    } else if (s_out_valid) {
        if ((s_speed_out != 0)
            && ((uint32_t)(now_ms - s_last_tx_ms) >= SERVO_BUS_KEEPALIVE_MS)) {
            if (transmit_speed(s_speed_out) != 0) {
                return -1;
            }
            return 1;
        }
        return 0;
    }

    s_speed_out = next;
    s_out_valid = true;
    if (transmit_speed(s_speed_out) != 0) {
        return -1;
    }
    return 1;
}

int16_t servo_bus_get_output_speed(void)
{
    return s_speed_out;
}

static void uart_flush_rx(void)
{
    uint8_t dump;
    uint32_t guard = 0U;

    if ((s_huart == NULL) || (s_huart->Instance == NULL)) {
        return;
    }
    while ((__HAL_UART_GET_FLAG(s_huart, UART_FLAG_RXNE) != RESET)
           && (guard < 64U)) {
        dump = (uint8_t)(s_huart->Instance->DR & 0xFFU);
        (void)dump;
        ++guard;
    }
}

static int16_t be_i16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static int32_t be_i32(const uint8_t *p)
{
    return (int32_t)(((uint32_t)p[0] << 24)
                     | ((uint32_t)p[1] << 16)
                     | ((uint32_t)p[2] << 8)
                     | (uint32_t)p[3]);
}

static int recv_byte(uint8_t *byte, uint32_t deadline_ms)
{
    while ((int32_t)(deadline_ms - HAL_GetTick()) > 0) {
        if (HAL_UART_Receive(s_huart, byte, 1U, 5U) == HAL_OK) {
            return 0;
        }
    }
    return -1;
}

/* Scan for one valid 02 frame. payload_len includes the command byte. */
static int recv_frame(uint8_t *payload, uint8_t *payload_len, uint32_t timeout_ms)
{
    const uint32_t deadline = HAL_GetTick() + timeout_ms;
    uint8_t byte = 0U;

    if ((payload == NULL) || (payload_len == NULL)) {
        return -1;
    }

    while (recv_byte(&byte, deadline) == 0) {
        uint8_t len = 0U;
        uint8_t raw[80];
        uint16_t crc;
        uint16_t rx_crc;

        if (byte != AK70_FRAME_HEADER) {
            continue;
        }
        if (recv_byte(&len, deadline) != 0) {
            return -1;
        }
        if ((len < 1U) || (len > 78U)) {
            continue;
        }
        for (uint8_t i = 0U; i < (uint8_t)(len + 3U); ++i) {
            if (recv_byte(&raw[i], deadline) != 0) {
                return -1;
            }
        }
        if (raw[len + 2U] != AK70_FRAME_TAIL) {
            continue;
        }
        crc = ak70_crc16(raw, len);
        rx_crc = (uint16_t)(((uint16_t)raw[len] << 8) | raw[len + 1U]);
        if (crc != rx_crc) {
            continue;
        }
        (void)memcpy(payload, raw, len);
        *payload_len = len;
        return 0;
    }
    return -1;
}

int servo_bus_read_feedback(servo_bus_feedback_t *out)
{
    uint8_t payload[1] = { AK70_CMD_FW_VERSION };
    uint8_t frame[6];
    uint8_t body[80];
    uint8_t len = 0U;
    uint16_t crc;

    if ((s_huart == NULL) || (out == NULL)) {
        return -1;
    }

    crc = ak70_crc16(payload, 1U);
    frame[0] = AK70_FRAME_HEADER;
    frame[1] = 1U;
    frame[2] = AK70_CMD_FW_VERSION;
    frame[3] = (uint8_t)(crc >> 8);
    frame[4] = (uint8_t)(crc & 0xFFU);
    frame[5] = AK70_FRAME_TAIL;

    uart_flush_rx();
    if (servo_bus_transmit(frame, 6U) != 0) {
        return -1;
    }
    if (recv_frame(body, &len, 60U) != 0) {
        return -1;
    }

    out->mos_x10 = 0;
    out->rpm = 0;
    out->vin_x10 = 0;
    out->fault = 0U;
    out->has_values = 0U;

    if (body[0] == AK70_CMD_GET_VALUES) {
        if (len < 54U) {
            return -1;
        }
        /* After cmd: mos, mtemp, currents, duty, rpm, vin, reserved[24], fault. */
        out->mos_x10 = be_i16(&body[1]);
        out->rpm = be_i32(&body[23]);
        out->vin_x10 = be_i16(&body[27]);
        out->fault = body[53];
        out->has_values = 1U;
        return 0;
    }

    /* MIT TEST V2 replies to cmd 0 with a short firmware frame and ignores cmd 4. */
    if (body[0] != AK70_CMD_FW_VERSION) {
        return -1;
    }
    return 0;
}
