#ifndef MQ2_H
#define MQ2_H

#include "stm32f1xx_hal.h"

HAL_StatusTypeDef MQ2_ReadRaw(ADC_HandleTypeDef *hadc, uint16_t *raw_value);

#endif
