#include "dht11.h"

static void DelayUs(uint32_t microseconds)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = microseconds * (SystemCoreClock / 1000000U);

    while ((DWT->CYCCNT - start) < cycles) {
    }
}

static void SetOutput(const DHT11_Handle *handle)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = handle->pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(handle->port, &gpio);
}

static void SetInput(const DHT11_Handle *handle)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = handle->pin;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(handle->port, &gpio);
}

static HAL_StatusTypeDef WaitForLevel(const DHT11_Handle *handle, GPIO_PinState level, uint32_t timeout_us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = timeout_us * (SystemCoreClock / 1000000U);

    while (HAL_GPIO_ReadPin(handle->port, handle->pin) != level) {
        if ((DWT->CYCCNT - start) > cycles) {
            return HAL_TIMEOUT;
        }
    }
    return HAL_OK;
}

HAL_StatusTypeDef DHT11_Init(const DHT11_Handle *handle)
{
    if (handle == NULL || handle->port == NULL) {
        return HAL_ERROR;
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    SetOutput(handle);
    HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_SET);
    return HAL_OK;
}

HAL_StatusTypeDef DHT11_Read(const DHT11_Handle *handle, DHT11_Data *data)
{
    uint8_t bytes[5] = {0};
    uint32_t bit_index;

    if (handle == NULL || data == NULL) {
        return HAL_ERROR;
    }

    SetOutput(handle);
    HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_RESET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_SET);
    DelayUs(30U);
    SetInput(handle);

    if (WaitForLevel(handle, GPIO_PIN_RESET, 100U) != HAL_OK ||
        WaitForLevel(handle, GPIO_PIN_SET, 100U) != HAL_OK ||
        WaitForLevel(handle, GPIO_PIN_RESET, 100U) != HAL_OK) {
        return HAL_TIMEOUT;
    }

    for (bit_index = 0; bit_index < 40U; ++bit_index) {
        uint32_t high_start;
        uint32_t high_cycles;

        if (WaitForLevel(handle, GPIO_PIN_SET, 70U) != HAL_OK) {
            return HAL_TIMEOUT;
        }
        high_start = DWT->CYCCNT;
        if (WaitForLevel(handle, GPIO_PIN_RESET, 100U) != HAL_OK) {
            return HAL_TIMEOUT;
        }
        high_cycles = DWT->CYCCNT - high_start;
        bytes[bit_index / 8U] <<= 1U;
        if (high_cycles > (SystemCoreClock / 1000000U) * 50U) {
            bytes[bit_index / 8U] |= 1U;
        }
    }

    if ((uint8_t)(bytes[0] + bytes[1] + bytes[2] + bytes[3]) != bytes[4]) {
        return HAL_ERROR;
    }

    data->humidity_percent = (float)bytes[0] + ((float)bytes[1] / 10.0f);
    data->temperature_c = (float)bytes[2] + ((float)bytes[3] / 10.0f);
    return HAL_OK;
}
