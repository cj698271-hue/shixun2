# `main.c` 接入片段

在 CubeMX 生成的 `Core/Src/main.c` 中包含应用头文件：

```c
#include "app_environment.h"
```

在 `MX_USART2_UART_Init()` 和 `MX_ADC1_Init()` 之后添加：

```c
if (EnvironmentMonitor_Init(&huart2, &hadc1) != HAL_OK) {
    Error_Handler();
}
```

用以下循环替换默认 `while (1)` 的内容：

```c
while (1) {
    EnvironmentMonitor_Process();
}
```
