#include "cli.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "encoder.h"
#include "servo_bus.h"

#define CLI_LINE_SIZE        64U
#define CLI_UART_TIMEOUT_MS  20U

static UART_HandleTypeDef *s_huart;
static nvm_blob_t s_params;
static char s_line[CLI_LINE_SIZE];
static size_t s_line_length;
static bool s_have_a;
static bool s_have_b;
static bool s_calibrated;
static int16_t s_motor_speed;

static void write_text(const char *text)
{
    if ((s_huart != NULL) && (text != NULL)) {
        (void)HAL_UART_Transmit(s_huart, (uint8_t *)text,
                                (uint16_t)strlen(text),
                                CLI_UART_TIMEOUT_MS);
    }
}

static void show_calibration(void)
{
    char output[96];

    (void)snprintf(output, sizeof(output),
                   "a=%ld b=%ld pwm=%u..%u dz=%ld kp=%ld vmax=%ld %s\r\n",
                   (long)s_params.count_a, (long)s_params.count_b,
                   (unsigned int)s_params.pwm_min_us,
                   (unsigned int)s_params.pwm_max_us,
                   (long)s_params.deadzone, (long)s_params.kp,
                   (long)s_params.vmax,
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
    write_text("OK saved\r\n");
}

static void show_status(void)
{
    int32_t count;
    char output[80];

    if (encoder_read_count(&count)) {
        (void)snprintf(output, sizeof(output),
                       "count=%ld motor=%d cal=%s enc_fail=0\r\n",
                       (long)count, (int)s_motor_speed,
                       s_calibrated ? "yes" : "no");
    } else {
        (void)snprintf(output, sizeof(output),
                       "count=ERR motor=%d cal=%s enc_fail=%u\r\n",
                       (int)s_motor_speed, s_calibrated ? "yes" : "no",
                       (unsigned int)encoder_fail_streak());
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
        write_text("ERR motor speed must be -500..500\r\n");
        return;
    }
    while (*end == ' ') {
        ++end;
    }
    if ((*end != '\0') || (errno == ERANGE)
        || (value < -SERVO_BUS_SPEED_MAX)
        || (value > SERVO_BUS_SPEED_MAX)
        || (value < INT16_MIN) || (value > INT16_MAX)) {
        write_text("ERR motor speed must be -500..500\r\n");
        return;
    }

    if (servo_bus_set_motor_speed((int16_t)value) != 0) {
        write_text("ERR servo bus\r\n");
        return;
    }
    s_motor_speed = (int16_t)value;
    write_text("OK motor\r\n");
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
    } else if (strcmp(line, "hold") == 0) {
        if (servo_bus_motor_stop() == 0) {
            s_motor_speed = 0;
            write_text("OK hold\r\n");
        } else {
            write_text("ERR servo bus\r\n");
        }
    } else if (strcmp(line, "help") == 0) {
        write_text("cal a|b|save|show; status; motor <spd>; hold; help\r\n");
    } else if (*line != '\0') {
        write_text("ERR unknown command (try help)\r\n");
    }
}

void cli_init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    s_line_length = 0U;
    s_motor_speed = 0;
    s_calibrated = nvm_load(&s_params);
    if (!s_calibrated) {
        nvm_defaults(&s_params);
    }
    s_have_a = s_calibrated;
    s_have_b = s_calibrated;
    write_text("Fold CLI ready; type help\r\n");
    if (!s_calibrated) {
        write_text("WARN calibration required\r\n");
    }
}

void cli_poll(void)
{
    uint8_t byte;

    if ((s_huart == NULL)
        || (HAL_UART_Receive(s_huart, &byte, 1U, 0U) != HAL_OK)) {
        return;
    }

    if (byte == '\r') {
        return;
    }
    if (byte == '\n') {
        s_line[s_line_length] = '\0';
        execute_line(s_line);
        s_line_length = 0U;
        return;
    }
    if (s_line_length + 1U < sizeof(s_line)) {
        s_line[s_line_length++] = (char)byte;
    } else {
        s_line_length = 0U;
        write_text("ERR line too long\r\n");
    }
}

const nvm_blob_t *cli_get_params(void)
{
    return &s_params;
}

bool cli_is_calibrated(void)
{
    return s_calibrated;
}
