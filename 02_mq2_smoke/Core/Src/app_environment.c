/*
 * app_environment.c —— MQ2烟雾报警的核心业务逻辑
 *
 * 【和01工程(温湿度)的相同之处】
 * 同样是"读传感器->连WiFi->连MQTT->上报"的流程，用同一套 mqtt_connected/goto cleanup
 * 的失败重试模式（详见01工程app_environment.c顶部的说明，此处不重复）。
 *
 * 【这个工程比01多出来的部分：接收云平台下发的控制指令】
 * MQ2工程不仅"上报"数据，还要"接收"云平台发来的服务调用指令(比如"打开蜂鸣器")，
 * 所以多了一套完整的：订阅Topic -> 解析收到的MQTT报文 -> 手写JSON解析取出指令参数
 * -> 执行动作(控制蜂鸣器GPIO) -> 回复执行结果给云平台，这一整条"下行控制链路"。
 * 这也是为什么这个文件里会有 JsonReadFieldValue 这种手写的迷你JSON解析器。
 */
#include "app_environment.h"
#include "app_config.h"
#include "debug_log.h"
#include "esp12f.h"
#include "main.h"
#include "mq2.h"
#include "mqtt_packet.h"
#include "oled_display.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MQTT_BUFFER_SIZE 512U           /* MQTT收发缓冲区大小 */
#define PAYLOAD_BUFFER_SIZE 192U        /* JSON载荷拼装缓冲区大小 */
#define MQTT_PING_INTERVAL_MS 30000U    /* 心跳间隔30秒 */
#define MQTT_SERVICE_TIMEOUT_MS 3000U   /* 等待SUBACK确认的超时时间 */
#define MQTT_STREAM_BUFFER_SIZE 768U     /* 下行数据流的暂存缓冲区（处理TCP粘包/半包问题） */
#define MQTT_MESSAGE_ID_SIZE 16U        /* 服务指令里messageId字段的最大长度 */
#define MQTT_VALUE_SIZE 24U             /* JSON字段值解析出来的临时存放长度 */
#define SMOKE_DEBUG_INTERVAL_MS 1000U   /* 每1秒刷新一次OLED烟雾浓度显示 */
#define SERVICE_DEBUG_PAYLOAD_MAX 180U  /* 调试打印服务指令内容时最多打印多少字节 */

/* 本设备（MQ2烟雾报警器）在DMP平台上的身份信息，比01工程多了两个"服务"相关的Topic */
typedef struct {
    const char *client_id;           /* MQTT CONNECT中的客户端会话名；平台据此识别接入设备。 */
    const char *username;            /* MQTT CONNECT中的用户名；由设备Key和产品Key拼接。 */
    const char *password;            /* MQTT CONNECT中的最终认证密码；来自私有local_credentials.h。 */
    const char *uplink_topic;         /* 属性上报主题 */
    const char *service_topic;        /* 订阅这个主题接收云平台下发的服务指令 */
    const char *service_reply_topic;  /* 执行完指令后往这个主题回复结果 */
} DMP_Profile;

/*
 * MQ2工程的共享运行状态。它们共同构成两条信息通道：
 * 上行：ADC -> current_smoke_concentration -> JSON -> mqtt_packet_buffer -> ESP-12F -> DMP；
 * 下行：ESP-12F +IPD数据 -> mqtt_response_buffer -> mqtt_stream_buffer -> MQTT解析 -> PA8蜂鸣器。
 */
static ESP12F_Handle esp12f; /* ESP-12F驱动句柄：由EnvironmentMonitor_Init写入USART2，网络函数通过它找到串口和连接状态。 */
static ADC_HandleTypeDef *mq2_adc; /* ADC1句柄指针：main.c把CubeMX初始化完成的hadc1传进来，MQ2_ReadRaw通过它启动PA0的模数转换。 */
static uint32_t last_report_ms; /* 上一次启动属性上报的HAL毫秒计数；用于实现REPORT_INTERVAL_MS=1000的节拍。 */
static uint32_t last_ping_ms; /* 上一次成功发送PINGREQ的HAL毫秒计数；通信空闲时靠它维持MQTT会话。 */
static uint32_t last_smoke_debug_ms; /* 上一次读取ADC并刷新OLED/USART1的HAL毫秒计数；显示和云端上报虽然都为1秒，但各自独立计时。 */
static uint32_t mqtt_message_sequence; /* 上行属性JSON的messageId计数器；每发送一条烟雾数据加1，平台可据此区分消息。 */
static uint16_t current_smoke_concentration; /* 最近一次换算后的0~500显示/上报值，而非原始0~4095 ADC值；OLED、串口、平台都读取它。 */
static bool mqtt_connected; /* MQTT会话是否可用；false会触发Wi-Fi、TCP、CONNECT整套握手，失败或心跳失败时会被清零。 */
static bool mqtt_service_subscribed; /* 服务Topic是否已收到SUBACK；只有true才允许轮询下行命令，避免在未订阅状态误读普通网络数据。 */
static bool smoke_concentration_valid; /* current_smoke_concentration是否已有一次成功ADC采样；false时上报函数会强制读一次，避免发送未初始化数据。 */
static uint16_t mqtt_packet_id; /* QoS1专用报文序号；SUBSCRIBE、服务回复和PUBACK用它关联协议确认，协议规定0不能使用。 */
static uint8_t mqtt_packet_buffer[MQTT_BUFFER_SIZE]; /* 发送二进制工作区：CONNECT、PUBLISH、SUBSCRIBE、PINGREQ、PUBACK均先写入此数组，再经ESP12F_SendRaw发送。 */
static uint8_t mqtt_response_buffer[MQTT_BUFFER_SIZE]; /* 一次性接收工作区：等待CONNACK或SUBACK时，ESP12F_ReadRaw把当前一帧TCP有效负载复制到这里供检查。 */
static uint8_t mqtt_stream_buffer[MQTT_STREAM_BUFFER_SIZE]; /* 下行流工作区：持续累积TCP分段/粘包，只有攒够一整条MQTT报文才交给ProcessMqttPacket，防止半包误解析。 */
static size_t mqtt_stream_length; /* mqtt_stream_buffer当前已占用字节数；每处理一条完整报文后会减去该报文长度并把剩余数据前移。 */
static char property_payload_buffer[PAYLOAD_BUFFER_SIZE]; /* 上行JSON文本工作区：snprintf在此生成{"messageId":...,"params":...}，随后再封装成MQTT二进制报文。 */

static const DMP_Profile mq2_profile = {
    DMP_MQ2_MQTT_CLIENT_ID,             /* CONNECT.client_id：必须和DMP平台实际注册的设备名/产品Key格式一致。 */
    DMP_MQ2_MQTT_USERNAME,              /* CONNECT.username：设备Key|产品Key。 */
    DMP_MQ2_PLATFORM_PASSWORD,          /* CONNECT.password：最终HMAC或平台直发密码。 */
    DMP_MQ2_UPLINK_TOPIC,               /* 属性上行主题：烟雾浓度按1秒周期发布到这里。 */
    DMP_MQ2_ASYNC_SERVICE_TOPIC,        /* 服务下行主题：SUBSCRIBE成功后，平台的开关蜂鸣器指令会从这里到达。 */
    DMP_MQ2_ASYNC_SERVICE_REPLY_TOPIC   /* 服务回复主题：设备执行PA8动作后，将成功/失败和messageId回传到这里。 */
};

static bool HasPlaceholder(const char *value)
{
    return value == NULL || value[0] == '\0' ||
           strncmp(value, "REPLACE_WITH_", sizeof("REPLACE_WITH_") - 1U) == 0;
}

/* 生成下一个MQTT报文ID（用于QoS1报文的确认匹配）。MQTT规定packet_id不能为0，
   所以自增后如果恰好绕回到0，就再加1跳过它。 */
static uint16_t NextPacketId(void)
{
    ++mqtt_packet_id;
    if (mqtt_packet_id == 0U) {
        ++mqtt_packet_id;
    }
    return mqtt_packet_id;
}

/* 控制蜂鸣器GPIO，并同步更新OLED显示和调试日志 */
static void SetBeeper(bool enabled)
{
    /* PA8由CubeMX配置为推挽输出；本硬件约定高电平=蜂鸣器响、低电平=停止。 */
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
    /* OLED和串口只反映已实际写入GPIO后的状态，因此现场看到的BEEP:ON/OFF与PA8电平一致。 */
    OLEDDisplay_ShowBeeper(enabled);
    DebugLog_Write(enabled ? "BEEP_ON: PA8=HIGH\r\n" : "BEEP_OFF: PA8=LOW\r\n");
}

/* 读取MQ2原始ADC值，并线性映射到 0~MQ2_PLATFORM_MAX_CONCENTRATION 的范围，
   作为上报给云平台的"浓度"数值（注意：这只是按ADC量程做的线性缩放，不是经过专业校准的真实ppm值）。
   "+ (满量程/2)"是四舍五入的常见技巧，让整数除法结果更接近真实比例，而不是总是向下截断。 */
static HAL_StatusTypeDef ReadSmokeConcentration(uint16_t *smoke_concentration)
{
    uint16_t mq2_raw;

    if (smoke_concentration == NULL || MQ2_ReadRaw(mq2_adc, &mq2_raw) != HAL_OK) {
        return HAL_ERROR;
    }
    *smoke_concentration = (uint16_t)(((uint32_t)mq2_raw * MQ2_PLATFORM_MAX_CONCENTRATION +
                                       (MQ2_ADC_FULL_SCALE / 2U)) / MQ2_ADC_FULL_SCALE);
    return HAL_OK;
}

/* 每1秒读一次烟雾浓度并刷新OLED显示/打印调试日志，节奏与实际上报周期独立 */
static void ReportSmokeDebug(void)
{
    uint16_t smoke_concentration;
    char diagnostic[48];

    if ((HAL_GetTick() - last_smoke_debug_ms) < SMOKE_DEBUG_INTERVAL_MS) {
        return;
    }
    last_smoke_debug_ms = HAL_GetTick();
    if (ReadSmokeConcentration(&smoke_concentration) != HAL_OK) {
        DebugLog_Write("SMOKE_READ_FAIL: check PA0 analog input\r\n");
        return;
    }
    current_smoke_concentration = smoke_concentration;
    smoke_concentration_valid = true;
    (void)snprintf(diagnostic, sizeof(diagnostic),
                   "SMOKE: %u (scale 0-500)\r\n", (unsigned int)smoke_concentration);
    OLEDDisplay_ShowSmoke(smoke_concentration);
    DebugLog_Write(diagnostic);
}

/* 把收到的服务指令原始载荷打印到调试口，方便排查问题。
   非可打印字符（比如二进制垂圾数据）替换成'.'，避免把终端刷乱。 */
static void DebugServicePayload(const uint8_t *payload, size_t payload_length)
{
    char diagnostic[SERVICE_DEBUG_PAYLOAD_MAX + 16U];
    size_t copy_length;
    size_t index;

    if (payload == NULL) {
        return;
    }
    memcpy(diagnostic, "SERVICE_RX: ", sizeof("SERVICE_RX: ") - 1U);
    copy_length = payload_length;
    if (copy_length > SERVICE_DEBUG_PAYLOAD_MAX) {
        copy_length = SERVICE_DEBUG_PAYLOAD_MAX;
    }
    for (index = 0U; index < copy_length; ++index) {
        uint8_t value = payload[index];

        diagnostic[sizeof("SERVICE_RX: ") - 1U + index] =
            value >= 0x20U && value <= 0x7EU ? (char)value : '.';
    }
    diagnostic[sizeof("SERVICE_RX: ") - 1U + copy_length] = '\r';
    diagnostic[sizeof("SERVICE_RX: ") + copy_length] = '\n';
    diagnostic[sizeof("SERVICE_RX: ") + copy_length + 1U] = '\0';
    DebugLog_Write(diagnostic);
}

/*
 * 手写的迷你JSON字段提取函数：在payload里查找 "field":值 这种模式，取出"值"部分。
 * 这不是一个通用的JSON解析器，只是"够用就好"——因为MCU资源有限，
 * 不值得为了解析几个简单字段引入完整的JSON解析库。
 *
 * 用法：从start位置开始扫描，找到第一个匹配的 "field" 键之后，
 * 跳过冒号和空白，再读取它的值（可能是带引号的字符串，也可能是数字/布尔等无引号的值），
 * 一直读到遇到引号收尾（带引号情况）或遇到逗号/大括号/空白（无引号情况）为止。
 * 通过next指针把"这个值读完后的位置"传出去，方便调用方从这个位置继续查找下一个字段
 * （因为一次PUBLISH的payload里可能有多个同名或不同名的字段，需要按顺序扫描）。
 */
static bool JsonReadFieldValue(const uint8_t *payload, size_t payload_length,
                               size_t start, const char *field,
                               char *value, size_t value_capacity, size_t *next)
{
    size_t field_length;
    size_t index;
    size_t cursor;
    size_t output_length = 0U;
    bool quoted = false;

    if (payload == NULL || field == NULL || value == NULL || value_capacity < 2U) {
        return false;
    }
    field_length = strlen(field);
    /* 逐位置扫描，寻找形如 "field" 这样的带引号键名（前后都是双引号包住的field字符串） */
    for (index = start; index + field_length + 2U < payload_length; ++index) {
        if (payload[index] != '"' || memcmp(&payload[index + 1U], field, field_length) != 0 ||
            payload[index + field_length + 1U] != '"') {
            continue;
        }
        /* 找到键名了，跳过键名后面的空白，接下来应该是冒号 */
        cursor = index + field_length + 2U;
        while (cursor < payload_length && (payload[cursor] == ' ' || payload[cursor] == '\t' ||
                                           payload[cursor] == '\r' || payload[cursor] == '\n')) {
            ++cursor;
        }
        if (cursor >= payload_length || payload[cursor] != ':') {
            continue;   /* 没有冒号，说明这不是一个真正的"键:值"结构，可能是巧合的子串，继续找下一个 */
        }
        ++cursor;
        /* 跳过冒号后面的空白，定位到真正的值开始的位置 */
        while (cursor < payload_length && (payload[cursor] == ' ' || payload[cursor] == '\t' ||
                                           payload[cursor] == '\r' || payload[cursor] == '\n')) {
            ++cursor;
        }
        if (cursor >= payload_length) {
            return false;
        }
        /* 判断值是不是字符串（带双引号）：如果是，记录下来，后面要靠"再遇到双引号"判断值结束 */
        if (payload[cursor] == '"') {
            quoted = true;
            ++cursor;
        }
        /* 逐字符拷贝值内容，直到遇到收尾标记为止：
           带引号的值遇到下一个双引号结束；不带引号的值遇到逗号/大括号/空白结束 */
        while (cursor < payload_length) {
            uint8_t current = payload[cursor];

            if ((quoted && current == '"') || (!quoted &&
                (current == ',' || current == '}' || current == ']' || current == ' ' ||
                 current == '\t' || current == '\r' || current == '\n'))) {
                break;
            }
            if (output_length + 1U >= value_capacity) {
                return false;   /* 值太长，超出调用方给的缓冲区容量，放弃这次解析 */
            }
            value[output_length++] = (char)current;
            ++cursor;
        }
        if (quoted && (cursor >= payload_length || payload[cursor] != '"')) {
            return false;   /* 带引号的值没有正常的收尾引号，说明数据不完整或格式有问题 */
        }
        value[output_length] = '\0';
        if (next != NULL) {
            /* 把"这个值读完之后"的位置传出去，调用方可以从这里继续扫描下一个字段 */
            *next = cursor < payload_length ? cursor + 1U : cursor;
        }
        return output_length > 0U;
    }
    return false;
}

/* 把operationCode的字符串值（"0"或"1"）解析成布尔值：1=开启蜂鸣器，0=关闭。
   任何非0/1的值都视为无效指令，直接拒绝（防止平台传来意料之外的值导致误操作）。 */
static bool ParseOperationCode(const char *value, bool *enabled)
{
    char *end;
    long operation_code;

    if (value == NULL || enabled == NULL || *value == '\0') {
        return false;
    }
    operation_code = strtol(value, &end, 10);
    if (*end != '\0' || (operation_code != 0L && operation_code != 1L)) {
        return false;
    }
    *enabled = operation_code == 1L;
    return true;
}

/*
 * 从云平台下发的服务指令JSON里解析出"是否要开启蜂鸣器"。
 * DMP平台的服务调用指令大致长这样（简化版）：
 *   {"messageId":"123","params":{"key":"operationCode","value":1}}
 * 需要找到 key=="operationCode" 的那个对象，再取它同一层的"value"字段。
 *
 * 【为什么要往前找"{"来确定object_start】
 * 因为一个JSON对象里可能不止一层，"key"和"value"字段的先后顺序在不同平台/版本里也可能不一致
 * （有的实现会把value写在key前面）。JsonReadFieldValue是"从某个位置往后线性扫描"的简单实现，
 * 不理解JSON的嵌套结构，所以这里手动往前找最近的一个"{"，把扫描起点退回到"当前这个对象刚开始"的位置，
 * 这样不管value写在key前面还是后面，都能在"同一个对象范围内"被正确找到，
 * 避免跑到别的对象或字段里去误读了不相关的"value"。
 */
static bool ParseBeeperCommand(const uint8_t *payload, size_t payload_length,
                               char *message_id, size_t message_id_capacity, bool *enabled)
{
    char field_value[MQTT_VALUE_SIZE];
    char operation_value[MQTT_VALUE_SIZE];
    size_t cursor = 0U;
    size_t value_cursor;
    size_t object_start;
    size_t index;
    bool operation_code_found = false;

    if (message_id == NULL || message_id_capacity < 2U || enabled == NULL) {
        return false;
    }
    message_id[0] = '\0';
    /* messageId是顶层字段，直接从头找一次即可，回复时要原样带回给平台 */
    (void)JsonReadFieldValue(payload, payload_length, 0U, "messageId",
                             message_id, message_id_capacity, NULL);
    /* 依次找出payload里所有的"key"字段，逐个检查它的值是不是我们关心的operationCode */
    while (JsonReadFieldValue(payload, payload_length, cursor, "key",
                              field_value, sizeof(field_value), &value_cursor)) {
        if (strcmp(field_value, DMP_MQ2_SERVICE_PARAMETER_KEY) == 0) {
            /* The DMP service parameter may put "value" before or after "key". */
            /* 找到了目标key，往前回退找到本对象开始的"{"，把搜索范围收窄到这个对象内部 */
            object_start = cursor;
            for (index = value_cursor; index > cursor; --index) {
                if (payload[index - 1U] == '{') {
                    object_start = index - 1U;
                    break;
                }
            }
            if (JsonReadFieldValue(payload, payload_length, object_start, "value",
                                   operation_value, sizeof(operation_value), NULL)) {
                operation_code_found = ParseOperationCode(operation_value, enabled);
                if (operation_code_found) {
                    break;
                }
            }
        }
        cursor = value_cursor;   /* 没匹配上就继续从上一次"key"结束的位置往后找下一个 */
    }
    /* 兜底：如果按标准格式没找到，再尝试直接找顶层的"value"字段（兼容更简化的指令格式） */
    if (!operation_code_found &&
        JsonReadFieldValue(payload, payload_length, 0U, "value",
                           operation_value, sizeof(operation_value), NULL)) {
        operation_code_found = ParseOperationCode(operation_value, enabled);
    }
    return operation_code_found;
}

/*
 * 把指令执行结果回复给云平台的service_reply_topic。
 * 用QoS1发送（而不是QoS0），是因为这是"回执"性质的消息，希望尽量确保平台真的收到了，
 * 值得为它多花一点确认开销。
 */
static HAL_StatusTypeDef ReplyToService(const char *message_id, bool success)
{
    char response_payload[PAYLOAD_BUFFER_SIZE]; /* 服务执行结果的JSON文本；只在本函数内有效，随后立即被打包发送。 */
    const char *reply_message_id; /* 指向平台原始messageId；平台用它把“回复”关联回“那一次服务调用”。 */
    int response_length;          /* snprintf生成的字符数，用于防止JSON被缓冲区静默截断。 */
    size_t packet_length;         /* QoS1 PUBLISH二进制报文长度；0代表打包失败，不允许发送。 */

    reply_message_id = message_id != NULL && message_id[0] != '\0' ? message_id : "0";
    response_length = snprintf(response_payload, sizeof(response_payload),
                               success ? "{\"code\":\"000000\",\"message\":\"\",\"messageId\":\"%s\",\"data\":[]}" :
                                         "{\"code\":\"500\",\"message\":\"invalid service command\",\"messageId\":\"%s\",\"data\":[]}",
                               reply_message_id);
    if (response_length < 0 || (size_t)response_length >= sizeof(response_payload)) {
        return HAL_ERROR;
    }
    packet_length = MQTT_BuildPublishQos1(mqtt_packet_buffer, sizeof(mqtt_packet_buffer),
                                          mq2_profile.service_reply_topic, response_payload,
                                          NextPacketId());
    return packet_length == 0U ? HAL_ERROR :
           ESP12F_SendRaw(&esp12f, mqtt_packet_buffer, packet_length);
}

/*
 * 处理一条从数据流里切出来的完整MQTT报文。
 * 只关心PUBLISH类型且topic匹配service_topic的报文，其它类型/其它topic的报文直接忽略。
 * 流程：解析出指令 -> 执行(控制蜂鸣器) -> 如果是QoS1还要回PUBACK确认收到 -> 最后回复执行结果。
 */
static void ProcessMqttPacket(const uint8_t *packet, size_t packet_length)
{
    MQTT_PublishView publish; /* 对原始packet的零拷贝“视图”；里面的topic/payload指针仍指向packet本身，函数返回前不能覆盖packet。 */
    char message_id[MQTT_MESSAGE_ID_SIZE] = {0}; /* 从服务JSON提取出的关联号；{0}保证即使平台没带该字段也以空字符串安全结束。 */
    bool enabled; /* 解析出的控制目标：true写PA8高电平，false写PA8低电平。 */
    bool command_valid; /* JSON是否包含本项目认可的operationCode=0或1；无效命令不会操作蜂鸣器。 */
    size_t ack_length; /* QoS1 PUBACK的固定4字节报文长度。 */
    HAL_StatusTypeDef reply_status; /* 业务回执是否已成功发入ESP-12F发送通道，供调试串口输出结果。 */

    if (!MQTT_ParsePublish(packet, packet_length, &publish) ||
        !MQTT_PublishTopicEquals(&publish, mq2_profile.service_topic)) {
        return;   /* 不是PUBLISH，或者topic不是服务指令Topic，跟本函数无关，直接忽略 */
    }
    DebugServicePayload(publish.payload, publish.payload_length);
    command_valid = ParseBeeperCommand(publish.payload, publish.payload_length,
                                       message_id, sizeof(message_id), &enabled);
    if (command_valid) {
        DebugLog_Write(enabled ? "SERVICE_CMD: operationCode=1\r\n" :
                                 "SERVICE_CMD: operationCode=0\r\n");
        SetBeeper(enabled);
    }
    /* 如果平台是用QoS1发的这条指令，按MQTT协议要求必须回一个PUBACK确认收到，
       否则平台会认为消息投递失败，可能会重发 */
    if (publish.qos == 1U && publish.packet_id != 0U) {
        ack_length = MQTT_BuildPubAck(mqtt_packet_buffer, sizeof(mqtt_packet_buffer),
                                      publish.packet_id);
        if (ack_length != 0U) {
            (void)ESP12F_SendRaw(&esp12f, mqtt_packet_buffer, ack_length);
        }
    }
    /* PUBACK只是"MQTT协议层面确认收到了"，ReplyToService才是"业务层面告诉平台指令执行得怎么样"，
       这是两个不同层面的确认，DMP平台要求业务层也要有明确的成功/失败回执 */
    reply_status = ReplyToService(message_id, command_valid);
    DebugLog_Write(command_valid ? "SERVICE_OK\r\n" : "SERVICE_FAIL: invalid command\r\n");
    DebugLog_Write(reply_status == HAL_OK ? "SERVICE_REPLY_OK\r\n" :
                                             "SERVICE_REPLY_FAIL\r\n");
}

/*
 * 处理所有已收到但还未处理的下行MQTT数据。
 *
 * 【为什么要维护一个"流缓冲区"(mqtt_stream_buffer)而不是直接处理每次读到的数据】
 * ESP模块通过AT+CIPSEND通道转发下行数据时，一次ESP12F_TryReadRaw读到的字节
 * 可能不是恰好一整条MQTT报文——可能是半条（TCP分片），也可能是一条半（粘包）。
 * 所以这里的策略是：先把所有能读到的原始字节都搬进流缓冲区累积起来，
 * 再用 MQTT_GetPacketLength 检查"缓冲区开头是否已经攒够一条完整报文"，
 * 攒够了就处理并从缓冲区里移除（用memmove把剩下的数据往前搬），没攒够就等下一轮再来看。
 */
static void ProcessIncomingMqtt(void)
{
    uint8_t raw_data[MQTT_BUFFER_SIZE]; /* 单次从ESP“+IPD”帧取出的原始TCP负载；可能只是一条MQTT消息的一部分。 */
    size_t raw_length; /* raw_data中本次真正有效的字节数，由ESP12F_TryReadRaw写入。 */
    size_t packet_length; /* 流缓冲区开头一条完整MQTT消息的总长度（固定头+可变头+负载），由MQTT_GetPacketLength计算。 */

    /* 第一步：把ESP模块目前能提供的下行数据全部搬进流缓冲区 */
    while (ESP12F_TryReadRaw(&esp12f, raw_data, sizeof(raw_data), &raw_length) == HAL_OK) {
        if (raw_length > sizeof(mqtt_stream_buffer) - mqtt_stream_length) {
            /* 流缓冲区都要溢出了，说明可能哪里出了异常（数据一直在堆积却没被消费），
               只能选择清空重来，牺牲这一批数据以避免真正的内存越界 */
            mqtt_stream_length = 0U;
            DebugLog_Write("MQTT_RX_FAIL: stream overflow\r\n");
            continue;
        }
        memcpy(&mqtt_stream_buffer[mqtt_stream_length], raw_data, raw_length);
        mqtt_stream_length += raw_length;
    }
    /* 第二步：只要流缓冲区开头已经攒够一条完整报文，就处理它，并把已处理的部分移出缓冲区，
       循环直到剩下的数据不够组成一条完整报文为止 */
    while (MQTT_GetPacketLength(mqtt_stream_buffer, mqtt_stream_length, &packet_length)) {
        ProcessMqttPacket(mqtt_stream_buffer, packet_length);
        mqtt_stream_length -= packet_length;
        if (mqtt_stream_length > 0U) {
            memmove(mqtt_stream_buffer, &mqtt_stream_buffer[packet_length], mqtt_stream_length);
        }
    }
}

/* 订阅服务下发Topic，只有订阅成功后才能收到平台推送的控制指令 */
static HAL_StatusTypeDef SubscribeAsyncService(void)
{
    size_t packet_length; /* SUBSCRIBE二进制报文长度。 */
    size_t response_length; /* 从ESP读出的SUBACK实际长度。 */
    uint16_t packet_id = NextPacketId(); /* 本次订阅请求唯一编号；返回SUBACK必须带相同编号才能证明对应的是这一次订阅。 */

    packet_length = MQTT_BuildSubscribe(mqtt_packet_buffer, sizeof(mqtt_packet_buffer),
                                        mq2_profile.service_topic, packet_id);
    if (packet_length == 0U ||
        ESP12F_SendRaw(&esp12f, mqtt_packet_buffer, packet_length) != HAL_OK ||
        ESP12F_ReadRaw(&esp12f, mqtt_response_buffer, sizeof(mqtt_response_buffer),
                       &response_length, MQTT_SERVICE_TIMEOUT_MS) != HAL_OK ||
        !MQTT_IsSubAckAccepted(mqtt_response_buffer, response_length, packet_id)) {
        DebugLog_Write("MQTT_SUBSCRIBE_FAIL\r\n");
        return HAL_ERROR;
    }
    mqtt_service_subscribed = true;
    DebugLog_Write("MQTT_SERVICE_SUB_OK\r\n");
    return HAL_OK;
}

/*
 * 核心函数：连接(若需要)+订阅服务Topic(若需要)+读传感器+上报烟雾浓度。
 * 结构和01工程的PublishTemperatureHumidity几乎一样，唯一多出来的一步是
 * 在MQTT CONNECT成功之后，还要额外SubscribeAsyncService()订阅服务下发Topic，
 * 只有订阅成功了才算真正的"会话建立完成"，否则也走cleanup重来。
 */
static HAL_StatusTypeDef PublishMq2(void)
{
    uint16_t smoke_concentration;
    MQTT_ConnectOptions options;
    size_t packet_length;
    size_t response_length;
    int payload_length;
    uint8_t connack_code;
    char diagnostic[48];
    HAL_StatusTypeDef status = HAL_ERROR;

    DebugLog_Write("REPORT: start\r\n");
    if (HasPlaceholder(mq2_profile.client_id) ||
        HasPlaceholder(mq2_profile.username) ||
        HasPlaceholder(mq2_profile.password) ||
        HasPlaceholder(mq2_profile.uplink_topic)) {
        DebugLog_Write("CONFIG_FAIL: local credentials not set\r\n");
        return HAL_ERROR;
    }
    if (!mqtt_connected) {
        DebugLog_Write("MQTT_CONNECT: opening session\r\n");
        if (ESP12F_ConnectWifi(&esp12f, WIFI_SSID, WIFI_PASSWORD) != HAL_OK) {
            DebugLog_Write("WIFI_FAIL: check hotspot is 2.4 GHz, SSID/password, ESP power\r\n");
            return HAL_ERROR;
        }
        DebugLog_Write("WIFI_OK\r\n");
        if (ESP12F_OpenTcp(&esp12f, DMP_MQTT_HOST, DMP_MQTT_PORT,
                           ESP12F_TLS_ENABLED != 0U) != HAL_OK) {
            DebugLog_Write("TCP_FAIL: Wi-Fi connected but DMP TCP connection failed\r\n");
            return HAL_ERROR;
        }
        DebugLog_Write("TCP_OK\r\n");
        if (ESP12F_StartTransparentMode(&esp12f) != HAL_OK) {
            goto cleanup;
        }

        options.client_id = mq2_profile.client_id;
        options.username = mq2_profile.username;
        options.password = mq2_profile.password;
        options.keep_alive_seconds = MQTT_KEEP_ALIVE_SECONDS;
        packet_length = MQTT_BuildConnect(mqtt_packet_buffer, sizeof(mqtt_packet_buffer), &options);
        if (packet_length == 0U ||
            ESP12F_SendRaw(&esp12f, mqtt_packet_buffer, packet_length) != HAL_OK ||
            ESP12F_ReadRaw(&esp12f, mqtt_response_buffer, sizeof(mqtt_response_buffer),
                           &response_length, 3000U) != HAL_OK) {
            DebugLog_Write("MQTT_CONNECT_FAIL: no valid CONNACK\r\n");
            goto cleanup;
        }
        connack_code = MQTT_GetConnAckReturnCode(mqtt_response_buffer, response_length);
        if (connack_code != 0U) {
            (void)snprintf(diagnostic, sizeof(diagnostic),
                           "MQTT_CONNECT_FAIL: CONNACK=%u\r\n", (unsigned int)connack_code);
            DebugLog_Write(diagnostic);
            goto cleanup;
        }
        mqtt_connected = true;
        /* 订阅服务下发Topic，失败也算整体会话建立失败（走cleanup），保证不会出现
           "已连上MQTT但收不到远程指令"这种半吊子状态 */
        if (SubscribeAsyncService() != HAL_OK) {
            goto cleanup;
        }
        last_ping_ms = HAL_GetTick();
        DebugLog_Write("MQTT_SESSION_OK\r\n");
    }
    if (!smoke_concentration_valid) {
        if (ReadSmokeConcentration(&current_smoke_concentration) != HAL_OK) {
            DebugLog_Write("MQ2_FAIL: check PA0 analog input\r\n");
            goto cleanup;
        }
        smoke_concentration_valid = true;
        OLEDDisplay_ShowSmoke(current_smoke_concentration);
    }
    smoke_concentration = current_smoke_concentration;
    payload_length = snprintf(property_payload_buffer, sizeof(property_payload_buffer),
                               "{\"messageId\":\"%llu\",\"params\":{" 
                               "\"key\":\"smokeConcentration\",\"value\":%u}}",
                               (unsigned long long)++mqtt_message_sequence,
                               (unsigned int)smoke_concentration);
    if (payload_length < 0 || (size_t)payload_length >= sizeof(property_payload_buffer)) {
        goto cleanup;
    }
    packet_length = MQTT_BuildPublish(mqtt_packet_buffer, sizeof(mqtt_packet_buffer),
                                      mq2_profile.uplink_topic, property_payload_buffer);
    if (packet_length == 0U ||
        ESP12F_SendRaw(&esp12f, mqtt_packet_buffer, packet_length) != HAL_OK) {
        DebugLog_Write("PUBLISH_FAIL: ESP MQTT send failed\r\n");
        goto cleanup;
    }
    DebugLog_Write("REPORT_OK\r\n");
    status = HAL_OK;
    return status;

cleanup:
    /* 失败重置：连接状态和订阅状态都要清掉，下次调用会从头重新连接+订阅 */
    mqtt_connected = false;
    mqtt_service_subscribed = false;
    (void)ESP12F_CloseTcp(&esp12f);
    return status;
}

/* 初始化：比01工程多了个ADC句柄参数（MQ2是模拟量传感器，需要靠ADC采样），
   开机先把蜂鸣器强制关闭，避免上电瞬间GPIO电平不确定导致蜂鸣器误响 */
HAL_StatusTypeDef EnvironmentMonitor_Init(UART_HandleTypeDef *esp_uart, ADC_HandleTypeDef *adc)
{
    mq2_adc = adc;
    last_report_ms = HAL_GetTick() - REPORT_INTERVAL_MS;
    last_ping_ms = HAL_GetTick();
    last_smoke_debug_ms = HAL_GetTick() - SMOKE_DEBUG_INTERVAL_MS;
    mqtt_message_sequence = 0U;
    current_smoke_concentration = 0U;
    mqtt_connected = false;
    mqtt_service_subscribed = false;
    smoke_concentration_valid = false;
    mqtt_packet_id = 0U;
    mqtt_stream_length = 0U;
    SetBeeper(false);
    return ESP12F_Init(&esp12f, esp_uart);
}

/*
 * 主循环"心脏"函数，比01工程多了一步：只要已连接且已订阅服务Topic，
 * 就调用ProcessIncomingMqtt()检查有没有云平台下发的新指令要处理。
 * 这一步必须放在心跳发送之前——道理是"先看看有没有别人找你说话，再决定要不要主动打个招呼"，
 * 不过两者顺序其实没有严格的时序依赖，这里只是约定的调用顺序。
 */
void EnvironmentMonitor_Process(void)
{
    size_t packet_length;

    ReportSmokeDebug();
    if (mqtt_connected && mqtt_service_subscribed) {
        ProcessIncomingMqtt();
    }
    if (mqtt_connected && (HAL_GetTick() - last_ping_ms) >= MQTT_PING_INTERVAL_MS) {
        packet_length = MQTT_BuildPingReq(mqtt_packet_buffer, sizeof(mqtt_packet_buffer));
        if (packet_length == 0U ||
            ESP12F_SendRaw(&esp12f, mqtt_packet_buffer, packet_length) != HAL_OK) {
            DebugLog_Write("PING_FAIL: reconnecting on next report\r\n");
            mqtt_connected = false;
            mqtt_service_subscribed = false;
            (void)ESP12F_CloseTcp(&esp12f);
        } else {
            last_ping_ms = HAL_GetTick();
        }
    }
    if ((HAL_GetTick() - last_report_ms) < REPORT_INTERVAL_MS) {
        return;
    }
    last_report_ms = HAL_GetTick();
    (void)PublishMq2();
}
