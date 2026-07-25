/*
 * mqtt_packet.h —— 手写的最小化MQTT 3.1.1协议编解码器
 * 这个项目没有使用现成的MQTT库，而是自己按协议规范手动拼二进制报文，
 * 只实现了本项目实际用到的报文类型：CONNECT/PUBLISH/PINGREQ/DISCONNECT。
 */
#ifndef MQTT_PACKET_H
#define MQTT_PACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 建立MQTT会话时需要携带的身份信息 */
typedef struct {
    const char *client_id;      /* 客户端ID，服务器用它识别是哪个设备 */
    const char *username;       /* 用户名（可为NULL，表示不携带） */
    const char *password;       /* 密码（可为NULL） */
    uint16_t keep_alive_seconds;/* 心跳保活周期，单位秒 */
} MQTT_ConnectOptions;

/* 组装CONNECT报文（建立会话的第一条报文） */
size_t MQTT_BuildConnect(uint8_t *buffer, size_t capacity, const MQTT_ConnectOptions *options);
/* 组装PUBLISH报文，QoS0（发送即完成，不需要服务器确认） */
size_t MQTT_BuildPublish(uint8_t *buffer, size_t capacity, const char *topic, const char *payload);
/* 组装PUBLISH报文，QoS1（需要服务器回PUBACK确认，报文里要带packet_id） */
size_t MQTT_BuildPublishQos1(uint8_t *buffer, size_t capacity, const char *topic,
                             const char *payload, uint16_t packet_id);
/* 组装PINGREQ心跳报文 */
size_t MQTT_BuildPingReq(uint8_t *buffer, size_t capacity);
/* 组装DISCONNECT报文 */
size_t MQTT_BuildDisconnect(uint8_t *buffer, size_t capacity);
/* 判断收到的CONNACK报文是否表示"连接被接受" */
bool MQTT_IsConnAckAccepted(const uint8_t *packet, size_t length);
/* 从CONNACK报文里取出服务器返回的状态码（0=接受，非0=各种拒绝原因） */
uint8_t MQTT_GetConnAckReturnCode(const uint8_t *packet, size_t length);
/* 判断收到的PUBACK报文是否是对指定packet_id的确认 */
bool MQTT_IsPubAckAccepted(const uint8_t *packet, size_t length, uint16_t packet_id);

#endif
