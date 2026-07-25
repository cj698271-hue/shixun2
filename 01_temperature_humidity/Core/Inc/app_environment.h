/*
 * app_environment.h —— "环境监测"业务模块的对外接口
 *
 * 这是整个项目的"大脑"模块，负责把 DHT11传感器读数、ESP-12F联网、
 * MQTT协议收发这几件事串联起来。main.c 只需要调用这两个函数，
 * 不需要知道内部具体怎么实现。
 */
#ifndef APP_ENVIRONMENT_H
#define APP_ENVIRONMENT_H

#include "stm32f1xx_hal.h"

/* 初始化：传入ESP-12F所用的串口句柄，内部会初始化DHT11和ESP-12F模块 */
HAL_StatusTypeDef EnvironmentMonitor_Init(UART_HandleTypeDef *esp_uart);
/* 主循环中反复调用：内部按时间间隔自动决定是否读传感器/发心跳/上报数据 */
void EnvironmentMonitor_Process(void);

#endif
