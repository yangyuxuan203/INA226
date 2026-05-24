/**
 ****************************************************************************************************
 * @file        lcd.h
 * @author      锟斤拷锟斤拷原锟斤拷锟脚讹拷(ALIENTEK)
 * @version     V1.1
 * @date        2023-05-29
 * @brief       2.8锟斤拷/3.5锟斤拷/4.3锟斤拷/7锟斤拷 TFTLCD(MCU锟斤拷) 锟斤拷锟斤拷锟斤拷锟斤拷
 *              支锟斤拷锟斤拷锟斤拷IC锟酵号帮拷锟斤拷:ILI9341/NT35310/NT35510/SSD1963/ST7789/ST7796/ILI9806锟斤拷
 *
 * @license     Copyright (c) 2020-2032, 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟接科硷拷锟斤拷锟睫癸拷司
 ****************************************************************************************************
 * @attention
 *
 * 实锟斤拷平台:锟斤拷锟斤拷原锟斤拷 探锟斤拷锟斤拷 F407锟斤拷锟斤拷锟斤拷
 * 锟斤拷锟斤拷锟斤拷频:www.yuanzige.com
 * 锟斤拷锟斤拷锟斤拷坛:www.openedv.com
 * 锟斤拷司锟斤拷址:www.alientek.com
 * 锟斤拷锟斤拷锟街�:openedv.taobao.com
 *
 * 锟睫革拷说锟斤拷
 * V1.0 20211016
 * 锟斤拷一锟轿凤拷锟斤拷
 * V1.1 20230529
 * 1锟斤拷锟斤拷锟斤拷锟斤拷ST7796锟斤拷ILI9806 IC支锟斤拷
 * 2锟斤拷锟津化诧拷锟街达拷锟诫，锟斤拷锟解长锟叫讹拷
 ****************************************************************************************************
 */

#ifndef __LCD_H
#define __LCD_H

#include "stdlib.h"
#include "main.h"


/******************************************************************************************/
/* LCD RST/WR/RD/BL/CS/RS 锟斤拷锟斤拷 锟斤拷锟斤拷 
 * LCD_D0~D15,锟斤拷锟斤拷锟斤拷锟斤拷太锟斤拷,锟酵诧拷锟斤拷锟斤拷锟斤定锟斤拷锟斤拷,直锟斤拷锟斤拷lcd_init锟斤拷锟斤拷锟睫革拷.锟斤拷锟斤拷锟斤拷锟斤拷植锟斤拷时锟斤拷,锟斤拷锟剿革拷
 * 锟斤拷6锟斤拷IO锟斤拷, 锟斤拷锟矫革拷LCD_Init锟斤拷锟斤拷锟紻0~D15锟斤拷锟节碉拷IO锟斤拷.
 */

/* RESET 锟斤拷系统锟斤拷位锟脚癸拷锟斤拷 锟斤拷锟斤拷锟斤拷锟斤不锟矫讹拷锟斤拷 RESET锟斤拷锟斤拷 */
//#define LCD_RST_GPIO_PORT               GPIOx
//#define LCD_RST_GPIO_PIN                SYS_GPIO_PINx
//#define LCD_RST_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOx_CLK_ENABLE(); }while(0)   /* 锟斤拷锟斤拷IO锟斤拷时锟斤拷使锟斤拷 */

#define LCD_WR_GPIO_PORT                GPIOD
#define LCD_WR_GPIO_PIN                 GPIO_PIN_5
#define LCD_WR_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOD_CLK_ENABLE(); }while(0)     /* 锟斤拷锟斤拷IO锟斤拷时锟斤拷使锟斤拷 */

#define LCD_RD_GPIO_PORT                GPIOD
#define LCD_RD_GPIO_PIN                 GPIO_PIN_4
#define LCD_RD_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOD_CLK_ENABLE(); }while(0)     /* 锟斤拷锟斤拷IO锟斤拷时锟斤拷使锟斤拷 */

#define LCD_BL_GPIO_PORT                GPIOB
#define LCD_BL_GPIO_PIN                 GPIO_PIN_15
#define LCD_BL_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)     /* 锟斤拷锟斤拷锟斤拷锟斤拷IO锟斤拷时锟斤拷使锟斤拷 */

/* LCD_CS(锟斤拷要锟斤拷锟斤拷LCD_FSMC_NEX锟斤拷锟斤拷锟斤拷确锟斤拷IO锟斤拷) 锟斤拷 LCD_RS(锟斤拷要锟斤拷锟斤拷LCD_FSMC_AX锟斤拷锟斤拷锟斤拷确锟斤拷IO锟斤拷) 锟斤拷锟斤拷 锟斤拷锟斤拷 */
#define LCD_CS_GPIO_PORT                GPIOG
#define LCD_CS_GPIO_PIN                 GPIO_PIN_12
#define LCD_CS_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOG_CLK_ENABLE(); }while(0)     /* 锟斤拷锟斤拷IO锟斤拷时锟斤拷使锟斤拷 */

#define LCD_RS_GPIO_PORT                GPIOF
#define LCD_RS_GPIO_PIN                 GPIO_PIN_12
#define LCD_RS_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOF_CLK_ENABLE(); }while(0)     /* 浣胯兘IO鍙ｆ椂閽� */

/* FSMC锟斤拷夭锟斤拷锟� 锟斤拷锟斤拷 
 * 注锟斤拷: 锟斤拷锟斤拷默锟斤拷锟斤拷通锟斤拷FSMC锟斤拷1锟斤拷锟斤拷锟斤拷LCD, 锟斤拷1锟斤拷4锟斤拷片选: FSMC_NE1~4
 *
 * 锟睫革拷LCD_FSMC_NEX, 锟斤拷应锟斤拷LCD_CS_GPIO锟斤拷锟斤拷锟斤拷锟揭诧拷酶锟�
 * 锟睫革拷LCD_FSMC_AX , 锟斤拷应锟斤拷LCD_RS_GPIO锟斤拷锟斤拷锟斤拷锟揭诧拷酶锟�
 */
#define LCD_FSMC_NEX         4              /* 使锟斤拷FSMC_NE4锟斤拷LCD_CS,取值锟斤拷围只锟斤拷锟斤拷: 1~4 */
#define LCD_FSMC_AX          6              /* 浣跨敤FSMC_A6浣滀负LCD_RS,鍙栧€艰寖鍥�: 0 ~ 25 */

#define LCD_FSMC_BCRX        FSMC_Bank1->BTCR[(LCD_FSMC_NEX - 1) * 2]       /* BCR锟侥达拷锟斤拷,锟斤拷锟斤拷LCD_FSMC_NEX锟皆讹拷锟斤拷锟斤拷 */
#define LCD_FSMC_BTRX        FSMC_Bank1->BTCR[(LCD_FSMC_NEX - 1) * 2 + 1]   /* BTR锟侥达拷锟斤拷,锟斤拷锟斤拷LCD_FSMC_NEX锟皆讹拷锟斤拷锟斤拷 */
#define LCD_FSMC_BWTRX       FSMC_Bank1E->BWTR[(LCD_FSMC_NEX - 1) * 2]      /* BWTR锟侥达拷锟斤拷,锟斤拷锟斤拷LCD_FSMC_NEX锟皆讹拷锟斤拷锟斤拷 */

/******************************************************************************************/

/* LCD锟斤拷要锟斤拷锟斤拷锟斤拷 */
typedef struct
{
    uint16_t width;     /* LCD 锟斤拷锟斤拷 */
    uint16_t height;    /* LCD 锟竭讹拷 */
    uint16_t id;        /* LCD ID */
    uint8_t dir;        /* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟狡ｏ拷0锟斤拷锟斤拷锟斤拷锟斤拷1锟斤拷锟斤拷锟斤拷锟斤拷 */
    uint16_t wramcmd;   /* 锟斤拷始写gram指锟斤拷 */
    uint16_t setxcmd;   /* 锟斤拷锟斤拷x锟斤拷锟斤拷指锟斤拷 */
    uint16_t setycmd;   /* 锟斤拷锟斤拷y锟斤拷锟斤拷指锟斤拷 */
} _lcd_dev;

/* LCD锟斤拷锟斤拷 */
extern _lcd_dev lcddev; /* 锟斤拷锟斤拷LCD锟斤拷要锟斤拷锟斤拷 */

/* LCD锟侥伙拷锟斤拷锟斤拷色锟酵憋拷锟斤拷色 */
extern uint32_t  g_point_color;     /* 默锟较猴拷色 */
extern uint32_t  g_back_color;      /* 锟斤拷锟斤拷锟斤拷色.默锟斤拷为锟斤拷色 */

/* LCD锟斤拷锟斤拷锟斤拷锟� */
#define LCD_BL(x)   do{ x ? \
                      HAL_GPIO_WritePin(LCD_BL_GPIO_PORT, LCD_BL_GPIO_PIN, GPIO_PIN_SET) : \
                      HAL_GPIO_WritePin(LCD_BL_GPIO_PORT, LCD_BL_GPIO_PIN, GPIO_PIN_RESET); \
                     }while(0)

/* LCD锟斤拷址锟结构锟斤拷 */
typedef struct
{
    volatile uint16_t LCD_REG;
    volatile uint16_t LCD_RAM;
} LCD_TypeDef;


/* LCD_BASE锟斤拷锟斤拷细锟斤拷锟姐方锟斤拷:
 * 锟斤拷锟斤拷一锟斤拷使锟斤拷FSMC锟侥匡拷1(BANK1)锟斤拷锟斤拷锟斤拷TFTLCD液锟斤拷锟斤拷(MCU锟斤拷), 锟斤拷1锟斤拷址锟斤拷围锟杰达拷小为256MB,锟斤拷锟街筹拷4锟斤拷:
 * 锟芥储锟斤拷1(FSMC_NE1)锟斤拷址锟斤拷围: 0x6000 0000 ~ 0x63FF FFFF
 * 锟芥储锟斤拷2(FSMC_NE2)锟斤拷址锟斤拷围: 0x6400 0000 ~ 0x67FF FFFF
 * 锟芥储锟斤拷3(FSMC_NE3)锟斤拷址锟斤拷围: 0x6800 0000 ~ 0x6BFF FFFF
 * 锟芥储锟斤拷4(FSMC_NE4)锟斤拷址锟斤拷围: 0x6C00 0000 ~ 0x6FFF FFFF
 *
 * 锟斤拷锟斤拷锟斤拷要锟斤拷锟斤拷硬锟斤拷锟斤拷锟接凤拷式选锟斤拷锟斤拷实锟狡�(锟斤拷锟斤拷LCD_CS)锟酵碉拷址锟斤拷(锟斤拷锟斤拷LCD_RS)
 * 探锟斤拷锟斤拷F407锟斤拷锟斤拷锟斤拷使锟斤拷FSMC_NE4锟斤拷锟斤拷LCD_CS, FSMC_A6锟斤拷锟斤拷LCD_RS ,16位锟斤拷锟斤拷锟斤拷,锟斤拷锟姐方锟斤拷锟斤拷锟斤拷:
 * 锟斤拷锟斤拷FSMC_NE4锟侥伙拷锟斤拷址为: 0x6C00 0000;     NEX锟侥伙拷址为(x=1/2/3/4): 0x6000 0000 + (0x400 0000 * (x - 1))
 * FSMC_A6锟斤拷应锟斤拷址值: 2^6 * 2 = 0x80;    FSMC_Ay锟斤拷应锟侥碉拷址为(y = 0 ~ 25): 2^y * 2
 *
 * LCD->LCD_REG,锟斤拷应LCD_RS = 0(LCD锟侥达拷锟斤拷); LCD->LCD_RAM,锟斤拷应LCD_RS = 1(LCD锟斤拷锟斤拷)
 * 锟斤拷 LCD->LCD_RAM锟侥碉拷址为:  0x6C00 0000 + 2^6 * 2 = 0x6C00 0080
 *    LCD->LCD_REG锟侥碉拷址锟斤拷锟斤拷为 LCD->LCD_RAM之锟斤拷锟斤拷锟斤拷锟斤拷址.
 * 锟斤拷锟斤拷锟斤拷锟斤拷使锟矫结构锟斤拷锟斤拷锟絃CD_REG 锟斤拷 LCD_RAM(REG锟斤拷前,RAM锟节猴拷,锟斤拷为16位锟斤拷锟捷匡拷锟斤拷)
 * 锟斤拷锟� 锟结构锟斤拷幕锟斤拷锟街�(LCD_BASE) = LCD_RAM - 2 = 0x6C00 0080 -2
 *
 * 锟斤拷锟斤拷通锟矫的硷拷锟姐公式为((片选锟斤拷FSMC_NEX)X=1/2/3/4, (RS锟接碉拷址锟斤拷FSMC_Ay)y=0~25):
 *          LCD_BASE = (0x6000 0000 + (0x400 0000 * (x - 1))) | (2^y * 2 -2)
 *          锟斤拷效锟斤拷(使锟斤拷锟斤拷位锟斤拷锟斤拷)
 *          LCD_BASE = (0x6000 0000 + (0x400 0000 * (x - 1))) | ((1 << y) * 2 -2)
 */
#define LCD_BASE        (uint32_t)((0x60000000 + (0x4000000 * (LCD_FSMC_NEX - 1))) | (((1 << LCD_FSMC_AX) * 2) -2))
#define LCD             ((LCD_TypeDef *) LCD_BASE)

/******************************************************************************************/
/* LCD扫锟借方锟斤拷锟斤拷锟缴� 锟斤拷锟斤拷 */

/* 扫锟借方锟斤拷锟斤拷 */
#define L2R_U2D         0           /* 锟斤拷锟斤拷锟斤拷,锟斤拷锟较碉拷锟斤拷 */
#define L2R_D2U         1           /* 锟斤拷锟斤拷锟斤拷,锟斤拷锟铰碉拷锟斤拷 */
#define R2L_U2D         2           /* 锟斤拷锟揭碉拷锟斤拷,锟斤拷锟较碉拷锟斤拷 */
#define R2L_D2U         3           /* 锟斤拷锟揭碉拷锟斤拷,锟斤拷锟铰碉拷锟斤拷 */

#define U2D_L2R         4           /* 锟斤拷锟较碉拷锟斤拷,锟斤拷锟斤拷锟斤拷 */
#define U2D_R2L         5           /* 锟斤拷锟较碉拷锟斤拷,锟斤拷锟揭碉拷锟斤拷 */
#define D2U_L2R         6           /* 锟斤拷锟铰碉拷锟斤拷,锟斤拷锟斤拷锟斤拷 */
#define D2U_R2L         7           /* 锟斤拷锟铰碉拷锟斤拷,锟斤拷锟揭碉拷锟斤拷 */

#define DFT_SCAN_DIR    L2R_U2D     /* 默锟较碉拷扫锟借方锟斤拷 */

/* 锟斤拷锟矫伙拷锟斤拷锟斤拷色 */
#define WHITE           0xFFFF      /* 锟斤拷色 */
#define BLACK           0x0000      /* 锟斤拷色 */
#define RED             0xF800      /* 锟斤拷色 */
#define GREEN           0x07E0      /* 锟斤拷色 */
#define BLUE            0x001F      /* 锟斤拷色 */ 
#define MAGENTA         0xF81F      /* 品锟斤拷色/锟较猴拷色 = BLUE + RED */
#define YELLOW          0xFFE0      /* 锟斤拷色 = GREEN + RED */
#define CYAN            0x07FF      /* 锟斤拷色 = GREEN + BLUE */  

/* 锟角筹拷锟斤拷锟斤拷色 */
#define BROWN           0xBC40      /* 锟斤拷色 */
#define BRRED           0xFC07      /* 锟截猴拷色 */
#define GRAY            0x8430      /* 锟斤拷色 */ 
#define DARKBLUE        0x01CF      /* 锟斤拷锟斤拷色 */
#define LIGHTBLUE       0x7D7C      /* 浅锟斤拷色 */ 
#define GRAYBLUE        0x5458      /* 锟斤拷锟斤拷色 */ 
#define LIGHTGREEN      0x841F      /* 浅锟斤拷色 */  
#define LGRAY           0xC618      /* 浅锟斤拷色(PANNEL),锟斤拷锟藉背锟斤拷色 */ 
#define LGRAYBLUE       0xA651      /* 浅锟斤拷锟斤拷色(锟叫硷拷锟斤拷锟缴�) */ 
#define LBBLUE          0x2B12      /* 浅锟斤拷锟斤拷色(选锟斤拷锟斤拷目锟侥凤拷色) */ 

/******************************************************************************************/
/* SSD1963锟斤拷锟斤拷锟斤拷貌锟斤拷锟�(一锟姐不锟矫革拷) */

/* LCD锟街憋拷锟斤拷锟斤拷锟斤拷 */ 
#define SSD_HOR_RESOLUTION      800     /* LCD水平锟街憋拷锟斤拷 */ 
#define SSD_VER_RESOLUTION      480     /* LCD锟斤拷直锟街憋拷锟斤拷 */ 

/* LCD锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 */ 
#define SSD_HOR_PULSE_WIDTH     1       /* 水平锟斤拷锟斤拷 */ 
#define SSD_HOR_BACK_PORCH      46      /* 水平前锟斤拷 */ 
#define SSD_HOR_FRONT_PORCH     210     /* 水平锟斤拷锟斤拷 */ 

#define SSD_VER_PULSE_WIDTH     1       /* 锟斤拷直锟斤拷锟斤拷 */ 
#define SSD_VER_BACK_PORCH      23      /* 锟斤拷直前锟斤拷 */ 
#define SSD_VER_FRONT_PORCH     22      /* 锟斤拷直前锟斤拷 */ 

/* 锟斤拷锟铰硷拷锟斤拷锟斤拷锟斤拷锟斤拷锟皆讹拷锟斤拷锟斤拷 */ 
#define SSD_HT          (SSD_HOR_RESOLUTION + SSD_HOR_BACK_PORCH + SSD_HOR_FRONT_PORCH)
#define SSD_HPS         (SSD_HOR_BACK_PORCH)
#define SSD_VT          (SSD_VER_RESOLUTION + SSD_VER_BACK_PORCH + SSD_VER_FRONT_PORCH)
#define SSD_VPS         (SSD_VER_BACK_PORCH)
   
/******************************************************************************************/
/* 锟斤拷锟斤拷锟斤拷锟斤拷 */

void lcd_wr_data(volatile uint16_t data);            /* LCD写锟斤拷锟斤拷 */
void lcd_wr_regno(volatile uint16_t regno);          /* LCD写锟侥达拷锟斤拷锟斤拷锟�/锟斤拷址 */
void lcd_write_reg(uint16_t regno, uint16_t data);   /* LCD写锟侥达拷锟斤拷锟斤拷值 */

void lcd_init(void);                        /* 锟斤拷始锟斤拷LCD */ 
void lcd_display_on(void);                  /* 锟斤拷锟斤拷示 */ 
void lcd_display_off(void);                 /* 锟斤拷锟斤拷示 */
void lcd_scan_dir(uint8_t dir);             /* 锟斤拷锟斤拷锟斤拷扫锟借方锟斤拷 */ 
void lcd_display_dir(uint8_t dir);          /* 锟斤拷锟斤拷锟斤拷幕锟斤拷示锟斤拷锟斤拷 */ 
void lcd_ssd_backlight_set(uint8_t pwm);    /* SSD1963 锟斤拷锟斤拷锟斤拷锟� */ 

void lcd_write_ram_prepare(void);                           /* 准锟斤拷写GRAM */ 
void lcd_set_cursor(uint16_t x, uint16_t y);                /* 锟斤拷锟矫癸拷锟� */ 
uint32_t lcd_read_point(uint16_t x, uint16_t y);            /* 锟斤拷锟斤拷(32位锟斤拷色,锟斤拷锟斤拷LTDC) */
void lcd_draw_point(uint16_t x, uint16_t y, uint32_t color);/* 锟斤拷锟斤拷(32位锟斤拷色,锟斤拷锟斤拷LTDC) */

void lcd_clear(uint16_t color);                                                             /* LCD锟斤拷锟斤拷 */
void lcd_fill_circle(uint16_t x, uint16_t y, uint16_t r, uint16_t color);                   /* 锟斤拷锟绞碉拷锟皆� */
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color);                  /* 锟斤拷圆 */
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color);                  /* 锟斤拷水平锟斤拷 */
void lcd_set_window(uint16_t sx, uint16_t sy, uint16_t width, uint16_t height);             /* 锟斤拷锟矫达拷锟斤拷 */
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color);          /* 锟斤拷色锟斤拷锟斤拷锟斤拷(32位锟斤拷色,锟斤拷锟斤拷LTDC) */
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color);   /* 锟斤拷色锟斤拷锟斤拷锟斤拷 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);     /* 锟斤拷直锟斤拷 */
void lcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);/* 锟斤拷锟斤拷锟斤拷 */

void lcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint16_t color);                       /* 锟斤拷示一锟斤拷锟街凤拷 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color);                     /* 锟斤拷示锟斤拷锟斤拷 */
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color);      /* 锟斤拷展锟斤拷示锟斤拷锟斤拷 */
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color);   /* 锟斤拷示锟街凤拷锟斤拷 */

#endif

















