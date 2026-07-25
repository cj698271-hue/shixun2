/*
 * esp12f.h —— ESP-12F WiFi模块驱动的对外接口（MQ2工程版本）
 * 与01工程基本一致，多了一个ESP12F_TryReadRaw：非阻塞地看看有没有新数据，
 * 用于主循环里"顺手检查一下有没有云平台下发的指令"，不能像ESP12F_ReadRaw那样阻塞等待。
 */
#ifndef ESP12F_H
#define ESP12F_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stm32f1xx_hal.h"

typedef struct {
    UART_HandleTypeDef *uart;
    bool transparent_mode;
    bool wifi_connected;
} ESP12F_Handle;

HAL_StatusTypeDef ESP12F_Init(ESP12F_Handle *handle, UART_HandleTypeDef *uart);
HAL_StatusTypeDef ESP12F_ConnectWifi(ESP12F_Handle *handle, const char *ssid, const char *password);
HAL_StatusTypeDef ESP12F_OpenTcp(ESP12F_Handle *handle, const char *host, uint16_t port, bool tls);
HAL_StatusTypeDef ESP12F_StartTransparentMode(ESP12F_Handle *handle);
HAL_StatusTypeDef ESP12F_ExitTransparentMode(ESP12F_Handle *handle);
HAL_StatusTypeDef ESP12F_CloseTcp(ESP12F_Handle *handle);
HAL_StatusTypeDef ESP12F_SendRaw(ESP12F_Handle *handle, const uint8_t *data, size_t length);
/* 阻塞等待，最多timeout_ms毫秒，读到一帧数据才返回（01工程CONNECT等场景用这个） */
HAL_StatusTypeDef ESP12F_ReadRaw(ESP12F_Handle *handle, uint8_t *data, size_t capacity, size_t *length, uint32_t timeout_ms);
/* 非阻塞：立刻检查一下当前是否已经有数据，没有就马上返回失败，不等待（主循环轮询下行指令用这个） */
HAL_StatusTypeDef ESP12F_TryReadRaw(ESP12F_Handle *handle, uint8_t *data, size_t capacity, size_t *length);

#endif
