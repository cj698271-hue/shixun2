/* USER CODE BEGIN Header */
/** @file usart.h @brief USART function prototypes. */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USART_H__
#define __USART_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */
#include <stdbool.h>
/* USER CODE END Includes */

extern UART_HandleTypeDef huart1;

extern UART_HandleTypeDef huart2;

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_USART1_UART_Init(void);
void MX_USART2_UART_Init(void);

/* USER CODE BEGIN Prototypes */
/* 手写的中断接收+缓冲区管理接口，供esp12f.c驱动ESP-12F模块时使用 */
void USART2_StartRxIT(void);            /* 重新开启一次单字节中断接收 */
HAL_StatusTypeDef USART2_SetBaudRate(uint32_t baud);  /* 动态修改波特率 */
void USART2_RxClear(void);              /* 清空接收缓冲区 */
bool USART2_RxContains(const char *text);  /* 检查缓冲区是否包含指定文本 */
bool USART2_RxHasError(void);           /* 检查是否收到了ERROR/FAIL */
uint16_t USART2_RxCopy(uint8_t *out, uint16_t capacity);  /* 拷贝缓冲区内容（不清空） */
void USART2_RxDiscard(uint16_t length); /* 丢弃缓冲区开头已处理的length字节 */
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __USART_H__ */

