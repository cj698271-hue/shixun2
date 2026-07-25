/* USER CODE BEGIN Header */
/** @file usart.c @brief USART configuration and ESP-12F receive support. */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "usart.h"

/* USER CODE BEGIN 0 */
#include <string.h>

#define USART2_RX_BUFFER_SIZE 1024U

/*
 * USART2是STM32与ESP-12F之间的通信通道（PA2=TX，PA3=RX）。
 * 中断每收到1个字节就把它追加到此数组；ESP驱动随后把数组内容复制出来，
 * 从中寻找AT回复“OK”或网络下行前缀“+IPD”。volatile表示这些值可能在主循环之外
 * 被串口中断随时改写，编译器不能把它们错误地缓存到寄存器里。
 */
static volatile uint8_t usart2_rx_buffer[USART2_RX_BUFFER_SIZE]; /* 累积保存ESP返回的原始字节；末尾额外保留一个0字节，方便AT文本匹配。 */
static volatile uint16_t usart2_rx_length; /* 当前已经存入数组的有效字节数；中断写入时递增，RxClear/RxDiscard清理时递减或归零。 */
static uint8_t usart2_rx_byte; /* HAL_UART_Receive_IT每次只接收这一个字节；回调函数复制进rx_buffer后立即重新挂起下一次1字节接收。 */
/* USER CODE END 0 */

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USART1 init function */

void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */
  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */
  /* USER CODE END USART1_Init 2 */

}
/* USART2 init function */

void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */
  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */
    USART2_StartRxIT(); /* 外设初始化完成后立即启动首个1字节接收中断；不启动就永远收不到ESP的AT回复。 */
  /* USER CODE END USART2_Init 2 */

}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspInit 0 */

  /* USER CODE END USART1_MspInit 0 */
    /* USART1 clock enable */
    __HAL_RCC_USART1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN USART1_MspInit 1 */

  /* USER CODE END USART1_MspInit 1 */
  }
  else if(uartHandle->Instance==USART2)
  {
  /* USER CODE BEGIN USART2_MspInit 0 */

  /* USER CODE END USART2_MspInit 0 */
    /* USART2 clock enable */
    __HAL_RCC_USART2_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART2 GPIO Configuration
    PA2     ------> USART2_TX
    PA3     ------> USART2_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USART2 interrupt Init */
    HAL_NVIC_SetPriority(USART2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
  /* USER CODE BEGIN USART2_MspInit 1 */

  /* USER CODE END USART2_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspDeInit 0 */

  /* USER CODE END USART1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART1_CLK_DISABLE();

    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9|GPIO_PIN_10);

  /* USER CODE BEGIN USART1_MspDeInit 1 */

  /* USER CODE END USART1_MspDeInit 1 */
  }
  else if(uartHandle->Instance==USART2)
  {
  /* USER CODE BEGIN USART2_MspDeInit 0 */

  /* USER CODE END USART2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART2_CLK_DISABLE();

    /**USART2 GPIO Configuration
    PA2     ------> USART2_TX
    PA3     ------> USART2_RX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2|GPIO_PIN_3);

    /* USART2 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART2_IRQn);
  /* USER CODE BEGIN USART2_MspDeInit 1 */

  /* USER CODE END USART2_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
/*
 * 下面这一段(USER CODE 1)是自己手写加在CubeMX生成的usart.c里的业务代码，
 * 实现了一个"中断接收 + 环形轮询读取"的简易缓冲区，专门用来接收ESP-12F从USART2发回的数据。
 *
 * 【为什么要用中断而不是阻塞读】
 * ESP模块什么时候回复、回复多长完全不可预测，如果用阻塞方式一个字节一个字节地等，
 * 会让整个程序卡死。所以改用"中断接收单字节 + 主循环里随时查缓冲区内容"的方式：
 * 硬件收到一个字节就触发一次中断，中断里把字节存进缓冲区，并立刻重新开启下一个字节的中断接收；
 * 主循环通过 USART2_RxContains/USART2_RxCopy 等函数随时查看"目前攒了些什么"。
 */

/* 重新开启一次"接收1个字节"的中断请求。HAL_UART_Receive_IT只会接收一次就自动停止，
   所以每次中断处理完都要重新调用它，形成"接一个字节->处理->再接一个字节"的循环。 */
void USART2_StartRxIT(void)
{
    (void)HAL_UART_Receive_IT(&huart2, &usart2_rx_byte, 1U);
}

/* 动态修改USART2的波特率：先反初始化、改配置、再重新初始化，
   之后清空旧数据并重新开启中断接收（ESP12F_Init探测波特率时会反复调用这个函数） */
HAL_StatusTypeDef USART2_SetBaudRate(uint32_t baud)
{
    if (HAL_UART_DeInit(&huart2) != HAL_OK) {
        return HAL_ERROR;
    }
    huart2.Init.BaudRate = baud;
    if (HAL_UART_Init(&huart2) != HAL_OK) {
        return HAL_ERROR;
    }
    USART2_RxClear();
    USART2_StartRxIT();
    return HAL_OK;
}

/* 清空接收缓冲区（发送新指令前调用，避免被上一条指令残留的回复干扰） */
void USART2_RxClear(void)
{
    uint16_t index;

    /* 关中断再操作缓冲区：因为中断服务函数会并发地往缓冲区写数据，
       如果不关中断，清空过程中可能被中断打断导致数据错乱（经典的中断与主程序共享数据问题） */
    __disable_irq();
    usart2_rx_length = 0U;
    for (index = 0U; index < USART2_RX_BUFFER_SIZE; ++index) {
        usart2_rx_buffer[index] = 0U;
    }
    __enable_irq();
}

/* 检查当前缓冲区里的内容是否包含指定的子字符串（简单的子串暴力匹配算法） */
bool USART2_RxContains(const char *text)
{
    uint16_t received_length;
    size_t text_length;
    uint16_t index;

    if (text == NULL) {
        return false;
    }
    text_length = strlen(text);
    received_length = usart2_rx_length;
    if (text_length == 0U || text_length > received_length) {
        return text_length == 0U;
    }
    /* 从每个可能的起始位置开始，逐字符比较，找到完全匹配就返回true */
    for (index = 0U; index <= received_length - text_length; ++index) {
        size_t cursor;
        for (cursor = 0U; cursor < text_length; ++cursor) {
            if (usart2_rx_buffer[index + cursor] != (uint8_t)text[cursor]) {
                break;
            }
        }
        if (cursor == text_length) {
            return true;
        }
    }
    return false;
}

/* 判断ESP模块是否回复了错误信息（AT指令失败通常回"ERROR"，某些场景回"FAIL"） */
bool USART2_RxHasError(void)
{
    return USART2_RxContains("ERROR\r\n") || USART2_RxContains("FAIL\r\n");
}

/* 把当前缓冲区的内容拷贝一份出去（不清空原缓冲区），供上层解析用 */
uint16_t USART2_RxCopy(uint8_t *out, uint16_t capacity)
{
    uint16_t length;

    if (out == NULL || capacity == 0U) {
        return 0U;
    }
    __disable_irq();
    length = usart2_rx_length < capacity ? usart2_rx_length : capacity;
    for (uint16_t index = 0U; index < length; ++index) {
        out[index] = usart2_rx_buffer[index];
    }
    __enable_irq();
    return length;
}

/* HAL库的中断接收完成回调：每收到一个字节就会被调用一次。
   把这个字节追加进缓冲区末尾（留一个字节给'\0'结尾方便当字符串处理），
   然后立刻重新发起下一次单字节接收，从而形成连续接收的效果。 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance == USART2) {
        if (usart2_rx_length < USART2_RX_BUFFER_SIZE - 1U) {
            usart2_rx_buffer[usart2_rx_length++] = usart2_rx_byte;
            usart2_rx_buffer[usart2_rx_length] = '\0';
        }
        USART2_StartRxIT();
    }
}
/* USER CODE END 1 */

