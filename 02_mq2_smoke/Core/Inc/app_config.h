/*
 * app_config.h —— MQ2烟雾报警设备的"身份证" + 运行参数配置表
 * 结构和01工程的app_config.h完全类似，区别只在于这里配置的是"MQ2烟雾设备"这个身份，
 * 并且多了"异步服务"相关的Topic（用于接收云平台下发的指令，比如远程控制蜂鸣器）。
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "local_credentials.h"

#define DMP_MQTT_HOST             "dmp-mqtt.cuiot.cn"
#define DMP_MQTT_PORT             1883U

/*
 * Values below are fixed by the device SDK downloaded from the DMP portal.
 * DMP_MQ2_MQTT_PASSWORD stays in local_credentials.h, which must never
 * contain the deviceSecret itself.
 */
/* 下面这些值来自DMP平台后台为本设备生成的SDK配置。真正的密码放在local_credentials.h里私有保存。 */
#define DMP_MQ2_PRODUCT_KEY           "cu1e1vp51svlk8zn"    /* 产品密钥：标识"MQ2烟雾报警器"这一类产品 */
#define DMP_MQ2_DEVICE_KEY            "u8UjAHR0JtyALgG"     /* 设备密钥：标识这一台具体设备 */
/* DMP authenticates Client ID with the platform device name, not deviceKey. */
#define DMP_MQ2_DEVICE_NAME           "24207117_MQ2"
#define DMP_MQ2_MQTT_CLIENT_ID        DMP_MQ2_DEVICE_KEY "|" DMP_MQ2_PRODUCT_KEY "|0|0|0"
#define DMP_MQ2_MQTT_USERNAME         DMP_MQ2_DEVICE_KEY "|" DMP_MQ2_PRODUCT_KEY
#define DMP_MQ2_UPLINK_TOPIC          "$sys/" DMP_MQ2_PRODUCT_KEY "/" DMP_MQ2_DEVICE_KEY "/property/pub"
/* 异步服务下发主题：云平台通过这个Topic给设备发指令（比如"打开蜂鸣器"） */
#define DMP_MQ2_ASYNC_SERVICE_TOPIC   "$sys/" DMP_MQ2_PRODUCT_KEY "/" DMP_MQ2_DEVICE_KEY "/service/pub"
/* 异步服务回复主题：设备执行完指令后，通过这个Topic把执行结果回复给云平台 */
#define DMP_MQ2_ASYNC_SERVICE_REPLY_TOPIC \
                                      "$sys/" DMP_MQ2_PRODUCT_KEY "/" DMP_MQ2_DEVICE_KEY "/service/pub_reply"
#define DMP_MQ2_SERVICE_KEY           "Operation"          /* 服务指令JSON里，指令类型字段的键名 */
#define DMP_MQ2_SERVICE_PARAMETER_KEY "operationCode"       /* 服务指令JSON里，具体参数字段的键名 */

/* The working reference project supplies the canonical platform DeviceName. */
/* 如果外部（比如烧录时的编译宏）指定了真正在平台注册的设备名，就用那个；否则退回默认名称 */
#ifdef GEWU_DEVICE_NAME
#define DMP_MQ2_PLATFORM_DEVICE_NAME  GEWU_DEVICE_NAME
#else
#define DMP_MQ2_PLATFORM_DEVICE_NAME  DMP_MQ2_DEVICE_NAME
#endif

/* 用平台实际设备名重新定义Client ID，覆盖上面用DEVICE_KEY拼的那个版本 */
#undef DMP_MQ2_MQTT_CLIENT_ID
#define DMP_MQ2_MQTT_CLIENT_ID        DMP_MQ2_PLATFORM_DEVICE_NAME "|" DMP_MQ2_PRODUCT_KEY "|0|0|0"

/* A platform-issued direct password takes precedence over the SDK HMAC. */
/* 如果平台后台直接给了一个密码（GEWU_PASSWORD），优先用它；否则用SDK算出来的HMAC密码 */
#ifdef GEWU_PASSWORD
#define DMP_MQ2_PLATFORM_PASSWORD     GEWU_PASSWORD
#else
#define DMP_MQ2_PLATFORM_PASSWORD     DMP_MQ2_MQTT_PASSWORD
#endif

#define ENVIRONMENT_TARGET_TEMPHUM     1U
#define ENVIRONMENT_TARGET_MQ2         2U

/* Selected by the Makefile target. Keil may define this symbol per target. */
/* 由编译目标决定，若未指定，此处默认给的是TEMPHUM——但本工程实际编译时会通过工程设置传入MQ2 */
#ifndef ENVIRONMENT_TARGET
#define ENVIRONMENT_TARGET             ENVIRONMENT_TARGET_TEMPHUM
#endif

#define ESP12F_TLS_ENABLED        0U      /* 不启用TLS，走明文TCP */
#define REPORT_INTERVAL_MS        1000U   /* 每1秒上报一次烟雾浓度 */
#define REPORT_TIMEZONE_OFFSET_HOURS 8
#define MQ2_ADC_FULL_SCALE         4095U  /* 12位ADC的满量程读数（2^12 - 1），用于把原始ADC值换算成比例 */
#define MQ2_PLATFORM_MAX_CONCENTRATION 500U /* 上报给云平台的"浓度"值的换算上限（单位ppm，仅为线性映射基准，非精确校准值） */
#define MQTT_KEEP_ALIVE_SECONDS   60U

#endif
