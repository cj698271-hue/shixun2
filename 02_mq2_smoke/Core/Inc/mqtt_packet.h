/*
 * mqtt_packet.h —— 手写的MQTT 3.1.1协议编解码器（MQ2工程版本）
 * 比01工程多了SUBSCRIBE/PUBACK的组装，以及"解析收到的PUBLISH报文"的能力，
 * 因为这个工程需要接收云平台下发的控制指令，不能只会"发"，还要会"收"。
 */
#ifndef MQTT_PACKET_H
#define MQTT_PACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 建立MQTT会话时携带的身份信息 */
typedef struct {
    const char *client_id;
    const char *username;
    const char *password;
    uint16_t keep_alive_seconds;
} MQTT_ConnectOptions;

/* 解析出来的一条PUBLISH报文的"视图"：不拷贝数据，只记录topic/payload在原始报文里的
   起始位置和长度，调用方通过这些指针直接访问原始buffer里的内容 */
typedef struct {
    const uint8_t *topic;
    size_t topic_length;
    const uint8_t *payload;
    size_t payload_length;
    uint16_t packet_id;   /* 仅QoS1/2报文才有意义，QoS0时为0 */
    uint8_t qos;
} MQTT_PublishView;

size_t MQTT_BuildConnect(uint8_t *buffer, size_t capacity, const MQTT_ConnectOptions *options);
size_t MQTT_BuildPublish(uint8_t *buffer, size_t capacity, const char *topic, const char *payload);
size_t MQTT_BuildPublishQos1(uint8_t *buffer, size_t capacity, const char *topic,
                             const char *payload, uint16_t packet_id);
/* 组装SUBSCRIBE报文（订阅一个主题） */
size_t MQTT_BuildSubscribe(uint8_t *buffer, size_t capacity, const char *topic, uint16_t packet_id);
/* 组装PUBACK报文（确认收到某个QoS1的PUBLISH） */
size_t MQTT_BuildPubAck(uint8_t *buffer, size_t capacity, uint16_t packet_id);
size_t MQTT_BuildPingReq(uint8_t *buffer, size_t capacity);
size_t MQTT_BuildDisconnect(uint8_t *buffer, size_t capacity);
bool MQTT_IsConnAckAccepted(const uint8_t *packet, size_t length);
uint8_t MQTT_GetConnAckReturnCode(const uint8_t *packet, size_t length);
bool MQTT_IsPubAckAccepted(const uint8_t *packet, size_t length, uint16_t packet_id);
/* 判断收到的SUBACK是否表示"订阅成功"，且packet_id与发出的SUBSCRIBE匹配 */
bool MQTT_IsSubAckAccepted(const uint8_t *packet, size_t length, uint16_t packet_id);
/* 检查buffer开头是否已经攒够一条完整的MQTT报文，够的话把长度写进packet_length */
bool MQTT_GetPacketLength(const uint8_t *packet, size_t available, size_t *packet_length);
/* 把一段完整的PUBLISH报文解析成topic/payload/qos/packet_id这些字段 */
bool MQTT_ParsePublish(const uint8_t *packet, size_t length, MQTT_PublishView *view);
/* 判断解析出的PublishView里的topic是否与给定的字符串完全一致 */
bool MQTT_PublishTopicEquals(const MQTT_PublishView *view, const char *topic);

#endif
