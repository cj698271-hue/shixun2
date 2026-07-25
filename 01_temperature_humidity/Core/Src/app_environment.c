/*
 * app_environment.c —— 温湿度环境监测的核心业务逻辑
 *
 * 【这个文件做什么】
 * 把"读DHT11传感器数据"、"连WiFi"、"连MQTT云平台"、"上报数据"、"发心跳"
 * 这一整套流程串起来，是整个固件里最重要的业务代码。
 *
 * 【核心思路：没有真正的"状态机"，靠几个布尔标志位记录当前处于哪个阶段】
 * - mqtt_connected：是否已经和云平台建立了MQTT会话（true=已连接）
 * - dht_data_valid：当前缓存的温湿度数据是否还有效（避免每次上报都强制重新读传感器）
 * 每次 EnvironmentMonitor_Process() 被调用，都会检查这些标志位和计时器，
 * 决定这一次要不要读传感器、要不要发心跳、要不要触发一次完整的"连接+上报"流程。
 *
 * 【出错怎么办：goto cleanup】
 * MQTT连接、发布任一步失败，都会跳转到函数末尾的 cleanup 标签，
 * 把 mqtt_connected 重置为 false、关闭TCP连接。
 * 这样下一次 Process() 被调用时，会重新走一遍"连WiFi->连MQTT"的流程，
 * 相当于一种简单粗暴但有效的"失败重试"机制。
 */
#include "app_environment.h"
#include "app_config.h"
#include "dht11.h"
#include "debug_log.h"
#include "esp12f.h"
#include "mqtt_packet.h"
#include "oled_display.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MQTT_BUFFER_SIZE 512U          /* MQTT收发缓冲区大小（字节），装得下CONNECT/PUBLISH等报文 */
#define PAYLOAD_BUFFER_SIZE 256U       /* 拼装JSON上报内容的缓冲区大小 */
#define MQTT_PING_INTERVAL_MS 30000U   /* 每30秒发一次PINGREQ心跳，防止MQTT连接被服务器判定为超时断开 */
#define DHT_DISPLAY_INTERVAL_MS 1000U  /* 每1秒读一次DHT11并刷新OLED显示（比实际上报更频繁，用于实时显示） */

/* DMP平台身份信息打包结构：把连接MQTT要用到的几个字符串放一起，方便传递 */
typedef struct {
    const char *client_id;      /* MQTT客户端ID */
    const char *username;       /* MQTT用户名 */
    const char *password;       /* MQTT密码（HMAC签名或平台直发的密码） */
    const char *uplink_topic;   /* 属性上报主题（Topic） */
} DMP_Profile;

/*
 * 下面这一组 static 变量就是温湿度工程的“运行记忆”。
 * static 表示它们只属于本 .c 文件：main.c 不能直接修改，必须通过
 * EnvironmentMonitor_Init()/EnvironmentMonitor_Process() 间接使用，避免状态被误改。
 * 数据流：DHT11_Read() -> current_dht_data -> OLED/调试串口/JSON -> MQTT报文 -> ESP-12F。
 */
static ESP12F_Handle esp12f; /* ESP驱动句柄：保存USART2地址、Wi-Fi是否已连、当前是否透传等状态。由Init写入，后续所有ESP函数读取。 */
static const DHT11_Handle dht11 = {GPIOA, GPIO_PIN_0}; /* DHT11硬件描述：GPIOA是端口组，GPIO_PIN_0是PA0；const表示接线固定后不允许程序改引脚。 */
static uint32_t last_report_ms;     /* 上次开始上报的HAL毫秒计数；Process用“现在-此值”判断是否满1秒。初始化时故意减1个周期，以便开机立即上报。 */
static uint32_t last_ping_ms;       /* 上次成功发送MQTT PINGREQ的HAL毫秒计数；只在MQTT会话已建立后才有意义。 */
static uint32_t last_dht_sample_ms; /* 上次成功尝试读取DHT11的HAL毫秒计数；限制传感器、OLED、USART1刷新频率为1秒。 */
static uint32_t mqtt_message_sequence; /* JSON中的messageId来源；每次PublishProperty先自增再写入，便于平台区分连续的两条属性消息。 */
static bool mqtt_connected;         /* MQTT会话状态锁：false时执行Wi-Fi/TCP/CONNECT握手；true时直接发送业务数据。任一发送失败会被cleanup清回false。 */
static bool dht_data_valid;         /* 温湿度缓存有效标志：false代表还没有可上报的样本；true代表current_dht_data已经由DHT11_Read写入。 */
static DHT11_Data current_dht_data; /* 最近一次完整校验通过的DHT11样本；OLED、USART1和云端上报都从这同一份缓存读取，保证三处数据一致。 */
static uint8_t mqtt_packet_buffer[MQTT_BUFFER_SIZE]; /* 发送工作区：MQTT_BuildConnect/BuildPublish把二进制协议字节写进这里，随后ESP12F_SendRaw原样通过USART2发出。 */
static uint8_t mqtt_response_buffer[MQTT_BUFFER_SIZE]; /* 接收工作区：ESP12F_ReadRaw从“+IPD,长度:数据”中取出TCP负载放这里，随后解析CONNACK返回码。 */
static char property_payload_buffer[PAYLOAD_BUFFER_SIZE]; /* 文本工作区：snprintf先在这里生成JSON；它不是MQTT报文，之后才由MQTT_BuildPublish封装进mqtt_packet_buffer。 */

/* 本设备（温湿度传感器）在DMP平台上的身份信息，来自 app_config.h 里的宏定义 */
static const DMP_Profile temphum_profile = {
    DMP_TEMPHUM_MQTT_CLIENT_ID, /* 对应client_id：CONNECT报文中用于标识本次MQTT客户端会话。 */
    DMP_TEMPHUM_MQTT_USERNAME,  /* 对应username：由设备Key和产品Key组成，平台用它定位具体设备。 */
    DMP_TEMPHUM_MQTT_PASSWORD,  /* 对应password：HMAC计算或平台给出的最终密码，只从local_credentials.h读取。 */
    DMP_TEMPHUM_UPLINK_TOPIC    /* 对应uplink_topic：两条温/湿属性PUBLISH都发往这个平台主题。 */
};

/* 检查配置字符串是否还是"没有真正填写"的占位符（例如忘记替换的 "REPLACE_WITH_xxx"）。
   如果检测到占位符，说明设备配置没做完，直接拒绝联网，避免用假账号去连云平台。 */
static bool HasPlaceholder(const char *value)
{
    return value == NULL || value[0] == '\0' ||
           strncmp(value, "REPLACE_WITH_", sizeof("REPLACE_WITH_") - 1U) == 0;
}

/*
 * 定时读取DHT11并刷新OLED/调试日志显示。
 * 这个函数只负责"看得见"的实时显示，不负责上报云平台——
 * 上报是在 PublishTemperatureHumidity() 里单独做的，两者节奏可以不同。
 */
static void RefreshTemperatureHumidityDisplay(void)
{
    char diagnostic[56];

    /* 距离上次读取还没到1秒，直接跳过，避免过于频繁地读传感器（DHT11本身也有读取间隔限制） */
    if ((HAL_GetTick() - last_dht_sample_ms) < DHT_DISPLAY_INTERVAL_MS) {
        return;
    }
    last_dht_sample_ms = HAL_GetTick();
    if (DHT11_Read(&dht11, &current_dht_data) != HAL_OK) {
        DebugLog_Write("DHT11_FAIL: check PA0, power and pull-up resistor\r\n");
        return;
    }
    dht_data_valid = true;
    OLEDDisplay_ShowTemperatureHumidity(current_dht_data.temperature_c,
                                        current_dht_data.humidity_percent);
    (void)snprintf(diagnostic, sizeof(diagnostic), "TEMP: %.1fC  HUMI: %.1f%%\r\n",
                   (double)current_dht_data.temperature_c,
                   (double)current_dht_data.humidity_percent);
    DebugLog_Write(diagnostic);
}

/*
 * 上报单个属性（如"temperature"或"humidity"）到云平台。
 * 流程：拼一段DMP平台要求格式的JSON -> 用 MQTT_BuildPublish 打包成MQTT PUBLISH报文
 *      -> 通过 ESP12F_SendRaw 把原始字节通过AT指令发出去。
 * 这里用的是QoS0（发了就不管，不需要平台确认），所以调用一次只管发送成功与否。
 */
static HAL_StatusTypeDef PublishProperty(const char *key, float value)
{
    int payload_length;    /* snprintf返回的JSON实际字符数；用于确认没有被缓冲区截断。 */
    size_t packet_length;  /* MQTT_BuildPublish返回的二进制报文长度；0表示参数、长度或缓冲区检查失败。 */

    /* 拼装DMP平台要求的属性上报JSON格式：
       {"messageId":"序号","params":{"key":"属性名","value":数值}} */
    payload_length = snprintf(property_payload_buffer, sizeof(property_payload_buffer),
                               "{\"messageId\":\"%llu\",\"params\":{"
                               "\"key\":\"%s\",\"value\":%.1f}}",
                               (unsigned long long)++mqtt_message_sequence, key, (double)value);
    if (payload_length < 0 || (size_t)payload_length >= sizeof(property_payload_buffer)) {
        return HAL_ERROR;
    }
    /* 把JSON字符串打包成标准MQTT PUBLISH二进制报文 */
    packet_length = MQTT_BuildPublish(mqtt_packet_buffer, MQTT_BUFFER_SIZE,
                                       temphum_profile.uplink_topic, property_payload_buffer);
    if (packet_length == 0U ||
        ESP12F_SendRaw(&esp12f, mqtt_packet_buffer, packet_length) != HAL_OK) {
        return HAL_ERROR;
    }
    return HAL_OK;
}

/*
 * 核心函数：完成"确保已连接 -> 读传感器 -> 上报数据"的完整流程。
 * 每次被 EnvironmentMonitor_Process() 按周期调用一次（默认1秒一次）。
 *
 * 【连接部分只在 mqtt_connected==false 时才执行】
 * 也就是说：只有在"还没连接"或"上次连接失败被重置"的情况下，
 * 才会重新走一遍 连WiFi -> 开TCP -> 发MQTT CONNECT -> 等CONNACK 的完整握手；
 * 一旦连接成功，之后的每次调用会跳过这一整段，直接进入读数据、上报的环节，
 * 这样可以避免每秒都重新连接一次，浪费时间和流量。
 *
 * 【任何一步失败都走 goto cleanup】
 * cleanup 会把 mqtt_connected 复位为 false 并关闭TCP连接，
 * 下一次调用时就会重新触发上面的连接流程，形成简单的失败重试。
 */
static HAL_StatusTypeDef PublishTemperatureHumidity(void)
{
    DHT11_Data dht;              /* 从全局缓存复制出的本次上报快照；本函数后续始终使用同一份温湿度值。 */
    MQTT_ConnectOptions options; /* CONNECT报文的输入参数容器，只在首次建立MQTT会话时填写和使用。 */
    size_t packet_length;        /* 当前即将发送的CONNECT报文长度。 */
    size_t response_length;      /* ESP从TCP下行数据中实际取出的CONNACK字节数。 */
    uint8_t connack_code;        /* MQTT服务器的连接结果：0=认证和会话均被接受，其他值=拒绝原因。 */
    char diagnostic[48];         /* 临时格式化调试文本，只发送到USART1，不参与MQTT传输。 */
    HAL_StatusTypeDef status = HAL_ERROR; /* 默认失败；仅所有上报步骤完成后才改为HAL_OK，cleanup直接返回它。 */

    DebugLog_Write("REPORT: start\r\n");
    /* 配置检查：如果关键字段还是占位符（没有正确填写），直接放弃，不去联网 */
    if (HasPlaceholder(temphum_profile.client_id) ||
        HasPlaceholder(temphum_profile.username) ||
        HasPlaceholder(temphum_profile.password) ||
        HasPlaceholder(temphum_profile.uplink_topic)) {
        DebugLog_Write("CONFIG_FAIL: local credentials not set\r\n");
        return HAL_ERROR;
    }
    if (!mqtt_connected) {
        /* 第1步：连接WiFi热点（如果ESP12F内部已经记录着已连接的WiFi，这一步会直接返回成功） */
        DebugLog_Write("MQTT_CONNECT: opening session\r\n");
        if (ESP12F_ConnectWifi(&esp12f, WIFI_SSID, WIFI_PASSWORD) != HAL_OK) {
            DebugLog_Write("WIFI_FAIL: check hotspot is 2.4 GHz, SSID/password, ESP power\r\n");
            return HAL_ERROR;
        }
        DebugLog_Write("WIFI_OK\r\n");
        /* 第2步：向云平台的MQTT服务器地址建立TCP连接（AT+CIPSTART） */
        if (ESP12F_OpenTcp(&esp12f, DMP_MQTT_HOST, DMP_MQTT_PORT,
                           ESP12F_TLS_ENABLED != 0U) != HAL_OK) {
            DebugLog_Write("TCP_FAIL: Wi-Fi connected but DMP TCP connection failed\r\n");
            return HAL_ERROR;
        }
        DebugLog_Write("TCP_OK\r\n");
        /* 进入"透传模式"占位调用（当前实现里其实是空操作，因为走的是普通AT指令模式，见esp12f.c说明） */
        if (ESP12F_StartTransparentMode(&esp12f) != HAL_OK) {
            DebugLog_Write("TRANSPARENT_FAIL\r\n");
            goto cleanup;
        }

        /* 第3步：在TCP连接上发送MQTT协议的 CONNECT 报文，携带设备身份信息 */
        options.client_id = temphum_profile.client_id;
        options.username = temphum_profile.username;
        options.password = temphum_profile.password;
        options.keep_alive_seconds = MQTT_KEEP_ALIVE_SECONDS;
        packet_length = MQTT_BuildConnect(mqtt_packet_buffer, sizeof(mqtt_packet_buffer), &options);
        if (packet_length == 0U ||
            ESP12F_SendRaw(&esp12f, mqtt_packet_buffer, packet_length) != HAL_OK ||
            ESP12F_ReadRaw(&esp12f, mqtt_response_buffer, sizeof(mqtt_response_buffer),
                           &response_length, 3000U) != HAL_OK) {
            DebugLog_Write("MQTT_CONNECT_FAIL: no valid CONNACK\r\n");
            goto cleanup;
        }
        /* 第4步：检查服务器返回的CONNACK报文里的返回码，0表示"连接被接受" */
        connack_code = MQTT_GetConnAckReturnCode(mqtt_response_buffer, response_length);
        if (connack_code != 0U) {
            (void)snprintf(diagnostic, sizeof(diagnostic),
                           "MQTT_CONNECT_FAIL: CONNACK=%u\r\n", (unsigned int)connack_code);
            DebugLog_Write(diagnostic);
            goto cleanup;
        }
        mqtt_connected = true;
        last_ping_ms = HAL_GetTick();
        DebugLog_Write("MQTT_SESSION_OK\r\n");
    }
    /* 如果当前没有可用的温湿度数据缓存（比如刚开机、还没被 RefreshTemperatureHumidityDisplay 读过），
       就临时读一次，保证马上要上报的数据不是空的 */
    if (!dht_data_valid) {
        if (DHT11_Read(&dht11, &current_dht_data) != HAL_OK) {
            DebugLog_Write("DHT11_FAIL: check PA0, power and pull-up resistor\r\n");
            goto cleanup;
        }
        dht_data_valid = true;
        OLEDDisplay_ShowTemperatureHumidity(current_dht_data.temperature_c,
                                            current_dht_data.humidity_percent);
    }
    dht = current_dht_data;
    /* 第5步：分别把温度、湿度各打包成一条PUBLISH报文发出去（QoS0，发送即认为完成，不等待确认） */
    if (PublishProperty("temperature", dht.temperature_c) != HAL_OK ||
        PublishProperty("humidity", dht.humidity_percent) != HAL_OK) {
        DebugLog_Write("PUBLISH_FAIL: ESP MQTT send failed\r\n");
        goto cleanup;
    }
    DebugLog_Write("REPORT_OK\r\n");
    status = HAL_OK;
    return status;

/* 统一的失败处理出口：断开MQTT状态标记 + 关闭TCP连接，下次调用会重新连接 */
cleanup:
    mqtt_connected = false;
    (void)ESP12F_CloseTcp(&esp12f);
    return status;
}

/*
 * 模块初始化：由 main.c 在开机时调用一次。
 * 重置所有计时器和状态标志，并初始化DHT11传感器的GPIO配置和ESP-12F模块（扫描波特率、进入AT模式）。
 */
HAL_StatusTypeDef EnvironmentMonitor_Init(UART_HandleTypeDef *esp_uart)
{
    /* 减去一个周期，让程序刚启动就能立刻触发一次上报，不用等第一个周期结束 */
    last_report_ms = HAL_GetTick() - REPORT_INTERVAL_MS;
    last_ping_ms = HAL_GetTick();
    last_dht_sample_ms = HAL_GetTick() - DHT_DISPLAY_INTERVAL_MS;
    mqtt_message_sequence = 0U;
    mqtt_connected = false;
    dht_data_valid = false;
    return DHT11_Init(&dht11) == HAL_OK && ESP12F_Init(&esp12f, esp_uart) == HAL_OK
           ? HAL_OK : HAL_ERROR;
}

/*
 * 主循环里反复调用的"心脏"函数。每次调用做三件事（按顺序）：
 *   1. 刷新OLED上的实时温湿度显示（每1秒一次，与上报节奏独立）
 *   2. 如果已连接且距上次心跳超过30秒，发一次PINGREQ心跳保活
 *   3. 如果距上次上报满1秒（REPORT_INTERVAL_MS），触发一次完整的"连接+上报"流程
 * 每一步都是"检查时间是否到了 -> 没到就跳过"，所以这个函数被频繁调用完全没问题，
 * 真正的I/O动作只会按各自的周期发生。
 */
void EnvironmentMonitor_Process(void)
{
    size_t packet_length;

    RefreshTemperatureHumidityDisplay();
    /* 心跳保活：MQTT协议要求客户端在keep-alive周期内必须有数据往来，否则服务器会认为掉线。
       这里选择每30秒主动发一次PINGREQ，比keep-alive的60秒周期短，留足安全余量。 */
    if (mqtt_connected && (HAL_GetTick() - last_ping_ms) >= MQTT_PING_INTERVAL_MS) {
        packet_length = MQTT_BuildPingReq(mqtt_packet_buffer, sizeof(mqtt_packet_buffer));
        if (packet_length == 0U ||
            ESP12F_SendRaw(&esp12f, mqtt_packet_buffer, packet_length) != HAL_OK) {
            /* 心跳发送失败，说明连接可能已经断了，主动标记为未连接，下次上报时会重新连接 */
            DebugLog_Write("PING_FAIL: reconnecting on next report\r\n");
            mqtt_connected = false;
            (void)ESP12F_CloseTcp(&esp12f);
        } else {
            last_ping_ms = HAL_GetTick();
        }
    }
    /* 还没到上报周期（默认1秒），本次调用到这里就结束，什么都不做 */
    if ((HAL_GetTick() - last_report_ms) < REPORT_INTERVAL_MS) {
        return;
    }
    last_report_ms = HAL_GetTick();
    (void)PublishTemperatureHumidity();
}
