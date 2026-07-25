#include "oled_display.h"
#include "oled.h"
#include <stdio.h>

#define OLED_LINE_LENGTH 16U

/* 初始化屏幕，第一行显示烟雾浓度占位符，第二行显示蜂鸣器初始状态(OFF) */
void OLEDDisplay_Init(void)
{
    OLED_Init();
    OLED_Display_str(0U, 0U, 8U, 16U, (u8 *)"SMOKE:---ppm    ");
    OLEDDisplay_ShowBeeper(false);
}

/* 刷新屏幕第一行显示的烟雾浓度值 */
void OLEDDisplay_ShowSmoke(uint16_t smoke_concentration)
{
    char smoke_line[24U];

    (void)snprintf(smoke_line, sizeof(smoke_line),
                   "SMOKE:%3uppm    ", (unsigned int)smoke_concentration);
    OLED_Display_str(0U, 0U, 8U, 16U, (u8 *)smoke_line);
}

/* 刷新屏幕第二行显示的蜂鸣器状态（ON/OFF），SetBeeper每次改变蜂鸣器状态时都会调用这个函数同步显示 */
void OLEDDisplay_ShowBeeper(bool enabled)
{
    OLED_Display_str(0U, 2U, 8U, 16U,
                     (u8 *)(enabled ? "BEEP:ON         " : "BEEP:OFF        "));
}
