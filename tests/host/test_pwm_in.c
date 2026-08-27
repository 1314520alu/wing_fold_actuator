#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "../../App/pwm_in.h"

static uint32_t now_ms;
static uint32_t tim_cnt;
static GPIO_PinState pin_state = GPIO_PIN_RESET;
static TIM_HandleTypeDef fake_tim;

GPIO_TypeDef GPIOA_inst;
GPIO_TypeDef *GPIOA = &GPIOA_inst;

void HAL_NVIC_SetPriority(int irqn, uint32_t priority, uint32_t subpriority)
{
    (void)irqn;
    (void)priority;
    (void)subpriority;
}

void HAL_NVIC_EnableIRQ(int irqn)
{
    (void)irqn;
}

HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *htim)
{
    (void)htim;
    return HAL_OK;
}

void HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init)
{
    (void)GPIOx;
    (void)GPIO_Init;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    (void)GPIOx;
    (void)GPIO_Pin;
    return pin_state;
}

uint32_t HAL_GetTick(void)
{
    return now_ms;
}

uint32_t fake_tim_get_counter(void)
{
    return tim_cnt;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);

static void edge(uint32_t cnt, GPIO_PinState level, uint32_t ms)
{
    tim_cnt = cnt;
    pin_state = level;
    now_ms = ms;
    HAL_GPIO_EXTI_Callback(GPIO_PIN_0);
}

int main(void)
{
    uint16_t pulse = 0U;

    pwm_in_init(&fake_tim);
    pwm_in_set_cmd_range(1000U, 2000U);

    /* Active-high 1500 µs pulse: rise then fall. */
    now_ms = 10U;
    edge(100U, GPIO_PIN_SET, 10U);
    edge(1600U, GPIO_PIN_RESET, 10U);
    assert(pwm_in_get_pulse_us(&pulse));
    assert(pulse == 1500U);

    /* Over-range high clamped to 2000. */
    edge(2000U, GPIO_PIN_SET, 20U);
    edge(4300U, GPIO_PIN_RESET, 30U); /* raw 2300 → clamp 2000, slew limited */
    assert(pwm_in_get_pulse_us(&pulse));
    assert(pulse <= 2000U);
    assert(pulse >= 1500U);

    /* Low-time (~18 ms) on inverted path is ignored: only rise starts. */
    edge(0U, GPIO_PIN_SET, 40U);
    edge(100U, GPIO_PIN_RESET, 40U); /* 100 µs high — below capture min, reject */
    /* previous pulse should remain valid */
    assert(pwm_in_get_pulse_us(&pulse));

    printf("OK\n");
    return 0;
}
