/*
 * adc.c —— ADC1初始化（本工程实际未使用ADC采集任何数据，DHT11走的是GPIO时序，不是模拟量）。
 * 保留此文件是因为项目共用同一套外设初始化模板，此处配置PA1为ADC1通道1，供以后扩展使用。
 */
#include "adc.h"

ADC_HandleTypeDef hadc1;

/* 初始化ADC1：单次转换模式（非连续、非扫描），只用1个通道 */
void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef channel = {0};

    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        Error_Handler();
    }

    /* 配置采样通道1（对应PA1引脚），采样时间选最长档(239.5周期)以提高精度 */
    channel.Channel = ADC_CHANNEL_1;
    channel.Rank = ADC_REGULAR_RANK_1;
    channel.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &channel) != HAL_OK) {
        Error_Handler();
    }
    /* STM32F1系列ADC需要先做一次自校准，消除内部偏移误差 */
    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK) {
        Error_Handler();
    }
}

/* HAL库回调：HAL_ADC_Init内部会自动调用它，完成ADC对应的时钟使能和引脚配置 */
void HAL_ADC_MspInit(ADC_HandleTypeDef *adc)
{
    GPIO_InitTypeDef gpio = {0};

    if (adc->Instance == ADC1) {
        __HAL_RCC_ADC1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        /* PA1配置为模拟输入模式（ADC采样引脚必须是模拟模式，不能是普通数字IO） */
        gpio.Pin = GPIO_PIN_1;
        gpio.Mode = GPIO_MODE_ANALOG;
        HAL_GPIO_Init(GPIOA, &gpio);
    }
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef *adc)
{
    if (adc->Instance == ADC1) {
        __HAL_RCC_ADC1_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_1);
    }
}
