/*
 * esp12f.c —— ESP-12F WiFi模块AT指令驱动实现（MQ2工程版本）
 * 整体思路与01工程一致（普通AT指令模式，轮询+超时的方式等待回复），详见01工程esp12f.c顶部说明。
 * 本文件在波特率探测阶段增加了调试日志输出，并新增ESP12F_TryReadRaw做非阻塞式下行数据检查，
 * 因为这个工程需要在主循环里随时"顺手看一眼"有没有平台下发的新指令，不能像01那样只在特定时刻等待。
 */
#include "esp12f.h"
#include "debug_log.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

#define ESP12F_RECEIVE_SNAPSHOT_SIZE 1024U

/* ESP模块常见波特率列表，逐一尝试直到探测出当前生效的那一个 */
static const uint32_t esp12f_baud_rates[] = {
    115200U, 9600U, 57600U, 74880U, 38400U, 19200U, 230400U, 460800U
};
/* USART2驱动的环形接收区不能被协议解析直接长期引用；这里先复制一份“快照”，
   再在快照内查找AT回复或+IPD数据，防止解析过程中串口又收到新字节导致内容变化。 */
static uint8_t esp12f_received_buffer[ESP12F_RECEIVE_SNAPSHOT_SIZE];

/* 切换USART2波特率（防御性检查：确保传入的确实是USART2） */
static HAL_StatusTypeDef SetUartBaud(UART_HandleTypeDef *uart, uint32_t baud)
{
    if (uart == NULL || uart->Instance != USART2) {
        return HAL_ERROR;
    }
    return USART2_SetBaudRate(baud);
}

/* 轮询等待接收缓冲区里出现指定文本，超时放弃 */
static HAL_StatusTypeDef WaitForText(ESP12F_Handle *handle, const char *expected, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    (void)handle;

    while ((HAL_GetTick() - start) < timeout_ms) {
        if (USART2_RxContains(expected)) {
            return HAL_OK;
        }
        if (USART2_RxHasError()) {
            return HAL_ERROR;
        }
        HAL_Delay(5U);
    }
    return HAL_TIMEOUT;
}

/* 发送一条AT指令并等待期望的响应文本 */
static HAL_StatusTypeDef SendCommand(ESP12F_Handle *handle, const char *command, const char *expected, uint32_t timeout_ms)
{
    size_t length = strlen(command); /* AT命令的实际字节数；命令参数已包含结束符，ESP据此识别一条完整AT指令。 */

    USART2_RxClear(); /* 先丢弃旧回复，确保WaitForText匹配到的一定是本次命令的新回复。 */
    if (HAL_UART_Transmit(handle->uart, (uint8_t *)command, (uint16_t)length, 1000U) != HAL_OK) {
        return HAL_ERROR;
    }
    return WaitForText(handle, expected, timeout_ms);
}

/*
 * 初始化ESP-12F模块：
 *   1. 等2秒跳过开机启动信息
 *   2. 逐个尝试波特率列表，直到发出"AT"后能收到任何回应（哪怕不是标准的"OK\r\n"，
 *      只要缓冲区里出现了任何字节也算探测成功——因为有些模块固件返回格式并不完全标准，
 *      "有没有收到东西"比"是否精确匹配OK"更可靠地说明这个波特率是对的）
 *   3. 依次关闭回显(ATE0)、关闭+IPD前缀里的额外信息(CIPDINFO=0)、设为Station模式(CWMODE=1)、
 *      单连接模式(CIPMUX=0)、AT指令收发模式(CIPMODE=0)
 * 后面几个非关键指令(CIPDINFO/CIPMUX/CIPMODE)即使失败也不当作初始化失败(忽略返回值)，
 * 因为它们大多是"设成默认值"性质的指令，即使设置失败通常模块本身默认值也是符合要求的。
 */
HAL_StatusTypeDef ESP12F_Init(ESP12F_Handle *handle, UART_HandleTypeDef *uart)
{
    size_t index;
    uint8_t retry;
    uint16_t response_length;
    bool detected = false;

    if (handle == NULL || uart == NULL) {
        return HAL_ERROR;
    }
    handle->uart = uart;
    handle->transparent_mode = false;
    handle->wifi_connected = false;
    /* The ESP can emit boot text for well over one second after power-up. */
    HAL_Delay(2000U);
    DebugLog_Write("ESP_AT: scanning baud rates\r\n");

    /* Match the verified reference: any response identifies the ESP baud rate. */
    for (index = 0U; index < sizeof(esp12f_baud_rates) / sizeof(esp12f_baud_rates[0]); ++index) {
        if (SetUartBaud(uart, esp12f_baud_rates[index]) != HAL_OK) {
            continue;
        }
        HAL_Delay(300U);
        for (retry = 0U; retry < 2U; ++retry) {
            USART2_RxClear();
            if (SendCommand(handle, "AT\r\n", "OK\r\n", 1500U) == HAL_OK) {
                detected = true;
                break;
            }
            /* 严格匹配"OK\r\n"失败了，退一步看看是不是"收到了什么但格式不完全标准"，
               只要缓冲区里有任何字节就足以证明波特率是匹配的 */
            response_length = USART2_RxCopy(esp12f_received_buffer,
                                            (uint16_t)sizeof(esp12f_received_buffer));
            if (response_length > 0U) {
                detected = true;
                break;
            }
        }
        if (detected) {
            break;
        }
    }
    if (!detected) {
        DebugLog_Write("ESP_INIT_STAGE: no AT response\r\n");
        return HAL_ERROR;
    }

    DebugLog_Write("ESP_AT: response detected\r\n");
    if (SendCommand(handle, "ATE0\r\n", "OK\r\n", 1000U) != HAL_OK) {
        DebugLog_Write("ESP_INIT_STAGE: ATE0\r\n");
        return HAL_ERROR;
    }
    (void)SendCommand(handle, "AT+CIPDINFO=0\r\n", "OK\r\n", 1000U);
    if (SendCommand(handle, "AT+CWMODE=1\r\n", "OK\r\n", 1000U) != HAL_OK) {
        DebugLog_Write("ESP_INIT_STAGE: CWMODE\r\n");
        return HAL_ERROR;
    }
    (void)SendCommand(handle, "AT+CIPMUX=0\r\n", "OK\r\n", 1000U);
    (void)SendCommand(handle, "AT+CIPMODE=0\r\n", "OK\r\n", 1000U);
    return HAL_OK;
}

/* 连接WiFi热点，已连接过就直接返回成功 */
HAL_StatusTypeDef ESP12F_ConnectWifi(ESP12F_Handle *handle, const char *ssid, const char *password)
{
    char command[196];
    int written;

    if (handle == NULL || ssid == NULL || password == NULL) {
        return HAL_ERROR;
    }
    if (handle->wifi_connected) {
        return HAL_OK;
    }
    written = snprintf(command, sizeof(command), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, password);
    if (written < 0 || (size_t)written >= sizeof(command)) {
        return HAL_ERROR;
    }
    if (SendCommand(handle, command, "OK\r\n", 20000U) != HAL_OK) {
        return HAL_ERROR;
    }
    /* CWJAP may return before DHCP has finished; wait until STAIP exists. */
    /* CWJAP返回OK不代表DHCP拿到IP已经完成，反复查询AT+CIFSR直到看到"STAIP"字段出现 */
    for (uint8_t retry = 0U; retry < 10U; ++retry) {
        if (SendCommand(handle, "AT+CIFSR\r\n", "STAIP", 2000U) == HAL_OK) {
            handle->wifi_connected = true;
            return HAL_OK;
        }
        HAL_Delay(1000U);
    }
    return HAL_ERROR;
}

/* 建立到指定服务器的TCP/TLS连接 */
HAL_StatusTypeDef ESP12F_OpenTcp(ESP12F_Handle *handle, const char *host, uint16_t port, bool tls)
{
    char command[160];
    int written;

    if (handle == NULL || host == NULL) {
        return HAL_ERROR;
    }
    if (SendCommand(handle, "AT+CIPMUX=0\r\n", "OK\r\n", 1000U) != HAL_OK) {
        handle->wifi_connected = false;
        return HAL_ERROR;
    }
    /* A standalone STM32 reset can leave an old ESP TCP socket open. */
    /* STM32单独复位不会重启ESP模块，若上面残留旧连接先尝试关闭一次，忽略失败结果 */
    (void)SendCommand(handle, "AT+CIPCLOSE\r\n", "OK\r\n", 1000U);
    HAL_Delay(100U);
    written = snprintf(command, sizeof(command), "AT+CIPSTART=\"%s\",\"%s\",%u\r\n",
                       tls ? "SSL" : "TCP", host, (unsigned int)port);
    if (written < 0 || (size_t)written >= sizeof(command)) {
        return HAL_ERROR;
    }
    if (SendCommand(handle, command, "OK\r\n", 10000U) != HAL_OK) {
        handle->wifi_connected = false;
        return HAL_ERROR;
    }
    return HAL_OK;
}

/* 占位函数：本项目走普通AT模式，不使用透传，保留只是为了兼容上层调用接口 */
HAL_StatusTypeDef ESP12F_StartTransparentMode(ESP12F_Handle *handle)
{
    /* Kept for the application API; the reference firmware uses normal mode. */
    return handle == NULL ? HAL_ERROR : HAL_OK;
}

HAL_StatusTypeDef ESP12F_ExitTransparentMode(ESP12F_Handle *handle)
{
    if (handle == NULL) {
        return HAL_ERROR;
    }
    handle->transparent_mode = false;
    return HAL_OK;
}

/* 关闭TCP连接 */
HAL_StatusTypeDef ESP12F_CloseTcp(ESP12F_Handle *handle)
{
    if (handle == NULL) {
        return HAL_ERROR;
    }
    if (handle->transparent_mode && ESP12F_ExitTransparentMode(handle) != HAL_OK) {
        return HAL_ERROR;
    }
    return SendCommand(handle, "AT+CIPCLOSE\r\n", "OK\r\n", 3000U);
}

/* 通过AT+CIPSEND通道发送一段原始字节数据（发的是MQTT二进制报文） */
HAL_StatusTypeDef ESP12F_SendRaw(ESP12F_Handle *handle, const uint8_t *data, size_t length)
{
    char command[32]; /* 存放AT+CIPSEND=<长度>命令；这里不是待发送的MQTT二进制数据本身。 */
    int written;      /* snprintf返回的字符数，用于确认CIPSEND命令没有被截断。 */

    if (handle == NULL || data == NULL || length == 0U || length > 65535U) {
        return HAL_ERROR;
    }
    written = snprintf(command, sizeof(command), "AT+CIPSEND=%lu\r\n", (unsigned long)length);
    if (written < 0 || (size_t)written >= sizeof(command) ||
        SendCommand(handle, command, ">", 5000U) != HAL_OK) {
        return HAL_ERROR;
    }
    /* data通常指向mqtt_packet_buffer，内容可能有0x00等二进制字节，因此必须按length发送，不能用strlen。 */
    if (HAL_UART_Transmit(handle->uart, (uint8_t *)data, (uint16_t)length, 5000U) != HAL_OK) {
        return HAL_ERROR;
    }
    return WaitForText(handle, "SEND OK\r\n", 5000U);
}

/* 阻塞式读取：在timeout_ms内反复调用非阻塞版本ESP12F_TryReadRaw，直到读到数据或超时 */
HAL_StatusTypeDef ESP12F_ReadRaw(ESP12F_Handle *handle, uint8_t *data, size_t capacity, size_t *length, uint32_t timeout_ms)
{
    uint32_t start;

    if (handle == NULL || data == NULL || length == NULL || capacity == 0U) {
        return HAL_ERROR;
    }
    *length = 0U;
    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        if (ESP12F_TryReadRaw(handle, data, capacity, length) == HAL_OK) {
            return HAL_OK;
        }
        HAL_Delay(5U);
    }
    return HAL_TIMEOUT;
}

/*
 * 非阻塞地检查一次：当前接收缓冲区里有没有一帧完整的"+IPD,<长度>:<数据>"下行数据。
 * 没有就立刻返回HAL_TIMEOUT（这里名字虽然叫TIMEOUT，其实是"这次没有数据"，
 * 因为该函数本身不等待，调用方(比如ESP12F_ReadRaw)可能会自己决定要不要重试）。
 *
 * 与01工程中的ESP12F_ReadRaw相比，这里多了一步 USART2_RxDiscard：
 * 找到并取出一帧数据后，把接收缓冲区里"已经被消费的这部分"（包括+IPD,前缀本身）主动丢弃掉，
 * 而不是留在缓冲区里。这是因为MQ2工程需要频繁、持续地轮询下行数据（每个主循环周期都要检查），
 * 如果不丢弃已处理的数据，缓冲区会越攒越多、旧数据会一直挡在前面干扰下一次的"+IPD,"扫描，
 * 而01工程的ESP12F_ReadRaw只在特定时刻（等CONNACK等）调用一次，用完整个缓冲区就会被
 * 下一次SendCommand里的USART2_RxClear清空，所以不需要单独丢弃。
 */
HAL_StatusTypeDef ESP12F_TryReadRaw(ESP12F_Handle *handle, uint8_t *data, size_t capacity, size_t *length)
{
    uint16_t received_length;
    size_t index;

    if (handle == NULL || data == NULL || length == NULL || capacity == 0U) {
        return HAL_ERROR;
    }
    *length = 0U;
    received_length = USART2_RxCopy(esp12f_received_buffer,
                                     (uint16_t)sizeof(esp12f_received_buffer));
    for (index = 0U; index + 6U < received_length; ++index) {
        size_t cursor;
        size_t payload_length = 0U;

        /* 查找"+IPD,"标记 */
        if (memcmp(&esp12f_received_buffer[index], "+IPD,", 5U) != 0) {
            continue;
        }
        /* 解析紧跟着的数字，得到数据长度 */
        cursor = index + 5U;
        while (cursor < received_length && esp12f_received_buffer[cursor] >= '0' &&
               esp12f_received_buffer[cursor] <= '9') {
            payload_length = payload_length * 10U +
                             (size_t)(esp12f_received_buffer[cursor] - '0');
            ++cursor;
        }
        /* 长度后面要紧跟冒号，且冒号后剩余字节要够长，否则说明这一帧还没收完整，跳过等下次再检查 */
        if (cursor >= received_length || esp12f_received_buffer[cursor] != ':' ||
            payload_length == 0U || payload_length > capacity ||
            received_length - (cursor + 1U) < payload_length) {
            continue;
        }
        memcpy(data, &esp12f_received_buffer[cursor + 1U], payload_length);
        *length = payload_length;
        /* 把这一帧(包括"+IPD,长度:"前缀和数据本身)从底层接收缓冲区里丢弃，避免重复处理 */
        USART2_RxDiscard((uint16_t)(cursor + 1U + payload_length));
        return HAL_OK;
    }
    return HAL_TIMEOUT;
}
