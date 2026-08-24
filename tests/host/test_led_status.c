#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "main.h"
#include "../../App/led_status.h"

GPIO_TypeDef fake_led_port;
static uint32_t now_ms;
static unsigned int toggle_count;

uint32_t HAL_GetTick(void)
{
    return now_ms;
}

void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin)
{
    assert(port == LED_GPIO_Port);
    assert(pin == LED_Pin);
    ++toggle_count;
}

static void test_uncalibrated_fast_flash_then_calibrated_heartbeat(void)
{
    now_ms = 0U;
    toggle_count = 0U;
    led_status_init(false);

    now_ms = 124U;
    led_status_poll();
    assert(toggle_count == 0U);
    now_ms = 125U;
    led_status_poll();
    assert(toggle_count == 1U);

    led_status_set_calibrated(true);
    now_ms = 624U;
    led_status_poll();
    assert(toggle_count == 1U);
    now_ms = 625U;
    led_status_poll();
    assert(toggle_count == 2U);
}

static void test_hold_and_fault_patterns_are_distinct(void)
{
    now_ms = 0U;
    toggle_count = 0U;
    led_status_init(true);

    led_status_hold();
    now_ms = 999U;
    led_status_poll();
    assert(toggle_count == 0U);
    now_ms = 1000U;
    led_status_poll();
    assert(toggle_count == 1U);

    led_status_fault();
    now_ms = 1124U;
    led_status_poll();
    assert(toggle_count == 1U);
    now_ms = 1125U;
    led_status_poll();
    assert(toggle_count == 2U);
}

int main(void)
{
    test_uncalibrated_fast_flash_then_calibrated_heartbeat();
    test_hold_and_fault_patterns_are_distinct();
    printf("OK\n");
    return 0;
}
