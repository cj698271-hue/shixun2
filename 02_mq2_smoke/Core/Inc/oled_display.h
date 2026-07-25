/* oled_display.h —— 对底层oled.c驱动的薄封装，只暴露本项目需要的显示动作 */
#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

void OLEDDisplay_Init(void);
/* 刷新第一行显示的烟雾浓度 */
void OLEDDisplay_ShowSmoke(uint16_t smoke_concentration);
/* 刷新第二行显示的蜂鸣器开关状态 */
void OLEDDisplay_ShowBeeper(bool enabled);

#endif
