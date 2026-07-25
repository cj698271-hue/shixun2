/*
 * mq2.h —— MQ2烟雾/可燃气体传感器驱动的对外接口
 * MQ2是模拟量输出传感器（浓度越高，输出电压越高），需要通过ADC采样读取。
 */
#ifndef MQ2_H
#define MQ2_H

#include "stm32f1xx_hal.h"

/* 触发一次ADC转换并读取原始值（0~4095，对应0~3.3V） */
HAL_StatusTypeDef MQ2_ReadRaw(ADC_HandleTypeDef *hadc, uint16_t *raw_value);

#endif
