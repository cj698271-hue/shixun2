/*
 * esp12f.c —— ESP-12F WiFi模块AT指令驱动实现
 *
 * 【工作方式】走的是普通AT指令模式（不是"透传"模式）：
 *   STM32通过USART2发一行文本指令（如"AT+CWJAP=\"ssid\",\"pwd\"\r\n"），
 *   ESP模块处理完后回复一段文本（通常以"OK\r\n"或"ERROR\r\n"结尾）。
 *   代码通过检测"收到的文本里是否包含期望的关键字"来判断指令是否成功。
 * 所有等待动作都是"轮询+超时"：不断检查有没有收到期待的文本，
 * 超过设定的超时时间就放弃并返回失败。
 */
#include "esp12f.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

#define ESP12F_RECEIVE_SNAPSHOT_SIZE 1024U

/* ESP模块出厂或用户设置过的常见波特率列表，初始化时按顺序逐个尝试，直到能收到回应为止 */
static const uint32_t esp12f_baud_rates[] = {
    115200U, 9600U, 57600U, 74880U, 38400U, 19200U, 230400U, 460800U
};
/* USART2驱动的环形接收区不能被协议解析直接长期引用；这里先复制一份“快照”，
   再在快照内查找AT回复或+IPD数据，防止解析过程中串口又收到新字节导致内容变化。 */
static uint8_t esp12f_received_buffer[ESP12F_RECEIVE_SNAPSHOT_SIZE];

/* 切换USART2的波特率（仅当传入的uart确实是USART2时才生效，做个防御性检查） */
static HAL_StatusTypeDef SetUartBaud(UART_HandleTypeDef *uart, uint32_t baud)
{
    if (uart == NULL || uart->Instance != USART2) {
        return HAL_ERROR;
    }
    return USART2_SetBaudRate(baud);
}

/*
 * 轮询等待，直到USART2接收缓冲区里出现指定的文本片段(expected)为止。
 * 这是所有"发AT指令后等回复"逻辑的通用等待部件。
 */
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

/*
 * 发送一条AT指令并等待期望的响应文本。
 * 每次发送前先清空接收缓冲区(USART2_RxClear)，避免被上一条指令残留的回复干扰判断。
 */
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
 *   1. 先等2秒，跳过模块刚上电时打印的开机启动信息（避免误判为AT指令回复）
 *   2. 依次尝试常见波特率列表里的每一种，每种波特率下发"AT"指令测试是否有回应
 *      （因为ESP模块出厂波特率不固定，或者之前被其他项目改过，需要自动探测）
 *   3. 找到能通信的波特率后，依次关闭指令回显(ATE0)、设为Station模式(CWMODE=1)、
 *      单连接模式(CIPMUX=0)、普通AT指令收发模式(CIPMODE=0)
 */
HAL_StatusTypeDef ESP12F_Init(ESP12F_Handle *handle, UART_HandleTypeDef *uart)
{
    size_t index;
    uint8_t retry;
    bool detected = false;

    if (handle == NULL || uart == NULL) {
        return HAL_ERROR;
    }
    handle->uart = uart;
    handle->transparent_mode = false;
    handle->wifi_connected = false;
    /* The ESP can emit boot text for well over one second after power-up. */
    HAL_Delay(2000U);
    /* ESP-12F firmware is often left at 9600 baud; find its current rate. */
    for (index = 0U; index < sizeof(esp12f_baud_rates) / sizeof(esp12f_baud_rates[0]); ++index) {
        if (SetUartBaud(uart, esp12f_baud_rates[index]) != HAL_OK) {
            continue;
        }
        HAL_Delay(300U);
        for (retry = 0U; retry < 2U; ++retry) {
            if (SendCommand(handle, "AT\r\n", "OK\r\n", 1500U) == HAL_OK) {
                detected = true;
                break;
            }
        }
        if (detected) {
            break;
        }
    }
    if (!detected) {
        return HAL_ERROR;
    }
    /* Keep the working baud rate and avoid a reset that can lose its reply. */
    return SendCommand(handle, "ATE0\r\n", "OK\r\n", 1000U) == HAL_OK &&
           SendCommand(handle, "AT+CWMODE=1\r\n", "OK\r\n", 1000U) == HAL_OK &&
           SendCommand(handle, "AT+CIPMUX=0\r\n", "OK\r\n", 1000U) == HAL_OK &&
           SendCommand(handle, "AT+CIPMODE=0\r\n", "OK\r\n", 1000U) == HAL_OK
           ? HAL_OK : HAL_ERROR;
}

/* 连接WiFi热点：如果之前已经记录为"已连接"，直接返回成功，不重复发指令 */
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
    /* 拼出 AT+CWJAP="ssid","password" 指令并发送，WiFi联网整体可能耗时较长，超时给20秒 */
    written = snprintf(command, sizeof(command), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, password);
    if (written < 0 || (size_t)written >= sizeof(command)) {
        return HAL_ERROR;
    }
    if (SendCommand(handle, command, "OK\r\n", 20000U) != HAL_OK) {
        return HAL_ERROR;
    }
    /* CWJAP may return before DHCP has finished; wait until STAIP exists. */
    /* 即使CWJAP返回OK，路由器分配IP地址(DHCP)可能还没完成，
       这里反复查询本地IP(AT+CIFSR)，看到"STAIP"字段出现才说明真正拿到了IP，可以用网了 */
    for (uint8_t retry = 0U; retry < 10U; ++retry) {
        if (SendCommand(handle, "AT+CIFSR\r\n", "STAIP", 2000U) == HAL_OK) {
            handle->wifi_connected = true;
            return HAL_OK;
        }
        HAL_Delay(1000U);
    }
    return HAL_ERROR;
}

/* 建立到指定服务器的TCP（或TLS）连接 */
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
    /* STM32单独复位（比如程序跑飞或手动按复位键）不会重启ESP模块，
       如果ESP上还残留着之前的TCP连接，这里先尝试关闭一次，即使失败也无所谓（忽略返回值） */
    (void)SendCommand(handle, "AT+CIPCLOSE\r\n", "OK\r\n", 1000U);
    HAL_Delay(100U);
    /* 拼出 AT+CIPSTART="TCP"/"SSL",host,port 指令 */
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

/* 占位函数：保留给上层调用方一个"进入透传模式"的接口，但本项目实际用的是普通AT指令模式，
   所以这里什么都不做，直接返回成功。之所以保留而不删除，是为了不改变上层调用代码的结构。 */
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

/* 关闭TCP连接（如果之前处于透传模式要先退出，再发关闭指令） */
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

/*
 * 通过已建立的TCP连接发送一段原始字节数据（这里发的是MQTT二进制报文，不是文本）。
 * ESP的AT+CIPSEND协议要求：
 *   1. 先发 AT+CIPSEND=<字节数>，ESP收到后会回一个">"提示符，表示"准备好了，开始发数据"
 *   2. 然后直接把原始字节流发出去（不需要额外加\r\n结尾）
 *   3. ESP确认发送完成后会回复"SEND OK"
 */
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

/*
 * 阻塞式读取：在timeout_ms毫秒内反复检查是否收到了完整的一帧下行数据，
 * 收到就立刻返回；超时还没收到就返回HAL_TIMEOUT。
 *
 * 【ESP模块收到TCP数据后的上报格式】
 * ESP不会直接把收到的原始数据转发给STM32，而是包一层自己的协议："+IPD,<长度>:<数据内容>"，
 * 例如服务器发了5个字节"HELLO"过来，STM32从串口收到的实际是"+IPD,5:HELLO"。
 * 所以这里要先在收到的原始文本流里找到"+IPD,"标记，解析出后面的数字（数据长度），
 * 再定位冒号后面对应长度的那一段，才是真正的数据内容。
 */
HAL_StatusTypeDef ESP12F_ReadRaw(ESP12F_Handle *handle, uint8_t *data, size_t capacity, size_t *length, uint32_t timeout_ms)
{
    uint32_t start;

    if (handle == NULL || data == NULL || length == NULL || capacity == 0U) {
        return HAL_ERROR;
    }
    *length = 0U;
    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        uint16_t received_length = USART2_RxCopy(esp12f_received_buffer,
                                                  (uint16_t)sizeof(esp12f_received_buffer));
        for (size_t index = 0U; index + 6U < received_length; ++index) {
            size_t cursor;
            size_t payload_length = 0U;
            /* 在缓冲区里逐字节扫描，查找"+IPD,"这个固定标记的起始位置 */
            if (memcmp(&esp12f_received_buffer[index], "+IPD,", 5U) != 0) {
                continue;
            }
            /* 找到标记后，紧跟着的是数字字符组成的数据长度，逐位累加解析成整数 */
            cursor = index + 5U;
            while (cursor < received_length && esp12f_received_buffer[cursor] >= '0' &&
                   esp12f_received_buffer[cursor] <= '9') {
                payload_length = payload_length * 10U + (size_t)(esp12f_received_buffer[cursor] - '0');
                ++cursor;
            }
            /* 长度数字后面必须紧跟一个冒号，且冒号后面剩余的字节数要够payload_length那么多，
               否则说明这一帧数据还没接收完整（粘包/半包），本次先跳过，等下次再检查 */
            if (cursor >= received_length || esp12f_received_buffer[cursor] != ':' ||
                payload_length == 0U || payload_length > capacity ||
                received_length - (cursor + 1U) < payload_length) {
                continue;
            }
            /* 冒号后面紧跟的正是数据本体，直接拷贝出来交给调用方 */
            memcpy(data, &esp12f_received_buffer[cursor + 1U], payload_length);
            *length = payload_length;
            return HAL_OK;
        }
        HAL_Delay(5U);
    }
    return HAL_TIMEOUT;
}
