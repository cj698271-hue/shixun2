/*
 * app_config.h —— 温湿度设备的“身份证” + 运行参数配置表
 *
 * 这个文件里全是宏定义（#define），没有一行可执行代码。
 * 它的作用是把“这台设备是谁”“要连哪个云平台”“多久上报一次数据”这些
 * 会变化的参数集中放在一起，方便修改，而不用去业务代码里到处找魔法数字。
 *
 * 敏感信息（Wi-Fi密码、平台密码）不会出现在这个文件里，而是被
 * include 进来的 local_credentials.h（该文件已被 .gitignore 排除，不会上传/分享）。
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "local_credentials.h"

/* 格物 DMP 云平台的 MQTT 服务器地址和端口（所有设备通用，1883 是标准 MQTT 明文端口） */
#define DMP_MQTT_HOST             "dmp-mqtt.cuiot.cn"
#define DMP_MQTT_PORT             1883U

/*
 * 下面这些值来自 DMP 平台后台为本设备生成的 SDK 配置，是固定不变的。
 * 真正的登录密码（HMAC签名结果）保存在 local_credentials.h 里，
 * 那个文件要保持私有，绝不要提交到 git 仓库。
 */
#define DMP_TEMPHUM_MQTT_PASSWORD     GEWU_PASSWORD
#define DMP_TEMPHUM_PRODUCT_KEY       "cug6z418jc2ve3n7"   /* 产品密钥：标识“温湿度传感器”这一类产品 */
#define DMP_TEMPHUM_DEVICE_KEY        "l0yREtvVWrihLXG"    /* 设备密钥：标识这一台具体设备 */
/* DMP 平台用“设备名称”而不是 deviceKey 来校验 MQTT 的 Client ID，这里做特殊处理。 */
#define DMP_TEMPHUM_DEVICE_NAME       "test24207117"
/* MQTT Client ID 的固定格式：设备名|产品密钥|0|0|0（后三个0是平台协议规定的占位符） */
#define DMP_TEMPHUM_MQTT_CLIENT_ID    DMP_TEMPHUM_DEVICE_NAME "|" DMP_TEMPHUM_PRODUCT_KEY "|0|0|0"
/* MQTT 用户名格式：设备密钥|产品密钥 */
#define DMP_TEMPHUM_MQTT_USERNAME     DMP_TEMPHUM_DEVICE_KEY "|" DMP_TEMPHUM_PRODUCT_KEY
/* 属性上报主题（Topic）：设备把温湿度数据发布到这个地址，云平台订阅它来接收数据 */
#define DMP_TEMPHUM_UPLINK_TOPIC      "$sys/" DMP_TEMPHUM_PRODUCT_KEY "/" DMP_TEMPHUM_DEVICE_KEY "/property/pub"

/* 两个工程共用同一份代码框架时，用这两个宏区分“我是温湿度设备”还是“我是烟雾设备” */
#define ENVIRONMENT_TARGET_TEMPHUM     1U
#define ENVIRONMENT_TARGET_MQ2         2U

/* 由编译时的目标工程决定，如果没有外部指定，默认按“温湿度设备”编译 */
#ifndef ENVIRONMENT_TARGET
#define ENVIRONMENT_TARGET             ENVIRONMENT_TARGET_TEMPHUM
#endif

#define ESP12F_TLS_ENABLED        0U      /* 是否启用TLS加密连接：0=不启用（明文TCP），本项目走明文 */
#define REPORT_INTERVAL_MS        1000U   /* 每隔多少毫秒上报一次数据：1000ms = 1秒 */
#define REPORT_TIMEZONE_OFFSET_HOURS 8     /* 时区偏移（东八区），用于本地时间显示/计算，不影响上报的UTC时间戳 */
#define MQ2_ALARM_THRESHOLD       1800U   /* MQ2烟雾浓度报警阈值（本工程未使用，保留自公共配置模板） */
#define MQTT_KEEP_ALIVE_SECONDS   60U     /* MQTT心跳保活周期：60秒内没有任何数据交互就要发一次心跳，防止被服务器断开 */

#endif
