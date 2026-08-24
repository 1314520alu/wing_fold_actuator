#include "pwm_in.h"

#define PWM_IN_MIN_US 800U
#define PWM_IN_MAX_US 2200U

static TIM_HandleTypeDef *s_htim;
static volatile uint16_t s_last_pulse_us;
static volatile bool s_pulse_valid;
static volatile uint32_t s_last_edge_ms;
static volatile uint32_t s_rising_tick;
static volatile bool s_awaiting_falling;

static uint32_t capture_delta(uint32_t start, uint32_t end)
{
    if (end >= start) {
        return end - start;
    }
    return (65536U - start) + end;
}

void pwm_in_init(TIM_HandleTypeDef *htim)
{
    s_htim = htim;
    s_last_pulse_us = 0U;
    s_pulse_valid = false;
    s_last_edge_ms = 0U;
    s_rising_tick = 0U;
    s_awaiting_falling = false;

    if (s_htim == NULL) {
        return;
    }

    HAL_NVIC_SetPriority(TIM2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    HAL_TIM_IC_Start_IT(s_htim, TIM_CHANNEL_1);
}

bool pwm_in_get_pulse_us(uint16_t *out_us)
{
    if ((out_us == NULL) || !s_pulse_valid) {
        return false;
    }

    *out_us = s_last_pulse_us;
    return true;
}

uint32_t pwm_in_last_edge_ms(void)
{
    return s_last_edge_ms;
}

static void pwm_in_on_capture(TIM_HandleTypeDef *htim)
{
    uint32_t captured;
    uint32_t width;

    if ((htim == NULL) || (htim != s_htim)
        || (htim->Channel != HAL_TIM_ACTIVE_CHANNEL_1)) {
        return;
    }

    captured = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);

    if (!s_awaiting_falling) {
        s_rising_tick = captured;
        s_awaiting_falling = true;
        __HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_1,
                                      TIM_INPUTCHANNELPOLARITY_FALLING);
        return;
    }

    width = capture_delta(s_rising_tick, captured);
    s_awaiting_falling = false;
    __HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_1,
                                  TIM_INPUTCHANNELPOLARITY_RISING);

    if ((width >= PWM_IN_MIN_US) && (width <= PWM_IN_MAX_US)) {
        s_last_pulse_us = (uint16_t)width;
        s_pulse_valid = true;
        s_last_edge_ms = HAL_GetTick();
    }
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    pwm_in_on_capture(htim);
}
