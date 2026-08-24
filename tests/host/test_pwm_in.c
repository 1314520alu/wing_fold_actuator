#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "main.h"
#include "../../App/pwm_in.h"

static uint32_t now_ms;
static uint32_t captured_tick;

void HAL_NVIC_SetPriority(int irqn, uint32_t priority, uint32_t subpriority)
{
    assert(irqn == TIM2_IRQn);
    assert(priority == 1U);
    assert(subpriority == 0U);
}

void HAL_NVIC_EnableIRQ(int irqn)
{
    assert(irqn == TIM2_IRQn);
}

HAL_StatusTypeDef HAL_TIM_IC_Start_IT(TIM_HandleTypeDef *htim,
                                      uint32_t channel)
{
    (void)htim;
    assert(channel == TIM_CHANNEL_1);
    return HAL_OK;
}

uint32_t HAL_TIM_ReadCapturedValue(TIM_HandleTypeDef *htim, uint32_t channel)
{
    (void)htim;
    assert(channel == TIM_CHANNEL_1);
    return captured_tick;
}

uint32_t HAL_GetTick(void)
{
    return now_ms;
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim);

static void capture_pulse(TIM_HandleTypeDef *timer, uint32_t rising_tick,
                          uint32_t falling_tick, uint32_t edge_ms)
{
    timer->Channel = HAL_TIM_ACTIVE_CHANNEL_1;
    captured_tick = rising_tick;
    HAL_TIM_IC_CaptureCallback(timer);
    now_ms = edge_ms;
    captured_tick = falling_tick;
    HAL_TIM_IC_CaptureCallback(timer);
}

static void test_invalid_pulse_does_not_refresh_last_good_edge(void)
{
    TIM_HandleTypeDef timer = {0};
    uint16_t pulse_us;

    pwm_in_init(&timer);
    capture_pulse(&timer, 100U, 1600U, 10U);
    assert(pwm_in_get_pulse_us(&pulse_us));
    assert(pulse_us == 1500U);
    assert(pwm_in_last_edge_ms() == 10U);

    capture_pulse(&timer, 2000U, 2300U, 100U);
    assert(pwm_in_last_edge_ms() == 10U);
}

int main(void)
{
    test_invalid_pulse_does_not_refresh_last_good_edge();
    printf("OK\n");
    return 0;
}
