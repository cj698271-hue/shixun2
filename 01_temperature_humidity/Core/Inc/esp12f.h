/*
 * esp12f.h —— ESP-12F WiFi模块驱动的对外接口
 * ESP-12F通过AT指令集工作：STM32通过串口发文本指令（如"AT+CWJAP=..."），
 * ESP模块执行对应的WiFi/TCP操作并通过串口回复结果文本（如"OK"）。
 */
#ifndef ESP12F_H
#define ESP12F_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stm32f1xx_hal.h"

/* ESP12F模块的运行状态句柄 */
typedef struct {
    UART_HandleTypeDef *uart;   /* 与ESP模块通信所用的串口（本项目是USART2） */
    bool transparent_mode;      /* 是否处于"透传模式"（本项目实际未使用，见esp12f.c说明） */
    bool wifi_connected;        /* 是否已经成功连上WiFi热点 */
} ESP12F_Handle;

/* 初始化：自动探测ESP模块当前的串口波特率并配置为标准AT指令工作模式 */
HAL_StatusTypeDef ESP12F_Init(ESP12F_Handle *handle, UART_HandleTypeDef *uart);
/* 连接WiFi热点（AT+CWJAP），已连接过则直接返回成功 */
HAL_StatusTypeDef ESP12F_ConnectWifi(ESP12F_Handle *handle, const char *ssid, const char *password);
/* 向指定主机:端口建立TCP连接（AT+CIPSTART），tls为true时使用SSL加密连接 */
HAL_StatusTypeDef ESP12F_OpenTcp(ESP12F_Handle *handle, const char *host, uint16_t port, bool tls);
/* 进入透传模式（当前实现是空操作占位，保留只是为了兼容调用方接口） */
HAL_StatusTypeDef ESP12F_StartTransparentMode(ESP12F_Handle *handle);
/* 退出透传模式 */
HAL_StatusTypeDef ESP12F_ExitTransparentMode(ESP12F_Handle *handle);
/* 关闭当前TCP连接（AT+CIPCLOSE） */
HAL_StatusTypeDef ESP12F_CloseTcp(ESP12F_Handle *handle);
/* 通过已建立的TCP连接发送一段原始字节数据（内部走AT+CIPSEND流程） */
HAL_StatusTypeDef ESP12F_SendRaw(ESP12F_Handle *handle, const uint8_t *data, size_t length);
/* 阻塞等待并读取一次收到的数据，最多等待timeout_ms毫秒 */
HAL_StatusTypeDef ESP12F_ReadRaw(ESP12F_Handle *handle, uint8_t *data, size_t capacity, size_t *length, uint32_t timeout_ms);

#endif
