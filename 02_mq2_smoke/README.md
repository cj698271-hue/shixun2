# MQ-2 Smoke Monitor

This is an independent STM32F103C8T6 project for the MQ-2 device only.

- MQ-2 `AO`: `PA0 / ADC1_IN0`
- ESP-12F UART2: `PA2` is STM32 TX, `PA3` is STM32 RX
- Debug UART: `PA9 / USART1_TX`, `115200 8N1`
- ULink topic: `$sys/{productKey}/{deviceKey}/property/pub`
- Payload: `{"messageId":"...","params":{"key":"smokeConcentration","value":161,"ts":...}}`
- Keil project: `MDK-ARM/mq2_smoke_monitor.uvprojx`
- Output: `build/mq2_smoke_monitor.hex`

The product/device identity, client ID, username, Topic, and payload are derived from the platform SDK. Fill only `Core/Inc/local_credentials.h` before building: the hotspot SSID, hotspot password, and this device's local HMAC-SHA256 MQTT password. Do not copy `deviceSecret` into source code.

From the project root, double-click `run_configure_dmp_credentials.cmd` to fill this file automatically from the downloaded DMP SDK.

The MQ-2 module has no factory concentration calibration in this hardware configuration. Its 12-bit `PA0` ADC value is mapped to the DMP model's permitted `smokeConcentration` range of 0-500. Do not interpret this number as calibrated ppm. Do not connect a 5 V MQ-2 `AO` signal directly to `PA0`; use a divider or ensure its maximum voltage is 3.3 V.

Build with Keil or run:

```powershell
& 'C:\TDM-GCC-64\bin\mingw32-make.exe'
```
