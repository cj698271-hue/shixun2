/*
 * debug_log.c —— 极简的调试日志输出，通过USART1把文本发到电脑串口终端，
 * 方便开发时观察程序运行到哪一步、哪里失败了（本项目大量用它输出"XXX_FAIL"这类诊断信息）。
 */
#include "debug_log.h"
#include "usart.h"
#include <string.h>

void DebugLog_Init(void)
{
    MX_USART1_UART_Init();
}

/* 阻塞式发送一段文本，最多等1秒。日志发送慢一点没关系，不会影响主流程的正确性 */
void DebugLog_Write(const char *text)
{
    if (text != NULL) {
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)text, (uint16_t)strlen(text), 1000U);
    }
}
