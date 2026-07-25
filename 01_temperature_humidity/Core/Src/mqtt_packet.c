/*
 * mqtt_packet.c —— 手写MQTT报文编解码实现
 *
 * 下面几个 Append* 辅助函数都遵循同一种写法：
 *   传入 cursor（当前写入位置的指针的指针）和 remaining（缓冲区剩余可写字节数），
 *   写入成功就把cursor向前移动、remaining减少对应字节数，返回true；
 *   如果剩余空间不够就什么都不写，直接返回false。
 * 这样上层可以用 "&&" 把多个Append操作串起来，只要中间有一个返回false，
 * 后面的操作会因为短路求值直接跳过，最终统一判断"整体是否成功"。
 */
#include "mqtt_packet.h"
#include <string.h>

/* 写入1个字节 */
static bool AppendByte(uint8_t **cursor, size_t *remaining, uint8_t value)
{
    if (*remaining < 1U) {
        return false;
    }
    **cursor = value;
    ++(*cursor);
    --(*remaining);
    return true;
}

/* 写入一段任意二进制数据 */
static bool AppendData(uint8_t **cursor, size_t *remaining, const uint8_t *data, size_t length)
{
    if (*remaining < length) {
        return false;
    }
    memcpy(*cursor, data, length);
    *cursor += length;
    *remaining -= length;
    return true;
}

/* 写入MQTT协议规定的"带长度前缀的字符串"格式：先写2字节长度（大端序），再写字符串内容本身 */
static bool AppendString(uint8_t **cursor, size_t *remaining, const char *text)
{
    size_t length = strlen(text);

    if (length > 65535U || *remaining < length + 2U) {
        return false;
    }
    return AppendByte(cursor, remaining, (uint8_t)(length >> 8U)) &&
           AppendByte(cursor, remaining, (uint8_t)length) &&
           AppendData(cursor, remaining, (const uint8_t *)text, length);
}

/*
 * 计算"剩余长度"字段本身要占用几个字节。
 * MQTT用"可变长度编码"表示剩余长度：每字节7位有效数据+1位"是否还有下一字节"的标记，
 * 类似protobuf的varint，数值越大占用字节越多（最多4字节，可表示到约256MB）。
 */
static size_t RemainingLengthSize(size_t value)
{
    size_t bytes = 1U;
    while (value > 127U) {
        value /= 128U;
        ++bytes;
    }
    return bytes;
}

/* 按MQTT可变长度编码规则写入"剩余长度"字段 */
static bool AppendRemainingLength(uint8_t **cursor, size_t *remaining, size_t value)
{
    do {
        uint8_t encoded = (uint8_t)(value % 128U);
        value /= 128U;
        if (value > 0U) {
            encoded |= 0x80U;   /* 最高位置1表示"后面还有字节"，接收方据此判断编码是否结束 */
        }
        if (!AppendByte(cursor, remaining, encoded)) {
            return false;
        }
    } while (value > 0U);
    return true;
}

/*
 * 组装CONNECT报文：固定头(0x10) + 剩余长度 + 协议名"MQTT" + 协议级别(4=v3.1.1)
 * + 连接标志位(flags) + keep-alive秒数 + client_id[+username][+password]
 */
size_t MQTT_BuildConnect(uint8_t *buffer, size_t capacity, const MQTT_ConnectOptions *options)
{
    size_t client_id_length;
    size_t username_length;
    size_t password_length;
    size_t remaining_length;
    uint8_t *cursor = buffer;
    size_t available = capacity;
    uint8_t flags = 0x02U;  /* bit1=Clean Session：每次都建立全新会话，不保留上次的订阅/未完成消息 */

    if (buffer == NULL || options == NULL || options->client_id == NULL) {
        return 0U;
    }
    client_id_length = strlen(options->client_id);
    username_length = options->username == NULL ? 0U : strlen(options->username);
    password_length = options->password == NULL ? 0U : strlen(options->password);
    if (client_id_length > 65535U || username_length > 65535U || password_length > 65535U) {
        return 0U;
    }
    if (options->username != NULL) {
        flags |= 0x80U;  /* bit7=Username Flag：报文里带了用户名字段 */
    }
    if (options->password != NULL) {
        flags |= 0x40U;  /* bit6=Password Flag：报文里带了密码字段 */
    }
    /* 剩余长度=可变头(10字节固定) + client_id字段(2字节长度+内容) [+username字段] [+password字段] */
    remaining_length = 10U + 2U + client_id_length;
    if (options->username != NULL) {
        remaining_length += 2U + username_length;
    }
    if (options->password != NULL) {
        remaining_length += 2U + password_length;
    }
    if (capacity < 1U + RemainingLengthSize(remaining_length) + remaining_length ||
        !AppendByte(&cursor, &available, 0x10U) ||
        !AppendRemainingLength(&cursor, &available, remaining_length) ||
        !AppendString(&cursor, &available, "MQTT") ||
        !AppendByte(&cursor, &available, 0x04U) ||
        !AppendByte(&cursor, &available, flags) ||
        !AppendByte(&cursor, &available, (uint8_t)(options->keep_alive_seconds >> 8U)) ||
        !AppendByte(&cursor, &available, (uint8_t)options->keep_alive_seconds) ||
        !AppendString(&cursor, &available, options->client_id)) {
        return 0U;
    }
    if ((options->username != NULL && !AppendString(&cursor, &available, options->username)) ||
        (options->password != NULL && !AppendString(&cursor, &available, options->password))) {
        return 0U;
    }
    return (size_t)(cursor - buffer);
}

/* 组装QoS0的PUBLISH报文：固定头(0x30) + 剩余长度 + topic字符串 + payload原始数据（不带packet_id） */
size_t MQTT_BuildPublish(uint8_t *buffer, size_t capacity, const char *topic, const char *payload)
{
    size_t topic_length;
    size_t payload_length;
    size_t remaining_length;
    uint8_t *cursor = buffer;
    size_t available = capacity;

    if (buffer == NULL || topic == NULL || payload == NULL) {
        return 0U;
    }
    topic_length = strlen(topic);
    payload_length = strlen(payload);
    if (topic_length == 0U || topic_length > 65535U) {
        return 0U;
    }
    remaining_length = 2U + topic_length + payload_length;
    if (capacity < 1U + RemainingLengthSize(remaining_length) + remaining_length ||
        !AppendByte(&cursor, &available, 0x30U) ||
        !AppendRemainingLength(&cursor, &available, remaining_length) ||
        !AppendString(&cursor, &available, topic) ||
        !AppendData(&cursor, &available, (const uint8_t *)payload, payload_length)) {
        return 0U;
    }
    return (size_t)(cursor - buffer);
}

/* 组装QoS1的PUBLISH报文：固定头(0x32，bit1标记QoS=1) + 剩余长度 + topic + packet_id(2字节) + payload。
   本工程目前实际未调用此函数（属性上报用的是QoS0），保留是为了配合服务下发指令场景可能需要的确认机制。 */
size_t MQTT_BuildPublishQos1(uint8_t *buffer, size_t capacity, const char *topic,
                             const char *payload, uint16_t packet_id)
{
    size_t topic_length;
    size_t payload_length;
    size_t remaining_length;
    uint8_t *cursor = buffer;
    size_t available = capacity;

    if (buffer == NULL || topic == NULL || payload == NULL || packet_id == 0U) {
        return 0U;
    }
    topic_length = strlen(topic);
    payload_length = strlen(payload);
    if (topic_length == 0U || topic_length > 65535U) {
        return 0U;
    }
    remaining_length = 2U + topic_length + 2U + payload_length;
    if (capacity < 1U + RemainingLengthSize(remaining_length) + remaining_length ||
        !AppendByte(&cursor, &available, 0x32U) ||
        !AppendRemainingLength(&cursor, &available, remaining_length) ||
        !AppendString(&cursor, &available, topic) ||
        !AppendByte(&cursor, &available, (uint8_t)(packet_id >> 8U)) ||
        !AppendByte(&cursor, &available, (uint8_t)packet_id) ||
        !AppendData(&cursor, &available, (const uint8_t *)payload, payload_length)) {
        return 0U;
    }
    return (size_t)(cursor - buffer);
}

/* PINGREQ心跳报文固定就是2个字节：0xC0 0x00，没有可变头和payload，不需要动态计算长度 */
size_t MQTT_BuildPingReq(uint8_t *buffer, size_t capacity)
{
    if (buffer == NULL || capacity < 2U) {
        return 0U;
    }
    buffer[0] = 0xC0U;
    buffer[1] = 0x00U;
    return 2U;
}

/* DISCONNECT报文同样固定2字节：0xE0 0x00 */
size_t MQTT_BuildDisconnect(uint8_t *buffer, size_t capacity)
{
    if (buffer == NULL || capacity < 2U) {
        return 0U;
    }
    buffer[0] = 0xE0U;
    buffer[1] = 0x00U;
    return 2U;
}

bool MQTT_IsConnAckAccepted(const uint8_t *packet, size_t length)
{
    return MQTT_GetConnAckReturnCode(packet, length) == 0U;
}

/*
 * 从收到的数据流里查找并解析CONNACK报文（固定头0x20 0x02 0x00 <returnCode>）。
 * 之所以要在整段buffer里"扫描查找"而不是直接假设开头就是CONNACK，
 * 是因为ESP模块的AT指令回复(比如"SEND OK")可能和真正的MQTT报文混在同一段接收数据里，
 * 扫描能容忍这种夹杂情况，只要CONNACK的4字节特征码出现在数据里就能找到。
 */
uint8_t MQTT_GetConnAckReturnCode(const uint8_t *packet, size_t length)
{
    size_t index;

    if (packet == NULL || length < 4U) {
        return 255U;
    }
    for (index = 0U; index + 3U < length; ++index) {
        if (packet[index] == 0x20U && packet[index + 1U] == 0x02U &&
            packet[index + 2U] == 0x00U) {
            return packet[index + 3U];   /* 找到了，第4个字节就是returnCode（0=接受） */
        }
    }
    return 255U;   /* 没找到CONNACK特征码，返回一个协议里不存在的值代表"未知/失败" */
}

/* 在收到的数据里查找是否存在针对指定packet_id的PUBACK确认（QoS1流程用，本工程暂未使用） */
bool MQTT_IsPubAckAccepted(const uint8_t *packet, size_t length, uint16_t packet_id)
{
    size_t index;
    uint8_t packet_id_msb = (uint8_t)(packet_id >> 8U);
    uint8_t packet_id_lsb = (uint8_t)packet_id;

    if (packet == NULL || packet_id == 0U || length < 4U) {
        return false;
    }
    for (index = 0U; index + 3U < length; ++index) {
        if (packet[index] == 0x40U && packet[index + 1U] == 0x02U &&
            packet[index + 2U] == packet_id_msb && packet[index + 3U] == packet_id_lsb) {
            return true;
        }
    }
    return false;
}
