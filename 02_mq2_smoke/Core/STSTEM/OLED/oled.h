#ifndef __OLED_H
#define __OLED_H

#include "main.h"

// 类型定义守卫（使用宏避免重复 typedef，C11 允许重定义但会报警告）
#ifndef U8_TYPEDEF
#define U8_TYPEDEF
typedef unsigned char u8;
#endif
#ifndef U16_TYPEDEF
#define U16_TYPEDEF
typedef unsigned short u16;
#endif
#ifndef U32_TYPEDEF
#define U32_TYPEDEF
typedef unsigned int u32;
#endif

// 数据/命令选择
#define OLED_DAT 1  // 写数据
#define OLED_CMD 0  // 写命令

// 引脚操作宏（使用 CubeMX 生成的引脚标签）
#define OLED_SCK(x)  HAL_GPIO_WritePin(OLED_SCK_GPIO_Port, OLED_SCK_Pin, (GPIO_PinState)x)   // 时钟
#define OLED_MOSI(x) HAL_GPIO_WritePin(OLED_MOSI_GPIO_Port, OLED_MOSI_Pin, (GPIO_PinState)x)  // 数据
#define OLED_RES(x)  HAL_GPIO_WritePin(OLED_RST_GPIO_Port, OLED_RST_Pin, (GPIO_PinState)x)    // 复位
#define OLED_DC(x)   HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, (GPIO_PinState)x)      // 数据/命令
#define OLED_CS(x)   HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, (GPIO_PinState)x)      // 片选

// 外部字库声明
extern const char android[];           // Android 图标
extern const char wbyq_logo[];         // LOGO 图标
extern const u8 font_24_24[][24 * 24/8]; // 24x24 汉字
extern const u8 font_32_32[][32 * 32/8]; // 32x32 汉字
extern const u8 ASCII_8_16[][16];      // 8x16 ASCII
extern const u8 ASCII_12_24[][36];     // 12x24 ASCII
extern const char bmp1[];
extern const char basketball[];
extern const char pandas[];
extern const u8 font_16_16[][16 * 16/8]; // 16x16 汉字

// 函数声明
void OLED_Init(void);              // OLED 初始化
void OLED_Clear(void);             // 清屏
void OLED_Refresh_Gram(void);      // 更新显示（GRAM 模式）
void OLED_Set_Pos(u8 x, u8 y);     // 设置光标位置
void OLED_SPI_ReadWriteOneByte(u8 data, u8 cmd); // SPI 写一字节（cmd=0命令, cmd=1数据）
void OLED_Display_font(u8 x, u8 y, u8 size, u8 number);  // 显示汉字
void OLED_Display_char(u8 x, u8 y, u8 w, u8 h, u8 chr);  // 显示 ASCII 字符
void OLED_Display_str(u8 x, u8 y, u8 w, u8 h, u8 *str);  // 显示字符串
void OLED_Display_Imag(u8 x, u8 y, u8 w, u8 h, u8 *imag);// 显示图片

// 兼容宏：OLED_ShowChar → OLED_Display_char（固定宽度 8 像素，用于 ASCII 8×16 字体）
#define OLED_ShowChar(x, y, chr, size) OLED_Display_char(x, y, 8, size, chr)

#endif