# MQ-2 工程与格物平台对应关系

本工程只对应 MQ-2 烟雾设备，不包含 DHT11。

| 项目 | 固件设置 |
| --- | --- |
| MCU | STM32F103C8T6 |
| 传感器 | MQ-2 AO -> `PA0 / ADC1_IN0` |
| ADC | 12 位、239.5 周期采样、8 次平均 |
| ESP 串口 | USART2：`PA2 TX -> ESP RX`，`PA3 RX <- ESP TX` |
| 调试串口 | USART1：`PA9 TX -> USB-TTL RX`，115200 8N1 |
| Broker | `dmp-mqtt.cuiot.cn:1883` |
| Topic | `$sys/{productKey}/{deviceKey}/property/pub` |
| 属性标识符 | `smokeConcentration`，整数 0 至 500 |
| MQTT | 3.1.1、Clean Session、QoS 1 |

属性消息格式如下：

```json
{"messageId":"...","params":{"key":"smokeConcentration","value":161,"ts":...}}
```

MQ-2 未经过标准气体标定，因此固件把 0 至 4095 的 ADC 值线性映射到平台允许的 0 至 500；这个值不能解释为精确 ppm。若模块以 5 V 供电，AO 必须分压到不超过 3.3 V。

平台产品应已发布。设备在首次成功 MQTT 登录前显示“未激活”属于正常现象。烧录后查看 USART1：依次出现 `ESP_INIT_OK`、`WIFI_OK`、`TCP_OK`、`MQTT_CONNECT_OK`、`REPORT_OK` 即表示完整链路成功。
