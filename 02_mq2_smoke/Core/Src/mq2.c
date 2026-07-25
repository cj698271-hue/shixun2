#include "mq2.h"

/*
 * 读取MQ2传感器的ADC原始值。
 * 连续采样8次再取平均值，是为了平滑掉单次采样可能出现的电噪声毛刺，
 * 让读数更稳定（MQ2本身信号变化就比较慢，多采样几次几乎不影响响应速度）。
 */
HAL_StatusTypeDef MQ2_ReadRaw(ADC_HandleTypeDef *hadc, uint16_t *raw_value)
{
    uint32_t total = 0U;
    uint32_t sample;

    if (hadc == NULL || raw_value == NULL) {
        return HAL_ERROR;
    }

    for (sample = 0; sample < 8U; ++sample) {
        /* 每次采样都是"启动转换->等转换完成->取值->停止"的标准单次ADC转换流程 */
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
