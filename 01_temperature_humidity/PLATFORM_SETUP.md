# DHT11 工程与格物平台对应关系

本工程只对应温湿度设备，不包含 MQ-2。

| 项目 | 固件设置 |
| --- | --- |
| MCU | STM32F103C8T6 |
| 传感器 | DHT11 DATA -> `PA0`，输入上拉并在读数时动态切换方向 |
| ESP 串口 | USART2：`PA2 TX -> ESP RX`，`PA3 RX <- ESP TX` |
| 调试串口 | USART1：`PA9 TX -> USB-TTL RX`，115200 8N1 |
| Broker | `dmp-mqtt.cuiot.cn:1883` |
| Topic | `$sys/{productKey}/{deviceKey}/property/pub` |
| 属性标识符 | `temperature`、`humidity` |
| MQTT | 3.1.1、Clean Session、QoS 1 |

温度和湿度分成两条属性消息发送，格式如下：

```json
{"messageId":"...","params":{"key":"temperature","value":25.3,"ts":...}}
```

```json
{"messageId":"...","params":{"key":"humidity","value":56.0,"ts":...}}
```

平台产品应已发布。设备在首次成功 MQTT 登录前显示“未激活”属于正常现象。烧录后查看 USART1：依次出现 `ESP_INIT_OK`、`WIFI_OK`、`TCP_OK`、`MQTT_CONNECT_OK`、`REPORT_OK` 即表示完整链路成功。
