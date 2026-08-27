#pragma once

#include <stdint.h>

typedef struct {
    volatile uint32_t DR;
} USART_TypeDef;

typedef enum {
    HAL_UART_STATE_RESET = 0x00U,
    HAL_UART_STATE_READY = 0x20U,
    HAL_UART_STATE_BUSY  = 0x24U,
} HAL_UART_StateTypeDef;

typedef struct {
    USART_TypeDef *Instance;
    struct {
        uint32_t BaudRate;
    } Init;
    HAL_UART_StateTypeDef gState;
    uint32_t ErrorCode;
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
#define UART_FLAG_RXNE                    0x0020U
#define UART_FLAG_ORE                     0x0008U
#define UART_FLAG_NE                      0x0004U
#define UART_FLAG_FE                      0x0002U
#define UART_FLAG_PE                      0x0001U
#define UART_IT_RXNE                      0x0525U
#define HAL_UART_ERROR_NONE               0x00000000U
#define USART3_IRQn                       39
#define EXTI0_IRQn                        6
#define GPIO_PIN_0                        (1U << 0)
#define GPIO_MODE_IT_RISING_FALLING       0x10110000U
#define GPIO_PULLDOWN                     0x00000002U
#define RESET                             0
#define __HAL_UART_GET_FLAG(__HANDLE__, __FLAG__) (RESET)
#define __HAL_UART_CLEAR_OREFLAG(__HANDLE__) \
    do { (void)(__HANDLE__); } while (0)
#define __HAL_UART_ENABLE_IT(__HANDLE__, __INTERRUPT__) \
    do { (void)(__HANDLE__); (void)(__INTERRUPT__); } while (0)
#define __HAL_RCC_GPIOA_CLK_ENABLE()      ((void)0)
#define __HAL_RCC_AFIO_CLK_ENABLE()       ((void)0)
#define __HAL_TIM_GET_COUNTER(__HANDLE__) fake_tim_get_counter()
#define __HAL_TIM_SET_CAPTUREPOLARITY(htim, channel, polarity) \
    do {                                                        \
        (void)(htim);                                           \
        (void)(channel);                                        \
        (void)(polarity);                                       \
    } while (0)

typedef int GPIO_PinState;
#define GPIO_PIN_RESET ((GPIO_PinState)0)
#define GPIO_PIN_SET   ((GPIO_PinState)1)

typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
} GPIO_InitTypeDef;

extern GPIO_TypeDef fake_led_port;
extern GPIO_TypeDef *GPIOA;
#define LED_GPIO_Port               (&fake_led_port)
#define LED_Pin                     GPIO_PIN_13

uint32_t fake_tim_get_counter(void);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart,
                                    uint8_t *data,
                                    uint16_t size,
                                    uint32_t timeout);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart,
                                       uint8_t *data,
                                       uint16_t size);
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *huart,
                                   uint8_t *data,
                                   uint16_t size,
                                   uint32_t timeout);
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef *huart);
void HAL_Delay(uint32_t Delay);
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *erase,
                                    uint32_t *page_error);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type,
                                    uint32_t address,
                                    uint64_t data);
uint32_t HAL_GetTick(void);
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
void HAL_NVIC_SetPriority(int irqn, uint32_t priority, uint32_t subpriority);
void HAL_NVIC_EnableIRQ(int irqn);
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_IC_Start_IT(TIM_HandleTypeDef *htim,
                                      uint32_t channel);
uint32_t HAL_TIM_ReadCapturedValue(TIM_HandleTypeDef *htim, uint32_t channel);
