#include "led_status.h"

#include <stdint.h>

#include "main.h"

#define LED_UNCALIBRATED_TOGGLE_MS  125U
#define LED_CALIBRATED_TOGGLE_MS    500U
#define LED_HOLD_TOGGLE_MS          1000U
#define LED_FAULT_TOGGLE_MS         125U

typedef enum {
    LED_MODE_RUN = 0,
    LED_MODE_HOLD,
    LED_MODE_FAULT
} led_mode_t;

static bool s_calibrated;
static uint32_t s_last_toggle_ms;
static led_mode_t s_mode;

static void set_mode(led_mode_t mode)
{
    if (s_mode != mode) {
        s_mode = mode;
        s_last_toggle_ms = HAL_GetTick();
    }
}

void led_status_init(bool calibrated)
{
    s_calibrated = calibrated;
    s_last_toggle_ms = HAL_GetTick();
    s_mode = LED_MODE_RUN;
}

void led_status_set_calibrated(bool calibrated)
{
    if (s_calibrated != calibrated) {
        s_calibrated = calibrated;
        s_last_toggle_ms = HAL_GetTick();
    }
}

void led_status_run(void)
{
    set_mode(LED_MODE_RUN);
}

void led_status_hold(void)
{
    set_mode(LED_MODE_HOLD);
}

void led_status_fault(void)
{
    set_mode(LED_MODE_FAULT);
}

void led_status_poll(void)
{
    const uint32_t now = HAL_GetTick();
    uint32_t interval;

    if (s_mode == LED_MODE_FAULT) {
        interval = LED_FAULT_TOGGLE_MS;
    } else if (s_mode == LED_MODE_HOLD) {
        interval = LED_HOLD_TOGGLE_MS;
    } else {
        interval = s_calibrated
                 ? LED_CALIBRATED_TOGGLE_MS
                 : LED_UNCALIBRATED_TOGGLE_MS;
    }

    if ((uint32_t)(now - s_last_toggle_ms) >= interval) {
        s_last_toggle_ms = now;
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    }
}
