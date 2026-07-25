#include "mq2.h"

HAL_StatusTypeDef MQ2_ReadRaw(ADC_HandleTypeDef *hadc, uint16_t *raw_value)
{
    uint32_t total = 0U;
    uint32_t sample;

    if (hadc == NULL || raw_value == NULL) {
        return HAL_ERROR;
    }

    for (sample = 0; sample < 8U; ++sample) {
        if (HAL_ADC_Start(hadc) != HAL_OK ||
            HAL_ADC_PollForConversion(hadc, 20U) != HAL_OK) {
            (void)HAL_ADC_Stop(hadc);
            return HAL_TIMEOUT;
        }
        total += HAL_ADC_GetValue(hadc);
        (void)HAL_ADC_Stop(hadc);
    }

    *raw_value = (uint16_t)(total / 8U);
    return HAL_OK;
}
