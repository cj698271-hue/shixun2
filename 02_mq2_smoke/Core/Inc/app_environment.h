/*
 * app_environment.h —— MQ2烟雾监测业务模块的对外接口
 * 比01工程多传一个ADC句柄，因为MQ2传感器是模拟量输出，需要靠ADC采样才能读数。
 */
#ifndef APP_ENVIRONMENT_H
#define APP_ENVIRONMENT_H

#include "stm32f1xx_hal.h"

/* 初始化：传入ESP-12F串口句柄和MQ2所用的ADC句柄 */
HAL_StatusTypeDef EnvironmentMonitor_Init(UART_HandleTypeDef *esp_uart, ADC_HandleTypeDef *mq2_adc);
/* 主循环中反复调用 */
void EnvironmentMonitor_Process(void);

#endif
