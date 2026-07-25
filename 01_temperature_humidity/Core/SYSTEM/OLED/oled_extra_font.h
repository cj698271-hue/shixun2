#ifndef __OLED_EXTRA_FONT_H
#define __OLED_EXTRA_FONT_H

#include "main.h"
#include "oled.h"

/* ================= 字模全局外部声明 ================= */

/* 32×32 字模：用于开机画面的“你” */
extern const u8 font_ni_32[128];

/* 16×16 字模：用于串口混合显示的所有汉字 */
extern const u8 font_16_huan[32];   // 欢
extern const u8 font_16_ying[32];   // 迎
extern const u8 font_16_ni[32];     // 你
extern const u8 font_16_wu[32];     // 物
extern const u8 font_16_lian[32];   // 联
extern const u8 font_16_wang[32];   // 网
extern const u8 font_16_gong[32];   // 工
extern const u8 font_16_cheng[32];  // 程
extern const u8 font_16_jiao[32];   // 焦
extern const u8 font_16_chen[32];   // 琛

/* ================= 辅助显示函数声明 ================= */

/**
 * @brief 显示一个 32×32 字模（直接逐页写入OLED）
 * @param x      起始列坐标（0~127）
 * @param y_page 起始页坐标（0~3，每页8行）
 * @param glyph  字模数据指针（128字节）
 */
void OLED_Display_Glyph32(u8 x, u8 y_page, const u8 *glyph);

/**
 * @brief 显示一个 16×16 字模（上半页+下半页）
 * @param x      起始列坐标
 * @param y_page 起始页坐标
 * @param glyph  字模数据指针（32字节）
 */
void OLED_Draw_16x16(u8 x, u8 y_page, const u8 *glyph);

/**
 * @brief 显示混合字符串（GBK编码，自动区分ASCII和汉字）
 * @param str 待显示的字符串（必须以'\0'结尾）
 * @note  ASCII字符使用8×16字体，已知汉字使用16×16字体，
 *        未知汉字显示为两个'?'占满16像素宽度。
 */
void OLED_Display_Mixed_Line(u8 *str);

#endif