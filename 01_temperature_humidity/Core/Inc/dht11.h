/*
 * dht11.h —— DHT11温湿度传感器驱动的对外接口
 * DHT11是一种只用一根信号线（单线协议）就能读取温度和湿度的廉价传感器。
 */
#ifndef DHT11_H
#define DHT11_H

#include "stm32f1xx_hal.h"

/* 记录DHT11接在哪个GPIO端口的哪个引脚上（本项目里是 GPIOA + PA0） */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} DHT11_Handle;

/* 读取到的温湿度数据 */
typedef struct {
    float temperature_c;      /* 温度，单位摄氏度 */
    float humidity_percent;   /* 相对湿度，单位百分之几 */
} DHT11_Data;

/* 初始化：配置GPIO为输出模式并拉高（DHT11协议要求空闲时线路为高电平） */
HAL_StatusTypeDef DHT11_Init(const DHT11_Handle *handle);
/* 读取一次温湿度数据（这是一次阻塞式的时序通信，耗时约几毫秒到几十毫秒） */
HAL_StatusTypeDef DHT11_Read(const DHT11_Handle *handle, DHT11_Data *data);

#endif
