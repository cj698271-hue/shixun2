#include "oled.h"

/*******************************************************************************
 * 函数名：OLED_SPI_ReadWriteOneByte
 * 功  能：通过模拟SPI向OLED写入一个字节（命令或数据）
 * 参  数：data – 要发送的字节
 *         cmd  – 0表示命令，1表示数据
 * 返回值：无
 *******************************************************************************/
void OLED_SPI_ReadWriteOneByte(u8 data, u8 cmd)
{
    u8 i;
    if(cmd) OLED_DC(1);   // 数据模式
    else    OLED_DC(0);   // 命令模式
    OLED_CS(0);           // 片选拉低，选中OLED
    OLED_SCK(0);          // 时钟起始为低
    for(i=0; i<8; i++)
    {
        OLED_SCK(0);
        if(data & 0x80) OLED_MOSI(1);   // 最高位先发送
        else            OLED_MOSI(0);
        OLED_SCK(1);                    // 时钟上升沿锁存数据
        data <<= 1;                     // 左移准备下一位
    }
    OLED_CS(1);           // 片选拉高，释放总线
    OLED_SCK(0);          // 时钟恢复低电平
}

/*******************************************************************************
 * 函数名：OLED_Init
 * 功  能：初始化OLED显示屏（SSD1306）
 * 参  数：无
 * 返回值：无
 *******************************************************************************/
void OLED_Init(void)
{
    // 硬件复位：先拉低再拉高，保持至少100ms
    OLED_RES(1);
    HAL_Delay(100);
    OLED_RES(0);
    HAL_Delay(100);
    OLED_RES(1);
    HAL_Delay(100);

    OLED_SPI_ReadWriteOneByte(0xAE, OLED_CMD); // 关闭显示
    OLED_SPI_ReadWriteOneByte(0x40, OLED_CMD); // 设置显示起始行（0x40~0x7F）
    OLED_SPI_ReadWriteOneByte(0xB0, OLED_CMD); // 设置页地址（Page 0）
    OLED_SPI_ReadWriteOneByte(0xC8, OLED_CMD); // 扫描方向：从上到下（正常）
    OLED_SPI_ReadWriteOneByte(0x81, OLED_CMD); // 设置对比度
    OLED_SPI_ReadWriteOneByte(0xFF, OLED_CMD); // 对比度最大值
    OLED_SPI_ReadWriteOneByte(0xA1, OLED_CMD); // 段重映射：列地址0对应SEG0
    OLED_SPI_ReadWriteOneByte(0xA6, OLED_CMD); // 正常显示（非反色）
    OLED_SPI_ReadWriteOneByte(0xA8, OLED_CMD); // 设置多路复用比
    OLED_SPI_ReadWriteOneByte(0x1F, OLED_CMD); // 32行（适合128×32 OLED）
    OLED_SPI_ReadWriteOneByte(0xD3, OLED_CMD); // 设置显示偏移
    OLED_SPI_ReadWriteOneByte(0x00, OLED_CMD); // 偏移量为0
    OLED_SPI_ReadWriteOneByte(0xD5, OLED_CMD); // 设置时钟分频/振荡频率
    OLED_SPI_ReadWriteOneByte(0xF0, OLED_CMD); // 分频因子1，频率较高
    OLED_SPI_ReadWriteOneByte(0xD9, OLED_CMD); // 设置预充电周期
    OLED_SPI_ReadWriteOneByte(0x22, OLED_CMD); // 相位1:2 DCLK，相位2:2 DCLK
    OLED_SPI_ReadWriteOneByte(0xDA, OLED_CMD); // 设置COM引脚硬件配置
    OLED_SPI_ReadWriteOneByte(0x02, OLED_CMD); // 适用于128×32
    OLED_SPI_ReadWriteOneByte(0xDB, OLED_CMD); // 设置VCOMH电压
    OLED_SPI_ReadWriteOneByte(0x49, OLED_CMD); // VCOMH = 0.77×VCC
    OLED_SPI_ReadWriteOneByte(0x8D, OLED_CMD); // 启用电荷泵
    OLED_SPI_ReadWriteOneByte(0x14, OLED_CMD); // 电荷泵开启
    OLED_SPI_ReadWriteOneByte(0xAF, OLED_CMD); // 打开显示
    OLED_Clear();                               // 清屏
}

/*******************************************************************************
 * 函数名：OLED_Clear
 * 功  能：清空OLED显示（所有像素熄灭）
 * 参  数：无
 * 返回值：无
 *******************************************************************************/
void OLED_Clear(void)
{
    u8 i, j;
    for(i=0; i<4; i++)          // 4页（每页8行，共32行）
    {
        OLED_SPI_ReadWriteOneByte(0xB0 + i, OLED_CMD); // 设置页地址
        OLED_SPI_ReadWriteOneByte(0x10, OLED_CMD);     // 列高4位
        OLED_SPI_ReadWriteOneByte(0x00, OLED_CMD);     // 列低4位
        for(j=0; j<128; j++)
            OLED_SPI_ReadWriteOneByte(0x00, OLED_DAT); // 写入0（熄灭）
    }
}

/*******************************************************************************
 * 函数名：OLED_Set_Pos
 * 功  能：设置OLED写入位置（列地址和页地址）
 * 参  数：x – 列地址（0~127）
 *         y – 页地址（0~4，对应行0~39，但128×32只用0~3）
 * 返回值：无
 *******************************************************************************/
void OLED_Set_Pos(u8 x, u8 y)
{
    OLED_SPI_ReadWriteOneByte((0xB0 | y) & 0xB7, OLED_CMD); // 页地址
    OLED_SPI_ReadWriteOneByte(0x10 | (x >> 4), OLED_CMD);   // 列高4位
    OLED_SPI_ReadWriteOneByte(0x00 | (x & 0x0F), OLED_CMD); // 列低4位
}

/*******************************************************************************
 * 函数名：OLED_Draw_Point
 * 功  能：在GRAM中画一个点（配合OLED_Refresh_Gram使用）
 * 参  数：x   – 列坐标（0~127）
 *         y   – 行坐标（0~63）
 *         dat – 0熄灭，1点亮
 * 返回值：无
 *******************************************************************************/
u8 OLED_GRAM[8][128];   // 显存缓冲区（8页×128列）
void OLED_Draw_Point(u8 x, u8 y, u8 dat)
{
    u8 page = y / 8;     // 计算所在页
    if(dat)
        OLED_GRAM[page][x] |= (1 << (y % 8));   // 点亮对应位
    else
        OLED_GRAM[page][x] &= ~(1 << (y % 8));  // 熄灭对应位
}

/*******************************************************************************
 * 函数名：OLED_Refresh_Gram
 * 功  能：将GRAM缓冲区的内容一次性刷新到OLED
 * 参  数：无
 * 返回值：无
 *******************************************************************************/
void OLED_Refresh_Gram(void)
{
    u8 i, j;
    for(i=0; i<8; i++)
    {
        OLED_SPI_ReadWriteOneByte(0xB0 + i, OLED_CMD);
        OLED_SPI_ReadWriteOneByte(0x10, OLED_CMD);
        OLED_SPI_ReadWriteOneByte(0x00, OLED_CMD);
        for(j=0; j<128; j++)
            OLED_SPI_ReadWriteOneByte(OLED_GRAM[i][j], OLED_DAT);
    }
}

/*******************************************************************************
 * 函数名：OLED_Display_font
 * 功  能：在指定位置显示一个汉字（16×16 / 24×24 / 32×32）
 * 参  数：x      – 起始列坐标
 *         y      – 起始页坐标（每页8行）
 *         size   – 字体大小：16/24/32
 *         number – 汉字在字模数组中的索引
 * 返回值：无
 *******************************************************************************/
void OLED_Display_font(u8 x, u8 y, u8 size, u8 number)
{
    u16 i, j;
    for(i=0; i<size/8; i++)          // 页循环（16点→2页，24点→3页，32点→4页）
    {
        OLED_Set_Pos(x, y + i);      // 设置光标
        for(j=0; j<size; j++)        // 列循环
        {
            if(size == 16)
                OLED_SPI_ReadWriteOneByte(font_16_16[number][j + i*size], OLED_DAT);
            else if(size == 24)
                OLED_SPI_ReadWriteOneByte(font_24_24[number][j + i*size], OLED_DAT);
            else if(size == 32)
                OLED_SPI_ReadWriteOneByte(font_32_32[number][j + i*size], OLED_DAT);
        }
    }
}

/*******************************************************************************
 * 函数名：OLED_Display_char
 * 功  能：在指定位置显示一个ASCII字符（8×16 或 12×24）
 * 参  数：x   – 起始列坐标
 *         y   – 起始页坐标
 *         w   – 字符宽度（8或12）
 *         h   – 字符高度（16或24）
 *         chr – 要显示的ASCII字符
 * 返回值：无
 *******************************************************************************/
void OLED_Display_char(u8 x, u8 y, u8 w, u8 h, u8 chr)
{
    u16 i, j;
    for(i=0; i<h/8; i++)            // 页循环
    {
        OLED_Set_Pos(x, y + i);
        for(j=0; j<w; j++)
        {
            if(h == 16)   // 8×16字体
                OLED_SPI_ReadWriteOneByte(ASCII_8_16[chr - ' '][j + i*w], OLED_DAT);
            else if(h == 24)  // 12×24字体（仅支持空格、数字、小数点）
            {
                if(chr == ' ')
                    OLED_SPI_ReadWriteOneByte(ASCII_12_24[chr - ' '][j + i*w], OLED_DAT);
                else if(chr == '.')
                    OLED_SPI_ReadWriteOneByte(ASCII_12_24[chr - ' ' - 2][j + i*w], OLED_DAT);
                else  // 数字0~9
                    OLED_SPI_ReadWriteOneByte(ASCII_12_24[chr - ' ' - 15][j + i*w], OLED_DAT);
            }
        }
    }
}

/*******************************************************************************
 * 函数名：OLED_Display_str
 * 功  能：显示ASCII字符串（自动换行）
 * 参  数：x   – 起始列坐标
 *         y   – 起始页坐标
 *         w   – 字符宽度
 *         h   – 字符高度
 *         str – 字符串指针
 * 返回值：无
 *******************************************************************************/
void OLED_Display_str(u8 x, u8 y, u8 w, u8 h, u8 *str)
{
    u32 i = 0;
    while(*str != '\0')
    {
        OLED_Display_char(x + i, y, w, h, *str);
        i += w;
        str++;
        if((x + i) > 127)   // 超出屏幕宽度则换行
        {
            x = 0;
            i = 0;
            y += h / 8;     // 下移一页
            if(y >= 7) y = 0;
        }
    }
}

/*******************************************************************************
 * 函数名：OLED_Display_Imag
 * 功  能：显示一张图片（位图）
 * 参  数：x    – 起始列坐标
 *         y    – 起始页坐标
 *         w    – 图片宽度（像素）
 *         h    – 图片高度（像素）
 *         imag – 图片数据指针
 * 返回值：无
 *******************************************************************************/
void OLED_Display_Imag(u8 x, u8 y, u8 w, u8 h, u8 *imag)
{
    u8 i, j;
    u8 page = h / 8;
    if(h % 8) page += 1;      // 不足一页的按一页算
    for(i=0; i<page; i++)
    {
        OLED_Set_Pos(x, y + i);
        for(j=0; j<w; j++)
            OLED_SPI_ReadWriteOneByte(imag[j + i*w], OLED_DAT);
    }
}