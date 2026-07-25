# DHT11 Temperature and Humidity Monitor

This is an independent STM32F103C8T6 project for the DHT11 device only.

- DHT11 DATA: `PA0`
- ESP-12F UART2: `PA2` is STM32 TX, `PA3` is STM32 RX
- ULink topic: `$sys/{productKey}/{deviceKey}/property/pub`
- Payloads: temperature and humidity are sent as two individual property reports, matching the working reference project.
- Debug UART: `PA9 / USART1_TX`, `115200 8N1`
- Keil project: `MDK-ARM/temperature_humidity_monitor.uvprojx`
- Output: `build/temperature_humidity_monitor.hex`

The product/device identity, client ID, username, Topic, and payload are derived from the platform SDK. Fill only `Core/Inc/local_credentials.h` before building: the hotspot SSID, hotspot password, and this device's local HMAC-SHA256 MQTT password. Do not copy `deviceSecret` into source code.

Example property payload:

```json
{"messageId":"1784649600000","params":{"key":"temperature","value":25.3,"ts":1784649600000}}
```

From the project root, double-click `run_configure_dmp_credentials.cmd` to fill this file automatically from the downloaded DMP SDK.

Build with Keil or run:

```powershell
& 'C:\TDM-GCC-64\bin\mingw32-make.exe'
```
