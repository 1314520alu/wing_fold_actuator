#pragma once

#include <stdint.h>

typedef struct {
    uint32_t instance;
} UART_HandleTypeDef;

typedef struct {
    uint32_t instance;
    uint32_t Channel;
} TIM_HandleTypeDef;

typedef struct {
    uint32_t instance;
} GPIO_TypeDef;

typedef struct {
    uint32_t TypeErase;
    uint32_t PageAddress;
    uint32_t NbPages;
} FLASH_EraseInitTypeDef;

typedef enum {
    HAL_OK = 0,
    HAL_ERROR = 1,
    HAL_TIMEOUT = 3
} HAL_StatusTypeDef;

#define FLASH_TYPEERASE_PAGES       0x02U
#define FLASH_TYPEPROGRAM_HALFWORD  0x01U
#define GPIO_PIN_13                 (1U << 13)
#define TIM2_IRQn                   28
#define TIM_CHANNEL_1               1U
#define HAL_TIM_ACTIVE_CHANNEL_1    1U
#define TIM_INPUTCHANNELPOLARITY_RISING   0U
#define TIM_INPUTCHANNELPOLARITY_FALLING  1U
#define __HAL_TIM_SET_CAPTUREPOLARITY(htim, channel, polarity) \
    do {                                                        \
        (void)(htim);                                           \
        (void)(channel);                                        \
        (void)(polarity);                                       \
    } while (0)

extern GPIO_TypeDef fake_led_port;
#define LED_GPIO_Port               (&fake_led_port)
#define LED_Pin                     GPIO_PIN_13

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart,
                                    uint8_t *data,
                                    uint16_t size,
                                    uint32_t timeout);
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *huart,
                                   uint8_t *data,
                                   uint16_t size,
                                   uint32_t timeout);
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *erase,
                                    uint32_t *page_error);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type,
                                    uint32_t address,
                                    uint64_t data);
uint32_t HAL_GetTick(void);
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin);
void HAL_NVIC_SetPriority(int irqn, uint32_t priority, uint32_t subpriority);
void HAL_NVIC_EnableIRQ(int irqn);
HAL_StatusTypeDef HAL_TIM_IC_Start_IT(TIM_HandleTypeDef *htim,
                                      uint32_t channel);
uint32_t HAL_TIM_ReadCapturedValue(TIM_HandleTypeDef *htim, uint32_t channel);
