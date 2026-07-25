/* debug_log.c —— 通过USART1把调试文本发到电脑串口终端，方便排查问题（详见01工程同名文件的说明） */
#include "debug_log.h"
#include "usart.h"
#include <string.h>

void DebugLog_Init(void)
{
    MX_USART1_UART_Init();
}

/* 阻塞式发送，最多等1秒 */
void DebugLog_Write(const char *text)
{
    if (text != NULL) {
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)text, (uint16_t)strlen(text), 1000U);
    }
}
