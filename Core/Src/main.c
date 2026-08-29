#include "main.h"

#include <stdio.h>

#include "app.h"
#include "cli.h"
#include "encoder.h"
#include "led_status.h"
#include "pwm_in.h"
#include "servo_bus.h"

/* Set to 1 for HTD-85H bench smoke: +200 / 0 / -200 for 1 s each. */
#ifndef SERVO_BUS_SMOKE_TEST
#define SERVO_BUS_SMOKE_TEST 0
#endif

/* Set to 1 to print BRT38 absolute counts on USART3 every 200 ms. */
#ifndef ENCODER_SMOKE_TEST
#define ENCODER_SMOKE_TEST 0
#endif

/* Set to 1 to print captured servo PWM pulse width on USART3 every 200 ms. */
#ifndef PWM_IN_SMOKE_TEST
#define PWM_IN_SMOKE_TEST 0
#endif

/* Set to 1: USART3 (B10/B11) hello + echo only. Use to verify CH340 wiring. */
#ifndef USART3_SMOKE_TEST
#define USART3_SMOKE_TEST 0
#endif

TIM_HandleTypeDef htim2;
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART3_UART_Init(void);

int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();

#if SERVO_BUS_SMOKE_TEST
  /* Wiring: USART1 PA9/PA10 -> half-duplex TTL buffer logic side; servo VBAT
   * separate; common GND. Default servo ID 1; change if needed. */
  servo_bus_init(&huart1, 1U);
#endif
#if ENCODER_SMOKE_TEST
  /* USART2 PA2/PA3 requires an external RS485 transceiver with automatic
   * direction control. USART3 PB10 is the 115200 8N1 smoke-test output. */
  encoder_init(&huart2);
#endif
#if PWM_IN_SMOKE_TEST
  /* TIM2 CH1 on PA0: connect flight-controller or servo tester PWM. */
  pwm_in_init(&htim2);
#endif
#if !SERVO_BUS_SMOKE_TEST && !ENCODER_SMOKE_TEST && !PWM_IN_SMOKE_TEST && !USART3_SMOKE_TEST
  app_init();
#endif

#if !USART3_SMOKE_TEST && !PWM_IN_SMOKE_TEST && !ENCODER_SMOKE_TEST && !SERVO_BUS_SMOKE_TEST
  uint32_t last_app_tick_ms = HAL_GetTick();
#endif
  while (1)
  {
#if USART3_SMOKE_TEST
    /* Continuous TX on PB10; echo RX from PB11. LED toggles each hello. */
    {
      static uint32_t usart3_hello_n = 0U;
      char line[48];
      int length;
      uint8_t byte;

      while (HAL_UART_Receive(&huart3, &byte, 1U, 0U) == HAL_OK) {
        (void)HAL_UART_Transmit(&huart3, &byte, 1U, 20U);
      }

      length = snprintf(line, sizeof(line),
                        "USART3 OK n=%lu PB10=TX PB11=RX\r\n",
                        (unsigned long)usart3_hello_n++);
      if ((length > 0) && ((size_t)length < sizeof(line))) {
        (void)HAL_UART_Transmit(&huart3, (uint8_t *)line, (uint16_t)length,
                                100U);
      }
      HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
      HAL_Delay(500);
    }
#elif PWM_IN_SMOKE_TEST
    uint16_t pulse_us;
    char line[32];
    int length;

    if (pwm_in_get_pulse_us(&pulse_us)) {
      length = snprintf(line, sizeof(line), "PWM %u us\r\n",
                        (unsigned int)pulse_us);
    } else {
      length = snprintf(line, sizeof(line), "PWM --\r\n");
    }
    if ((length > 0) && ((size_t)length < sizeof(line))) {
      HAL_UART_Transmit(&huart3, (uint8_t *)line, (uint16_t)length, 100U);
    }
    HAL_Delay(200);
#elif ENCODER_SMOKE_TEST
    int32_t count;
    char line[48];
    int length;

    if (encoder_read_count(&count)) {
      length = snprintf(line, sizeof(line), "ENC %ld\r\n", (long)count);
    } else {
      length = snprintf(line, sizeof(line), "ENC ERR %u\r\n",
                        (unsigned int)encoder_fail_streak());
    }
    if ((length > 0) && ((size_t)length < sizeof(line))) {
      HAL_UART_Transmit(&huart3, (uint8_t *)line, (uint16_t)length, 100U);
    }
    HAL_Delay(200);
#elif SERVO_BUS_SMOKE_TEST
    servo_bus_set_motor_speed_immediate(200);
    HAL_Delay(1000);
    servo_bus_motor_stop();
    HAL_Delay(1000);
    servo_bus_set_motor_speed_immediate(-200);
    HAL_Delay(1000);
    servo_bus_motor_stop();
    HAL_Delay(1000);
#else
    cli_poll();
    {
      const uint32_t now_ms = HAL_GetTick();
      if ((uint32_t)(now_ms - last_app_tick_ms) >= 10U) {
        last_app_tick_ms = now_ms;
        app_tick(now_ms);
      }
    }
#endif
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }

  {
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5; /* 72/1.5 = 48 MHz */
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }
  }
}

static void MX_TIM2_Init(void)
{
  /* Free-running 1 µs counter for EXTI pulse timing on PA0 (not IC mode). */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 71; /* 72 MHz / 72 = 1 MHz */
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 65535;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
}

static void uart_common_init(UART_HandleTypeDef *huart, USART_TypeDef *instance,
                             uint32_t baud_rate)
{
  huart->Instance = instance;
  huart->Init.BaudRate = baud_rate;
  huart->Init.WordLength = UART_WORDLENGTH_8B;
  huart->Init.StopBits = UART_STOPBITS_1;
  huart->Init.Parity = UART_PARITY_NONE;
  huart->Init.Mode = UART_MODE_TX_RX;
  huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart->Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(huart) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_USART1_UART_Init(void)
{
  uart_common_init(&huart1, USART1, 115200);
}

static void MX_USART2_UART_Init(void)
{
  uart_common_init(&huart2, USART2, 9600);
}

static void MX_USART3_UART_Init(void)
{
  uart_common_init(&huart3, USART3, 115200);
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);

  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif
