/*
 * dht11.c —— DHT11单线时序通信驱动
 *
 * 【DHT11的通信原理简述】
 * DHT11只用一根数据线（本项目是PA0）既发指令又收数据，靠"电平持续的时间长短"
 * 来表示0和1（类似摩斯电码，但更简单）：
 *   - 主机（STM32）先拉低电平至少18ms，通知DHT11"我要读数据了"
 *   - 然后拉高一小段时间，切换成输入模式等待DHT11的回应
 *   - DHT11回应：拉低80us、拉高80us，作为"我收到了，准备发数据"的应答信号
 *   - 接着连续发送40个二进制位（5个字节）：每一位先拉低50us，
 *     再拉高一段时间——如果拉高时间短（约26-28us）代表0，长（约70us）代表1
 * 这个文件里的每一步严格按照这个协议的时序要求来读写GPIO电平。
 *
 * 【为什么要用DWT->CYCCNT计时，而不是HAL_GetTick()】
 * HAL_GetTick()的精度只有1毫秒，而DHT11协议里很多时间窗口是"几十微秒"级别，
 * 1毫秒的精度完全不够用。DWT->CYCCNT是CPU内部的"时钟周期计数器"，
 * 每个CPU时钟周期加1，可以实现微秒甚至更精细的计时。
 */
#include "dht11.h"

/*
 * 微秒级延时：通过CPU时钟周期数计算需要空转多少个周期。
 * 例如CPU主频72MHz时，1微秒 = 72个时钟周期。
 */
static void DelayUs(uint32_t microseconds)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = microseconds * (SystemCoreClock / 1000000U);

    while ((DWT->CYCCNT - start) < cycles) {
    }
}

/* 把引脚切换成"输出"模式：主机要主动拉高/拉低电平给DHT11发起始信号时用 */
static void SetOutput(const DHT11_Handle *handle)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = handle->pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(handle->port, &gpio);
}

/* 把引脚切换成"输入"模式：等待读取DHT11发回来的电平变化时用（带上拉电阻，防止线路悬空误判） */
static void SetInput(const DHT11_Handle *handle)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = handle->pin;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(handle->port, &gpio);
}

/*
 * 等待引脚电平变成指定状态（高或低），如果超过timeout_us微秒还没变化就判定超时。
 * 这是整个时序读取过程中最基础的"等待动作"，DHT11协议里每个阶段都要靠它来判断
 * 当前处于哪个信号阶段（起始应答/数据位0/数据位1）。
 */
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

/*
 * 初始化：开启CPU的周期计数器（DWT），供后面微秒级延时使用；
 * 并把引脚设为输出、拉高电平（DHT11协议规定：总线空闲时应该是高电平）。
 */
HAL_StatusTypeDef DHT11_Init(const DHT11_Handle *handle)
{
    if (handle == NULL || handle->port == NULL) {
        return HAL_ERROR;
    }

    /* 使能内核调试跟踪单元(DEMCR)和周期计数器(CYCCNTENA)，这样DWT->CYCCNT才会真正计数 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    SetOutput(handle);
    HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_SET);
    return HAL_OK;
}

/*
 * 读取一次完整的温湿度数据。整个函数执行期间会阻塞CPU大约几毫秒，
 * 因为单线协议要求主机全程精确控制/侍候电平时序，中间不能被打断太久。
 */
HAL_StatusTypeDef DHT11_Read(const DHT11_Handle *handle, DHT11_Data *data)
{
    uint8_t bytes[5] = {0};   /* DHT11每次发回5个字节：湿度整数+湿度小数+温度整数+温度小数+校验和 */
    uint32_t bit_index;

    if (handle == NULL || data == NULL) {
        return HAL_ERROR;
    }

    /* 起始信号：主机拉低至少18ms（这里用20ms留余量），通知DHT11"开始一次读取" */
    SetOutput(handle);
    HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_RESET);
    HAL_Delay(20U);
    /* 拉高一小段时间(30us)后切换为输入，等待DHT11的应答信号 */
    HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_SET);
    DelayUs(30U);
    SetInput(handle);

    /* DHT11的应答：拉低约80us，再拉高约80us。这里依次等待"变低->变高->变低"三次电平跳变，
       第三次变低就意味着应答结束，即将开始传输40位数据。 */
    if (WaitForLevel(handle, GPIO_PIN_RESET, 100U) != HAL_OK ||
        WaitForLevel(handle, GPIO_PIN_SET, 100U) != HAL_OK ||
        WaitForLevel(handle, GPIO_PIN_RESET, 100U) != HAL_OK) {
        return HAL_TIMEOUT;
    }

    /* 逐位读取40个数据位（5字节 × 8位）。
       每一位的判断方法：先等电平变高（数据位开始），记录变高的时刻，
       再等电平变低（数据位结束），用"变高到变低"经过的时长来判断这一位是0还是1：
       时长短(~26-28us)是0，时长长(~70us)是1。用一个固定的50us阈值来区分刚好合适。 */
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
        bytes[bit_index / 8U] <<= 1U;   /* 每读一位就把已有的位向左移，给新位让出最低位 */
        if (high_cycles > (SystemCoreClock / 1000000U) * 50U) {
            bytes[bit_index / 8U] |= 1U;  /* 高电平持续超过50us，判定为二进制1 */
        }
        /* 否则不设置（保持0），因为上面已经左移过，默认就是0 */
    }

    /* DHT11自带一个简单的校验和：前4个字节之和的低8位应该等于第5个字节。
       校验失败说明这次读取过程中有干扰或时序判断出错，数据不可信。 */
    if ((uint8_t)(bytes[0] + bytes[1] + bytes[2] + bytes[3]) != bytes[4]) {
        return HAL_ERROR;
    }

    /* 数据格式：湿度=整数部分.小数部分，温度同理（DHT11精度只有0.1，小数部分固定是个位数） */
    data->humidity_percent = (float)bytes[0] + ((float)bytes[1] / 10.0f);
    data->temperature_c = (float)bytes[2] + ((float)bytes[3] / 10.0f);
    return HAL_OK;
}
