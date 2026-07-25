#ifndef DHT11_H
#define DHT11_H

#include "stm32f1xx_hal.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} DHT11_Handle;

typedef struct {
    float temperature_c;
    float humidity_percent;
} DHT11_Data;

HAL_StatusTypeDef DHT11_Init(const DHT11_Handle *handle);
HAL_StatusTypeDef DHT11_Read(const DHT11_Handle *handle, DHT11_Data *data);

#endif
