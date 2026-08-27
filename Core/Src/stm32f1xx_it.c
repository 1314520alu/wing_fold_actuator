#include "main.h"
#include "stm32f1xx_it.h"

#include "cli.h"

extern TIM_HandleTypeDef htim2;
extern UART_HandleTypeDef huart3;

void NMI_Handler(void)
{
  while (1)
  {
  }
}

void HardFault_Handler(void)
{
  while (1)
  {
  }
}

void MemManage_Handler(void)
{
  while (1)
  {
  }
}

void BusFault_Handler(void)
{
  while (1)
  {
  }
}

void UsageFault_Handler(void)
{
  while (1)
  {
  }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}

void TIM2_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim2);
}

void EXTI0_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}

void USART3_IRQHandler(void)
{
  if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_ORE)
      || __HAL_UART_GET_FLAG(&huart3, UART_FLAG_NE)
      || __HAL_UART_GET_FLAG(&huart3, UART_FLAG_FE)
      || __HAL_UART_GET_FLAG(&huart3, UART_FLAG_PE)) {
    __HAL_UART_CLEAR_OREFLAG(&huart3);
  }

  if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_RXNE)) {
    const uint8_t byte = (uint8_t)(huart3.Instance->DR & 0xFFU);
    cli_uart_rx_irq_byte(byte);
  }

  HAL_UART_IRQHandler(&huart3);
}
