#include "led_status.h"

#include <stdint.h>

#include "main.h"

#define LED_UNCALIBRATED_TOGGLE_MS  125U
#define LED_CALIBRATED_TOGGLE_MS    500U

static bool s_calibrated;
static uint32_t s_last_toggle_ms;

void led_status_init(bool calibrated)
{
    s_calibrated = calibrated;
    s_last_toggle_ms = HAL_GetTick();
}

void led_status_set_calibrated(bool calibrated)
{
    if (s_calibrated != calibrated) {
        s_calibrated = calibrated;
        s_last_toggle_ms = HAL_GetTick();
    }
}

void led_status_poll(void)
{
    const uint32_t now = HAL_GetTick();
    const uint32_t interval = s_calibrated
                            ? LED_CALIBRATED_TOGGLE_MS
                            : LED_UNCALIBRATED_TOGGLE_MS;

    if ((uint32_t)(now - s_last_toggle_ms) >= interval) {
        s_last_toggle_ms = now;
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    }
}
