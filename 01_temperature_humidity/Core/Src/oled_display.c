#include "oled_display.h"
#include "oled.h"
#include <stdio.h>

#define OLED_LINE_LENGTH 16U   /* 屏幕一行能显示16个字符（8x16字体下） */

/* 初始化底层OLED驱动，并在两行分别显示"温度/湿度"占位符，等真实数据来了再刷新 */
void OLEDDisplay_Init(void)
{
    OLED_Init();
    OLED_Display_str(0U, 0U, 8U, 16U, (u8 *)"TEMP: --.-C     ");
    OLED_Display_str(0U, 2U, 8U, 16U, (u8 *)"HUMI: --.-%     ");
}

/* 把最新的温湿度数值格式化成固定宽度的字符串并刷新到屏幕上两行 */
void OLEDDisplay_ShowTemperatureHumidity(float temperature_c, float humidity_percent)
{
    char temperature_line[OLED_LINE_LENGTH + 1U];
    char humidity_line[OLED_LINE_LENGTH + 1U];

    /* 末尾补足空格，是为了覆盖掉上一次显示的内容里可能残留的多余字符（比如从两位数变回一位数） */
    (void)snprintf(temperature_line, sizeof(temperature_line),
                   "TEMP:%5.1fC     ", (double)temperature_c);
    (void)snprintf(humidity_line, sizeof(humidity_line),
                   "HUMI:%5.1f%%     ", (double)humidity_percent);
    OLED_Display_str(0U, 0U, 8U, 16U, (u8 *)temperature_line);
    OLED_Display_str(0U, 2U, 8U, 16U, (u8 *)humidity_line);
}
