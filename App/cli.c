#include "cli.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "encoder.h"
#include "motor_backend.h"
#include "servo_bus.h"

#ifndef USB_CDC_DEBUG
#define USB_CDC_DEBUG 0
#endif

#if USB_CDC_DEBUG
#include "usbd_cdc_if.h"
#include "usb_device.h"
extern USBD_HandleTypeDef hUsbDeviceFS;
#endif

#define CLI_LINE_SIZE        64U
#define CLI_UART_TIMEOUT_MS  20U
#define CLI_RX_RING_SIZE     128U

static UART_HandleTypeDef *s_huart;
static nvm_blob_t s_params;
static char s_line[CLI_LINE_SIZE];
static size_t s_line_length;
static bool s_have_a;
static bool s_have_b;
static bool s_calibrated;
static int16_t s_motor_speed;
static bool s_manual_override;
static uint32_t s_manual_started_ms;

static volatile uint8_t s_rx_ring[CLI_RX_RING_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static bool s_telem_on;
static uint32_t s_telem_last_ms;
static char s_telem_output[160];

static void write_text(const char *text);

static bool console_tx_ready(void)
{
#if USB_CDC_DEBUG
    USBD_CDC_HandleTypeDef *hcdc =
        (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
    return (hcdc != NULL) && (hcdc->TxState == 0U);
#else
    return (s_huart != NULL)
        && (s_huart->gState == HAL_UART_STATE_READY);
#endif
}

static void console_write(const uint8_t *data, uint16_t len)
{
#if USB_CDC_DEBUG
    /* CDC_Transmit_FS does not copy: Buf must stay valid until TxState clears. */
    static uint8_t s_usb_tx[64];
    uint16_t off = 0U;

    while (off < len) {
        uint16_t chunk = (uint16_t)(len - off);
        uint32_t start;
        USBD_CDC_HandleTypeDef *hcdc;

        if (chunk > sizeof(s_usb_tx)) {
            chunk = (uint16_t)sizeof(s_usb_tx);
        }
        (void)memcpy(s_usb_tx, data + off, chunk);

        start = HAL_GetTick();
        while (CDC_Transmit_FS(s_usb_tx, chunk) == USBD_BUSY) {
            if ((uint32_t)(HAL_GetTick() - start) > 100U) {
                return;
            }
        }
        /* Wait until USB finished reading s_usb_tx before next chunk / return. */
        start = HAL_GetTick();
        for (;;) {
            hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
            if ((hcdc == NULL) || (hcdc->TxState == 0U)) {
                break;
            }
            if ((uint32_t)(HAL_GetTick() - start) > 100U) {
                return;
            }
        }
        off = (uint16_t)(off + chunk);
    }
#else
    if (s_huart != NULL) {
        (void)HAL_UART_Transmit(s_huart, (uint8_t *)data, len,
                                CLI_UART_TIMEOUT_MS);
    }
#endif
}

static void emit_telem_line(uint32_t now_ms)
{
    app_status_t st;
    uint16_t pwm;
    int n;

    if (!console_tx_ready()) {
        return; /* drop frame */
    }
    app_get_status(&st);
    /* Always report filtered command pulse when known; raw stays in status. */
    pwm = st.pwm_us;
    n = snprintf(s_telem_output, sizeof(s_telem_output),
                 "T,%lu,%u,%ld,%ld,%ld,%d,%d,%d,%d,%d,%d\r\n",
                 (unsigned long)now_ms,
                 (unsigned int)pwm,
                 (long)st.current,
                 (long)st.target,
                 (long)(st.target - st.current),
                 (int)st.speed_cmd,
                 (int)st.speed_out,
                 st.hold ? 1 : 0,
                 st.settled ? 1 : 0,
                 (int)st.last_dir,
                 st.servo_fault ? 1 : 0);
    if ((n > 0) && ((size_t)n < sizeof(s_telem_output))) {
#if USB_CDC_DEBUG
        console_write((const uint8_t *)s_telem_output, (uint16_t)n);
#else
        (void)HAL_UART_Transmit_IT(s_huart, (uint8_t *)s_telem_output,
                                   (uint16_t)n);
#endif
    }
}

void cli_telem_tick(uint32_t now_ms)
{
    if (!s_telem_on) {
        return;
    }
    if ((uint32_t)(now_ms - s_telem_last_ms) < 10U) {
        return;
    }
    s_telem_last_ms = now_ms;
    emit_telem_line(now_ms);
}

static void write_text(const char *text)
{
    if (text != NULL) {
        console_write((const uint8_t *)text, (uint16_t)strlen(text));
    }
}

static void show_calibration(void)
{
    char output[96];

    (void)snprintf(output, sizeof(output),
                   "a=%ld b=%ld pwm=%u..%u dz=%ld kp=%ld vmax=%ld cruise=%ld %s\r\n",
                   (long)s_params.count_a, (long)s_params.count_b,
                   (unsigned int)s_params.pwm_min_us,
                   (unsigned int)s_params.pwm_max_us,
                   (long)s_params.deadzone, (long)s_params.kp,
                   (long)s_params.vmax, (long)s_params.cruise_err,
                   s_calibrated ? "calibrated" : "NOT SAVED");
    write_text(output);
}

static void capture_endpoint(bool endpoint_a)
{
    int32_t count;
    char output[40];

    if (!encoder_read_count(&count)) {
        write_text("ERR encoder error\r\n");
        return;
    }

    if (endpoint_a) {
        s_params.count_a = count;
        s_have_a = true;
    } else {
        s_params.count_b = count;
        s_have_b = true;
    }
    s_calibrated = false;
    (void)snprintf(output, sizeof(output), "OK cal %c=%ld\r\n",
                   endpoint_a ? 'a' : 'b', (long)count);
    write_text(output);
}

static void save_calibration(void)
{
    if (!s_have_a || !s_have_b) {
        write_text("ERR capture cal a and cal b first\r\n");
        return;
    }
    if (!nvm_save(&s_params)) {
        write_text("ERR Flash save failed\r\n");
        return;
    }

    s_calibrated = true;
    app_reload_params();
    write_text("OK saved\r\n");
}

static void show_status(void)
{
    int32_t count;
    char output[128];
    app_status_t st;

    /* Ensure reply starts on a new line (some hosts concatenate echo + reply). */
    write_text("\r\n");
    app_get_status(&st);
    if (encoder_read_count(&count)) {
        (void)snprintf(output, sizeof(output),
                       "count=%ld motor=%d cal=%s enc_fail=0 "
                       "pwm=%u raw=%lu irq=%lu age=%lums hold=%d "
                       "tgt=%ld spd=%d/%d fault=%d backend=%s\r\n",
                       (long)count, (int)s_motor_speed,
                       s_calibrated ? "yes" : "no",
                       (unsigned int)st.pwm_us,
                       (unsigned long)st.pwm_raw_us,
                       (unsigned long)st.pwm_irq,
                       (unsigned long)st.pwm_age_ms,
                       st.hold ? 1 : 0, (long)st.target,
                       (int)st.speed_cmd, (int)st.speed_out,
                       st.servo_fault ? 1 : 0, MOTOR_BACKEND_ID);
    } else {
        (void)snprintf(output, sizeof(output),
                       "count=ERR motor=%d cal=%s enc_fail=%u "
                       "pwm=%u raw=%lu irq=%lu age=%lums hold=%d "
                       "tgt=%ld spd=%d/%d fault=%d backend=%s\r\n",
                       (int)s_motor_speed, s_calibrated ? "yes" : "no",
                       (unsigned int)encoder_fail_streak(),
                       (unsigned int)st.pwm_us,
                       (unsigned long)st.pwm_raw_us,
                       (unsigned long)st.pwm_irq,
                       (unsigned long)st.pwm_age_ms,
                       st.hold ? 1 : 0, (long)st.target,
                       (int)st.speed_cmd, (int)st.speed_out,
                       st.servo_fault ? 1 : 0, MOTOR_BACKEND_ID);
    }
    write_text(output);
}

static void set_motor(const char *argument)
{
    char *end;
    long value;

    errno = 0;
    value = strtol(argument, &end, 10);
    if (argument == end) {
        write_text("ERR motor speed must be -1000..1000\r\n");
        return;
    }
    while (*end == ' ') {
        ++end;
    }
    if ((*end != '\0') || (errno == ERANGE)
        || (value < -SERVO_BUS_SPEED_MAX)
        || (value > SERVO_BUS_SPEED_MAX)
        || (value < INT16_MIN) || (value > INT16_MAX)) {
        write_text("ERR motor speed must be -1000..1000\r\n");
        return;
    }

    if (servo_bus_set_motor_speed_immediate((int16_t)value) != 0) {
        write_text("ERR servo bus\r\n");
        return;
    }
    s_motor_speed = (int16_t)value;
    s_manual_override = true;
    s_manual_started_ms = HAL_GetTick();
    write_text("OK motor\r\n");
}

static void set_param(const char *argument)
{
    char name[16];
    char *end;
    long value;
    int n;

    while (*argument == ' ') {
        ++argument;
    }
    if (strcmp(argument, "save") == 0) {
        if (!nvm_save(&s_params)) {
            write_text("ERR Flash save failed\r\n");
            return;
        }
        s_calibrated = true;
        app_reload_params();
        write_text("OK set saved\r\n");
        return;
    }

    n = 0;
    while ((argument[n] != '\0') && (argument[n] != ' ') && (n < 15)) {
        name[n] = argument[n];
        ++n;
    }
    name[n] = '\0';
    argument += n;
    while (*argument == ' ') {
        ++argument;
    }
    if (*argument == '\0') {
        write_text("ERR set dz|kp|vmax|cruise <n> | set save\r\n");
        return;
    }

    errno = 0;
    value = strtol(argument, &end, 10);
    while (*end == ' ') {
        ++end;
    }
    if ((argument == end) || (*end != '\0') || (errno == ERANGE)) {
        write_text("ERR set value\r\n");
        return;
    }

    if (strcmp(name, "dz") == 0) {
        if ((value < 1) || (value > 5000)) {
            write_text("ERR dz 1..5000\r\n");
            return;
        }
        s_params.deadzone = (int32_t)value;
    } else if (strcmp(name, "kp") == 0) {
        if ((value < 1) || (value > 5000)) {
            write_text("ERR kp 1..5000\r\n");
            return;
        }
        s_params.kp = (int32_t)value;
    } else if (strcmp(name, "vmax") == 0) {
        if ((value < 80) || (value > SERVO_BUS_SPEED_MAX)) {
            write_text("ERR vmax 80..1000\r\n");
            return;
        }
        s_params.vmax = (int32_t)value;
    } else if (strcmp(name, "cruise") == 0) {
        if ((value < 100) || (value > 20000)) {
            write_text("ERR cruise 100..20000\r\n");
            return;
        }
        s_params.cruise_err = (int32_t)value;
    } else {
        write_text("ERR set dz|kp|vmax|cruise <n> | set save\r\n");
        return;
    }

    app_reload_params();
    write_text("OK set (set save to Flash)\r\n");
}

static void execute_line(char *line)
{
    while (*line == ' ') {
        ++line;
    }

    if (strcmp(line, "cal a") == 0) {
        capture_endpoint(true);
    } else if (strcmp(line, "cal b") == 0) {
        capture_endpoint(false);
    } else if (strcmp(line, "cal save") == 0) {
        save_calibration();
    } else if (strcmp(line, "cal show") == 0) {
        show_calibration();
    } else if (strcmp(line, "status") == 0) {
        show_status();
    } else if (strncmp(line, "motor ", 6U) == 0) {
        set_motor(line + 6U);
    } else if (strncmp(line, "set ", 4U) == 0) {
        set_param(line + 4U);
    } else if (strcmp(line, "hold") == 0) {
        if (servo_bus_motor_stop() == 0) {
            s_motor_speed = 0;
            s_manual_override = false;
            write_text("OK hold\r\n");
        } else {
            write_text("ERR servo bus\r\n");
        }
    } else if (strcmp(line, "telem on") == 0) {
        s_telem_on = true;
        s_telem_last_ms = 0U; /* force next tick */
        write_text("OK telem on\r\n");
    } else if (strcmp(line, "telem off") == 0) {
        s_telem_on = false;
        write_text("OK telem off\r\n");
    } else if (strcmp(line, "telem") == 0) {
        write_text(s_telem_on ? "telem=on\r\n" : "telem=off\r\n");
    } else if (strcmp(line, "backend") == 0) {
        char output[32];
        (void)snprintf(output, sizeof(output), "backend=%s\r\n", MOTOR_BACKEND_ID);
        write_text(output);
    } else if (strcmp(line, "help") == 0) {
        write_text("\r\ncal a|b|save|show; set dz|kp|vmax|cruise <n>|save; "
                   "status; backend; motor <spd>; hold; telem on|off; help\r\n");
    } else if (*line != '\0') {
        write_text("\r\nERR unknown command (try help)\r\n");
    }
}

static bool ring_pop(uint8_t *out)
{
    uint16_t tail;
    uint16_t head;

    tail = s_rx_tail;
    head = s_rx_head;
    if (tail == head) {
        return false;
    }
    *out = s_rx_ring[tail];
    s_rx_tail = (uint16_t)((tail + 1U) % CLI_RX_RING_SIZE);
    return true;
}

void cli_uart_rx_irq_byte(uint8_t byte)
{
    uint16_t next = (uint16_t)((s_rx_head + 1U) % CLI_RX_RING_SIZE);

    if (next == s_rx_tail) {
        return; /* drop on overflow */
    }
    s_rx_ring[s_rx_head] = byte;
    s_rx_head = next;
}

void cli_init(UART_HandleTypeDef *huart)
{
    char boot_line[48];

    s_huart = huart;
    s_line_length = 0U;
    s_motor_speed = 0;
    s_manual_override = false;
    s_manual_started_ms = 0U;
    s_telem_on = false;
    s_telem_last_ms = 0U;
    s_rx_head = 0U;
    s_rx_tail = 0U;
    if (nvm_load(&s_params)) {
        s_calibrated = true;
#if USB_CDC_DEBUG
        write_text("Fold CLI ready (USB CDC); type help\r\n");
#else
        write_text("Fold CLI ready; type help\r\n");
#endif
        (void)snprintf(boot_line, sizeof(boot_line), "boot backend=%s\r\n",
                       MOTOR_BACKEND_ID);
        write_text(boot_line);
        write_text("NVM calibration loaded\r\n");
    } else {
        nvm_defaults(&s_params);
        /* Factory stroke defaults: PWM closed-loop works without cal a/b/save. */
        s_calibrated = true;
#if USB_CDC_DEBUG
        write_text("Fold CLI ready (USB CDC); type help\r\n");
#else
        write_text("Fold CLI ready; type help\r\n");
#endif
        (void)snprintf(boot_line, sizeof(boot_line), "boot backend=%s\r\n",
                       MOTOR_BACKEND_ID);
        write_text(boot_line);
        write_text("Using default a=0 b=24000 (optional: cal a/b/save)\r\n");
    }
    s_have_a = true;
    s_have_b = true;

    /* huart NULL: NVM/params only; console may be USB CDC. */
    if (s_huart == NULL) {
        return;
    }

    /* IRQ RX so encoder Modbus blocking cannot drop CLI bytes. */
    HAL_NVIC_SetPriority(USART3_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
    __HAL_UART_ENABLE_IT(s_huart, UART_IT_RXNE);
}

void cli_poll(void)
{
    uint8_t byte;

#if USB_CDC_DEBUG
    while (CDC_GetRxBufferBytesAvailable_FS() > 0U) {
        if (CDC_ReadRxBuffer_FS(&byte, 1U) != USB_CDC_RX_BUFFER_OK) {
            break;
        }
        if ((byte >= 0x20U) && (byte <= 0x7EU)) {
            console_write(&byte, 1U);
        }
        if ((byte == '\r') || (byte == '\n')) {
            if (s_line_length == 0U) {
                continue;
            }
            s_line[s_line_length] = '\0';
            write_text("\r\n");
            execute_line(s_line);
            s_line_length = 0U;
            continue;
        }
        if ((byte < 0x20U) || (byte > 0x7EU)) {
            continue;
        }
        if (s_line_length + 1U < sizeof(s_line)) {
            s_line[s_line_length++] = (char)byte;
        } else {
            s_line_length = 0U;
            write_text("\r\nERR line too long\r\n");
        }
    }
    return;
#else
    if (s_huart == NULL) {
        return;
    }

    if (__HAL_UART_GET_FLAG(s_huart, UART_FLAG_ORE)
        || __HAL_UART_GET_FLAG(s_huart, UART_FLAG_NE)
        || __HAL_UART_GET_FLAG(s_huart, UART_FLAG_FE)
        || __HAL_UART_GET_FLAG(s_huart, UART_FLAG_PE)) {
        __HAL_UART_CLEAR_OREFLAG(s_huart);
        s_huart->ErrorCode = HAL_UART_ERROR_NONE;
    }

    while (ring_pop(&byte)) {
        if ((byte >= 0x20U) && (byte <= 0x7EU)) {
            (void)HAL_UART_Transmit(s_huart, &byte, 1U, 5U);
        }

        if ((byte == '\r') || (byte == '\n')) {
            if (s_line_length == 0U) {
                continue;
            }
            s_line[s_line_length] = '\0';
            execute_line(s_line);
            s_line_length = 0U;
            continue;
        }
        if ((byte < 0x20U) || (byte > 0x7EU)) {
            continue;
        }
        if (s_line_length + 1U < sizeof(s_line)) {
            s_line[s_line_length++] = (char)byte;
        } else {
            s_line_length = 0U;
            write_text("\r\nERR line too long\r\n");
        }
    }
#endif
}

const nvm_blob_t *cli_get_params(void)
{
    return &s_params;
}

bool cli_is_calibrated(void)
{
    return s_calibrated;
}

bool cli_manual_override_active(uint32_t now_ms)
{
#if CLI_MANUAL_TIMEOUT_MS != 0U
    if (s_manual_override
        && ((uint32_t)(now_ms - s_manual_started_ms) >= CLI_MANUAL_TIMEOUT_MS)) {
        s_manual_override = false;
        s_motor_speed = 0;
        (void)servo_bus_motor_stop();
    }
#else
    (void)now_ms;
#endif
    return s_manual_override;
}

int16_t cli_manual_speed(void)
{
    return s_motor_speed;
}
