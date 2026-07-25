/* oled_display.h —— 对底层oled.c驱动的一层薄封装，只暴露本项目需要的两个显示动作 */
#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <stdint.h>

/* 初始化屏幕并显示初始占位内容 */
void OLEDDisplay_Init(void);
/* 刷新屏幕上显示的温度和湿度数值 */
void OLEDDisplay_ShowTemperatureHumidity(float temperature_c, float humidity_percent);

#endif
