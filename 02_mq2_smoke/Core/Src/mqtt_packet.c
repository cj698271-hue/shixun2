/*
 * mqtt_packet.c —— 手写MQTT报文编解码实现（MQ2工程版本）
 * 编码部分(Build*)的辅助函数和思路与01工程完全一致，见其中的详细注释。
 * 本文件额外实现了"解码"部分：从收到的原始字节流里解析出SUBACK/PUBLISH等报文，
 * 因为这个工程需要理解云平台下发的控制指令，而不只是单向发送数据。
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

/* 写入一段二进制数据 */
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

/* 写入"2字节长度前缀+内容"格式的字符串（MQTT协议规定的字符串编码方式） */
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

/* 计算"剩余长度"字段本身占用几个字节（MQTT可变长度编码，类似varint） */
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
            encoded |= 0x80U;
        }
        if (!AppendByte(cursor, remaining, encoded)) {
            return false;
        }
    } while (value > 0U);
    return true;
}

/* 组装CONNECT报文 */
size_t MQTT_BuildConnect(uint8_t *buffer, size_t capacity, const MQTT_ConnectOptions *options)
{
    size_t client_id_length;
    size_t username_length;
    size_t password_length;
    size_t remaining_length;
    uint8_t *cursor = buffer;
    size_t available = capacity;
    uint8_t flags = 0x02U;   /* Clean Session */

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
        flags |= 0x80U;
    }
    if (options->password != NULL) {
        flags |= 0x40U;
    }
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

/* 组装QoS0的PUBLISH报文（属性上报走这个，不需要packet_id） */
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

/* 组装QoS1的PUBLISH报文（回复服务执行结果走这个，多了一个2字节的packet_id字段） */
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

/* 组装SUBSCRIBE报文：固定头(0x82) + 剩余长度 + packet_id(2字节) + topic字符串 + 请求的QoS(这里固定填1) */
size_t MQTT_BuildSubscribe(uint8_t *buffer, size_t capacity, const char *topic, uint16_t packet_id)
{
    size_t topic_length;
    size_t remaining_length;
    uint8_t *cursor = buffer;
    size_t available = capacity;

    if (buffer == NULL || topic == NULL || packet_id == 0U) {
        return 0U;
    }
    topic_length = strlen(topic);
    if (topic_length == 0U || topic_length > 65535U) {
        return 0U;
    }
    remaining_length = 2U + 2U + topic_length + 1U;
    if (capacity < 1U + RemainingLengthSize(remaining_length) + remaining_length ||
        !AppendByte(&cursor, &available, 0x82U) ||
        !AppendRemainingLength(&cursor, &available, remaining_length) ||
        !AppendByte(&cursor, &available, (uint8_t)(packet_id >> 8U)) ||
        !AppendByte(&cursor, &available, (uint8_t)packet_id) ||
        !AppendString(&cursor, &available, topic) ||
        !AppendByte(&cursor, &available, 0x01U)) {
        return 0U;
    }
    return (size_t)(cursor - buffer);
}

/* PUBACK报文固定4字节：固定头(0x40 0x02) + packet_id(2字节)，不需要用可变长度编码 */
size_t MQTT_BuildPubAck(uint8_t *buffer, size_t capacity, uint16_t packet_id)
{
    if (buffer == NULL || capacity < 4U || packet_id == 0U) {
        return 0U;
    }
    buffer[0] = 0x40U;
    buffer[1] = 0x02U;
    buffer[2] = (uint8_t)(packet_id >> 8U);
    buffer[3] = (uint8_t)packet_id;
    return 4U;
}

/* PINGREQ心跳报文固定2字节：0xC0 0x00 */
size_t MQTT_BuildPingReq(uint8_t *buffer, size_t capacity)
{
    if (buffer == NULL || capacity < 2U) {
        return 0U;
    }
    buffer[0] = 0xC0U;
    buffer[1] = 0x00U;
    return 2U;
}

/* DISCONNECT报文固定2字节：0xE0 0x00 */
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

/* 在收到的数据里扫描查找CONNACK特征码(0x20 0x02 0x00)，找到就返回其后的returnCode */
uint8_t MQTT_GetConnAckReturnCode(const uint8_t *packet, size_t length)
{
    size_t index;

    if (packet == NULL || length < 4U) {
        return 255U;
    }
    for (index = 0U; index + 3U < length; ++index) {
        if (packet[index] == 0x20U && packet[index + 1U] == 0x02U &&
            packet[index + 2U] == 0x00U) {
            return packet[index + 3U];
        }
    }
    return 255U;
}

/* 在收到的数据里查找是否有针对指定packet_id的PUBACK确认(0x40 0x02 <packet_id高字节><packet_id低字节>) */
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

/*
 * 检查是否收到了表示"订阅成功"的SUBACK报文。
 * SUBACK固定头是0x90，第2字节是剩余长度(至少3，因为至少含packet_id 2字节+1个返回码)，
 * 紧跟着2字节packet_id，再紧跟着"每个被订阅主题各一个"的返回码——
 * 返回码0x80表示"该主题订阅失败"，其它值(0x00/0x01/0x02)表示订阅成功并授予的QoS等级。
 * 这里只订阅了一个主题，所以只需要检查紧跟在packet_id后面的那一个返回码是否不等于0x80。
 */
bool MQTT_IsSubAckAccepted(const uint8_t *packet, size_t length, uint16_t packet_id)
{
    size_t index;
    uint8_t packet_id_msb = (uint8_t)(packet_id >> 8U);
    uint8_t packet_id_lsb = (uint8_t)packet_id;

    if (packet == NULL || packet_id == 0U || length < 5U) {
        return false;
    }
    for (index = 0U; index + 4U < length; ++index) {
        if (packet[index] == 0x90U && packet[index + 1U] >= 0x03U &&
            packet[index + 2U] == packet_id_msb && packet[index + 3U] == packet_id_lsb &&
            packet[index + 4U] != 0x80U) {
            return true;
        }
    }
    return false;
}

/*
 * 检查buffer开头是否已经攒够了"1条完整MQTT报文"所需的全部字节数，是的话把这个长度写进packet_length。
 * 这个函数不关心报文的具体类型，只解析所有MQTT报文都通用的头部结构：
 *   第1字节是固定头（报文类型+标志位），后面跟1~4字节的"剩余长度"（可变长度编码），
 *   再后面是"剩余长度"字节数的报文主体。
 * 用于ProcessIncomingMqtt里处理TCP粘包/半包场景：只有在这个函数返回true时，
 * 才说明流缓冲区里已经有一条完整报文可以安全地取出来处理了。
 */
bool MQTT_GetPacketLength(const uint8_t *packet, size_t available, size_t *packet_length)
{
    size_t multiplier = 1U;
    size_t remaining_length = 0U;
    size_t index;
    uint8_t encoded;

    if (packet == NULL || packet_length == NULL || available < 2U) {
        return false;
    }
    /* 从第2字节开始，逐字节解析"剩余长度"的可变长度编码，直到遇到最高位为0的字节（表示编码结束） */
    for (index = 1U; index < available && index <= 4U; ++index) {
        encoded = packet[index];
        remaining_length += (size_t)(encoded & 0x7FU) * multiplier;
        if ((encoded & 0x80U) == 0U) {
            *packet_length = index + 1U + remaining_length;
            /* 只有当目前已收到的字节数(available)真的够这条报文的完整长度时才算数 */
            return *packet_length <= available;
        }
        multiplier *= 128U;
    }
    return false;
}

/*
 * 把一段完整的PUBLISH报文解析拆解成topic/payload/qos/packet_id这几个字段，填入view里。
 * 这里的view->topic和view->payload都是"指向packet内部的指针"，不做任何数据拷贝，
 * 所以调用方必须保证packet这块内存在使用view期间一直有效。
 */
bool MQTT_ParsePublish(const uint8_t *packet, size_t length, MQTT_PublishView *view)
{
    size_t packet_length;
    size_t index = 1U;
    size_t multiplier = 1U;
    size_t remaining_length = 0U;
    size_t topic_length;
    uint8_t qos;

    /* 先确认这确实是一条完整报文(长度吻合)，并且固定头的高4位是0x30(PUBLISH的报文类型标识) */
    if (packet == NULL || view == NULL || !MQTT_GetPacketLength(packet, length, &packet_length) ||
        packet_length != length || (packet[0] & 0xF0U) != 0x30U) {
        return false;
    }
    /* 重新解析一遍"剩余长度"字段，把index移动到"剩余长度"字段结束后的位置（即可变头开始的位置） */
    while ((packet[index] & 0x80U) != 0U) {
        remaining_length += (size_t)(packet[index] & 0x7FU) * multiplier;
        multiplier *= 128U;
        ++index;
    }
    remaining_length += (size_t)packet[index] * multiplier;
    ++index;
    if (remaining_length < 2U || index + 2U > length) {
        return false;
    }
    /* PUBLISH报文的可变头第一部分是topic：先2字节长度，再是topic本身 */
    topic_length = ((size_t)packet[index] << 8U) | (size_t)packet[index + 1U];
    index += 2U;
    if (topic_length == 0U || index + topic_length > length) {
        return false;
    }
    /* QoS等级藏在固定头第1字节的bit1~bit2里 */
    qos = (uint8_t)((packet[0] >> 1U) & 0x03U);
    view->topic = &packet[index];
    view->topic_length = topic_length;
    index += topic_length;
    view->packet_id = 0U;
    view->qos = qos;
    /* 只有QoS1/2才带packet_id字段（紧跟在topic后面2字节）；QoS0没有这个字段，直接是payload */
    if (qos > 0U) {
        if (qos == 3U || index + 2U > length) {
            return false;   /* qos==3是协议里的非法值，直接判定报文无效 */
        }
        view->packet_id = ((uint16_t)packet[index] << 8U) | packet[index + 1U];
        index += 2U;
    }
    /* 剩下的字节全部是payload（PUBLISH报文没有单独的payload长度字段，靠"总长度减去已消耗的部分"得出） */
    view->payload = &packet[index];
    view->payload_length = length - index;
    return true;
}

/* 比较解析出的PublishView的topic是否与给定字符串完全一致（长度和内容都要匹配） */
bool MQTT_PublishTopicEquals(const MQTT_PublishView *view, const char *topic)
{
    size_t topic_length;

    if (view == NULL || topic == NULL) {
        return false;
    }
    topic_length = strlen(topic);
    return view->topic_length == topic_length && memcmp(view->topic, topic, topic_length) == 0;
}
