/**
 ****************************************************************************************************
 * @file        touch.c
 * @author      锟斤拷锟斤拷原锟斤拷锟脚讹拷(ALIENTEK)
 * @version     V1.1
 * @date        2023-05-29
 * @brief       锟斤拷锟斤拷锟斤拷 锟斤拷锟斤拷锟斤拷锟斤拷
 *   @note      支锟街碉拷锟斤拷/锟斤拷锟斤拷式锟斤拷锟斤拷锟斤拷
 *              锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷支锟斤拷ADS7843/7846/UH7843/7846/XPT2046/TSC2046/GT9147/GT9271/FT5206/GT1151锟饺ｏ拷锟斤拷锟斤拷
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
 * V1.0 20211025
 * 锟斤拷一锟轿凤拷锟斤拷
 * V1.1 20230529
 * 1锟斤拷锟斤拷锟斤拷锟斤拷ST7796 3.5锟斤拷锟斤拷 GT1151锟斤拷支锟斤拷
 * 2锟斤拷锟斤拷锟斤拷锟斤拷ILI9806 4.3锟斤拷锟斤拷 GT1151锟斤拷支锟斤拷
 ****************************************************************************************************
 */

#include "stdio.h"
#include "stdlib.h"
#include "lcd.h"
#include "touch.h"
#include "delay.h"
#include "24cxx.h"
#include "gt9xxx.h"
#include "ft5206.h"


_m_tp_dev tp_dev =
{
    .init = tp_init,
    .scan = tp_scan,
    .adjust = tp_adjust,
    .xfac = 0,
    .yfac = 0,
    .xc = 0,
    .yc = 0,
    .x = {0},
    .y = {0},
    .sta = 0,
    .touchtype = 0,
};

/**
 * @brief       SPI写锟斤拷锟斤拷
 *   @note      锟斤拷锟斤拷锟斤拷IC写锟斤拷1 byte锟斤拷锟斤拷
 * @param       data: 要写锟斤拷锟斤拷锟斤拷锟�
 * @retval      锟斤拷
 */
static void tp_write_byte(uint8_t data)
{
    uint8_t count = 0;

    for (count = 0; count < 8; count++)
    {
        if (data & 0x80)    /* 锟斤拷锟斤拷1 */
        {
            T_MOSI(1);
        }
        else                /* 锟斤拷锟斤拷0 */
        {
            T_MOSI(0);
        }

        data <<= 1;
        T_CLK(0);
        delay_us(1);
        T_CLK(1);           /* 锟斤拷锟斤拷锟斤拷锟斤拷效 */
    }
}

/**
 * @brief       SPI锟斤拷锟斤拷锟斤拷
 *   @note      锟接达拷锟斤拷锟斤拷IC锟斤拷取adc值
 * @param       cmd: 指锟斤拷
 * @retval      锟斤拷取锟斤拷锟斤拷锟斤拷锟斤拷,ADC值(12bit)
 */
static uint16_t tp_read_ad(uint8_t cmd)
{
    uint8_t count = 0;
    uint16_t num = 0;
    
    T_CLK(0);           /* 锟斤拷锟斤拷锟斤拷时锟斤拷 */
    T_MOSI(0);          /* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 */
    T_CS(0);            /* 选锟叫达拷锟斤拷锟斤拷IC */
    tp_write_byte(cmd); /* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 */
    delay_us(6);        /* ADS7846锟斤拷转锟斤拷时锟斤拷锟筋长为6us */
    T_CLK(0);
    delay_us(1);
    T_CLK(1);           /* 锟斤拷1锟斤拷时锟接ｏ拷锟斤拷锟紹USY */
    delay_us(1);
    T_CLK(0);

    for (count = 0; count < 16; count++)    /* 锟斤拷锟斤拷16位锟斤拷锟斤拷,只锟叫革拷12位锟斤拷效 */
    {
        num <<= 1;
        T_CLK(0);       /* 锟铰斤拷锟斤拷锟斤拷效 */
        delay_us(1);
        T_CLK(1);

        if (T_MISO) num++;
    }

    num >>= 4;          /* 只锟叫革拷12位锟斤拷效. */
    T_CS(1);            /* 锟酵凤拷片选 */
    return num;
}

/* 锟斤拷锟借触锟斤拷锟斤拷锟斤拷芯片 锟斤拷锟捷采硷拷 锟剿诧拷锟矫诧拷锟斤拷 */
#define TP_READ_TIMES   5       /* 锟斤拷取锟斤拷锟斤拷 */
#define TP_LOST_VAL     1       /* 锟斤拷锟斤拷值 */

/**
 * @brief       锟斤拷取一锟斤拷锟斤拷锟斤拷值(x锟斤拷锟斤拷y)
 *   @note      锟斤拷锟斤拷锟斤拷取TP_READ_TIMES锟斤拷锟斤拷锟斤拷,锟斤拷锟斤拷些锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷,
 *              然锟斤拷去锟斤拷锟斤拷秃锟斤拷锟斤拷TP_LOST_VAL锟斤拷锟斤拷, 取平锟斤拷值
 *              锟斤拷锟斤拷时锟斤拷锟斤拷锟斤拷: TP_READ_TIMES > 2*TP_LOST_VAL 锟斤拷锟斤拷锟斤拷
 *
 * @param       cmd : 指锟斤拷
 *   @arg       0XD0: 锟斤拷取X锟斤拷锟斤拷锟斤拷(@锟斤拷锟斤拷状态,锟斤拷锟斤拷状态锟斤拷Y锟皆碉拷.)
 *   @arg       0X90: 锟斤拷取Y锟斤拷锟斤拷锟斤拷(@锟斤拷锟斤拷状态,锟斤拷锟斤拷状态锟斤拷X锟皆碉拷.)
 *
 * @retval      锟斤拷取锟斤拷锟斤拷锟斤拷锟斤拷(锟剿诧拷锟斤拷锟�), ADC值(12bit)
 */
static uint16_t tp_read_xoy(uint8_t cmd)
{
    uint16_t i, j;
    uint16_t buf[TP_READ_TIMES];
    uint16_t sum = 0;
    uint16_t temp;

    for (i = 0; i < TP_READ_TIMES; i++)     /* 锟饺讹拷取TP_READ_TIMES锟斤拷锟斤拷锟斤拷 */
    {
        buf[i] = tp_read_ad(cmd);
    }

    for (i = 0; i < TP_READ_TIMES - 1; i++) /* 锟斤拷锟斤拷锟捷斤拷锟斤拷锟斤拷锟斤拷 */
    {
        for (j = i + 1; j < TP_READ_TIMES; j++)
        {
            if (buf[i] > buf[j])   /* 锟斤拷锟斤拷锟斤拷锟斤拷 */
            {
                temp = buf[i];
                buf[i] = buf[j];
                buf[j] = temp;
            }
        }
    }

    sum = 0;

    for (i = TP_LOST_VAL; i < TP_READ_TIMES - TP_LOST_VAL; i++)   /* 去锟斤拷锟斤拷锟剿的讹拷锟斤拷值 */
    {
        sum += buf[i];  /* 锟桔硷拷去锟斤拷锟斤拷锟斤拷值锟皆猴拷锟斤拷锟斤拷锟�. */
    }

    temp = sum / (TP_READ_TIMES - 2 * TP_LOST_VAL); /* 取平锟斤拷值 */
    return temp;
}

/**
 * @brief       锟斤拷取x, y锟斤拷锟斤拷
 * @param       x,y: 锟斤拷取锟斤拷锟斤拷锟斤拷锟斤拷值
 * @retval      锟斤拷
 */
static void tp_read_xy(uint16_t *x, uint16_t *y)
{
    uint16_t xval, yval;

    if (tp_dev.touchtype & 0X01)    /* X,Y锟斤拷锟斤拷锟斤拷锟斤拷幕锟洁反 */
    {
        xval = tp_read_xoy(0X90);   /* 锟斤拷取X锟斤拷锟斤拷锟斤拷AD值, 锟斤拷锟斤拷锟叫凤拷锟斤拷浠� */
        yval = tp_read_xoy(0XD0);   /* 锟斤拷取Y锟斤拷锟斤拷锟斤拷AD值 */
    }
    else                            /* X,Y锟斤拷锟斤拷锟斤拷锟斤拷幕锟斤拷同 */
    {
        xval = tp_read_xoy(0XD0);   /* 锟斤拷取X锟斤拷锟斤拷锟斤拷AD值 */
        yval = tp_read_xoy(0X90);   /* 锟斤拷取Y锟斤拷锟斤拷锟斤拷AD值 */
    }

    *x = xval;
    *y = yval;
}

/* 锟斤拷锟斤拷锟斤拷锟轿讹拷取X,Y锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟街� */
#define TP_ERR_RANGE    50      /* 锟斤拷罘段� */

/**
 * @brief       锟斤拷锟斤拷锟斤拷取2锟轿达拷锟斤拷IC锟斤拷锟斤拷, 锟斤拷锟剿诧拷
 *   @note      锟斤拷锟斤拷2锟轿讹拷取锟斤拷锟斤拷锟斤拷IC,锟斤拷锟斤拷锟斤拷锟轿碉拷偏锟筋不锟杰筹拷锟斤拷ERR_RANGE,锟斤拷锟斤拷
 *              锟斤拷锟斤拷,锟斤拷锟斤拷为锟斤拷锟斤拷锟斤拷确,锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟�.锟矫猴拷锟斤拷锟杰达拷锟斤拷锟斤拷准确锟斤拷.
 *
 * @param       x,y: 锟斤拷取锟斤拷锟斤拷锟斤拷锟斤拷值
 * @retval      0, 失锟斤拷; 1, 锟缴癸拷;
 */
static uint8_t tp_read_xy2(uint16_t *x, uint16_t *y)
{
    uint16_t x1, y1;
    uint16_t x2, y2;

    tp_read_xy(&x1, &y1);   /* 锟斤拷取锟斤拷一锟斤拷锟斤拷锟斤拷 */
    tp_read_xy(&x2, &y2);   /* 锟斤拷取锟节讹拷锟斤拷锟斤拷锟斤拷 */

    /* 前锟斤拷锟斤拷锟轿诧拷锟斤拷锟斤拷+-TP_ERR_RANGE锟斤拷 */
    if (((x2 <= x1 && x1 < x2 + TP_ERR_RANGE) || (x1 <= x2 && x2 < x1 + TP_ERR_RANGE)) &&
            ((y2 <= y1 && y1 < y2 + TP_ERR_RANGE) || (y1 <= y2 && y2 < y1 + TP_ERR_RANGE)))
    {
        *x = (x1 + x2) / 2;
        *y = (y1 + y2) / 2;
        return 1;
    }

    return 0;
}

/******************************************************************************************/
/* 锟斤拷LCD锟斤拷锟斤拷锟叫关的猴拷锟斤拷, 锟斤拷锟斤拷校准锟矫碉拷 */

/**
 * @brief       锟斤拷一锟斤拷校准锟矫的达拷锟斤拷锟斤拷(十锟街硷拷)
 * @param       x,y   : 锟斤拷锟斤拷
 * @param       color : 锟斤拷色
 * @retval      锟斤拷
 */
static void tp_draw_touch_point(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_draw_line(x - 12, y, x + 13, y, color); /* 锟斤拷锟斤拷 */
    lcd_draw_line(x, y - 12, x, y + 13, color); /* 锟斤拷锟斤拷 */
    lcd_draw_point(x + 1, y + 1, color);
    lcd_draw_point(x - 1, y + 1, color);
    lcd_draw_point(x + 1, y - 1, color);
    lcd_draw_point(x - 1, y - 1, color);
    lcd_draw_circle(x, y, 6, color);            /* 锟斤拷锟斤拷锟斤拷圈 */
}

/**
 * @brief       锟斤拷一锟斤拷锟斤拷锟�(2*2锟侥碉拷)
 * @param       x,y   : 锟斤拷锟斤拷
 * @param       color : 锟斤拷色
 * @retval      锟斤拷
 */
void tp_draw_big_point(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_draw_point(x, y, color);       /* 锟斤拷锟侥碉拷 */
    lcd_draw_point(x + 1, y, color);
    lcd_draw_point(x, y + 1, color);
    lcd_draw_point(x + 1, y + 1, color);
}

/******************************************************************************************/

/**
 * @brief       锟斤拷锟斤拷锟斤拷锟斤拷扫锟斤拷
 * @param       mode: 锟斤拷锟斤拷模式
 *   @arg       0, 锟斤拷幕锟斤拷锟斤拷;
 *   @arg       1, 锟斤拷锟斤拷锟斤拷锟斤拷(校准锟斤拷锟斤拷锟解场锟斤拷锟斤拷)
 *
 * @retval      0, 锟斤拷锟斤拷锟睫达拷锟斤拷; 1, 锟斤拷锟斤拷锟叫达拷锟斤拷;
 */
uint8_t tp_scan(uint8_t mode)
{
    if (T_PEN == 0)     /* 锟叫帮拷锟斤拷锟斤拷锟斤拷 */
    {
        if (mode)       /* 锟斤拷取锟斤拷锟斤拷锟斤拷锟斤拷, 锟斤拷锟斤拷转锟斤拷 */
        {
            tp_read_xy2(&tp_dev.x[0], &tp_dev.y[0]);
        }
        else if (tp_read_xy2(&tp_dev.x[0], &tp_dev.y[0]))     /* 锟斤拷取锟斤拷幕锟斤拷锟斤拷, 锟斤拷要转锟斤拷 */
        {
            /* 锟斤拷X锟斤拷 锟斤拷锟斤拷锟斤拷锟斤拷转锟斤拷锟斤拷锟竭硷拷锟斤拷锟斤拷(锟斤拷锟斤拷应LCD锟斤拷幕锟斤拷锟斤拷锟絏锟斤拷锟斤拷值) */
            tp_dev.x[0] = (signed short)(tp_dev.x[0] - tp_dev.xc) / tp_dev.xfac + lcddev.width / 2;

            /* 锟斤拷Y锟斤拷 锟斤拷锟斤拷锟斤拷锟斤拷转锟斤拷锟斤拷锟竭硷拷锟斤拷锟斤拷(锟斤拷锟斤拷应LCD锟斤拷幕锟斤拷锟斤拷锟結锟斤拷锟斤拷值) */
            tp_dev.y[0] = (signed short)(tp_dev.y[0] - tp_dev.yc) / tp_dev.yfac + lcddev.height / 2;
        }

        if ((tp_dev.sta & TP_PRES_DOWN) == 0)   /* 之前没锟叫憋拷锟斤拷锟斤拷 */
        {
            tp_dev.sta = TP_PRES_DOWN | TP_CATH_PRES;   /* 锟斤拷锟斤拷锟斤拷锟斤拷 */
            tp_dev.x[CT_MAX_TOUCH - 1] = tp_dev.x[0];   /* 锟斤拷录锟斤拷一锟轿帮拷锟斤拷时锟斤拷锟斤拷锟斤拷 */
            tp_dev.y[CT_MAX_TOUCH - 1] = tp_dev.y[0];
        }
    }
    else
    {
        if (tp_dev.sta & TP_PRES_DOWN)      /* 之前锟角憋拷锟斤拷锟铰碉拷 */
        {
            tp_dev.sta &= ~TP_PRES_DOWN;    /* 锟斤拷前锟斤拷锟斤拷煽锟� */
        }
        else     /* 之前锟斤拷没锟叫憋拷锟斤拷锟斤拷 */
        {
            tp_dev.x[CT_MAX_TOUCH - 1] = 0;
            tp_dev.y[CT_MAX_TOUCH - 1] = 0;
            tp_dev.x[0] = 0xFFFF;
            tp_dev.y[0] = 0xFFFF;
        }
    }

    return (tp_dev.sta & TP_PRES_DOWN) ? 1 : 0;
}

/* TP_SAVE_ADDR_BASE锟斤拷锟藉触锟斤拷锟斤拷校准锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷EEPROM锟斤拷锟斤拷锟轿伙拷锟�(锟斤拷始锟斤拷址)
 * 占锟矫空硷拷 : 13锟街斤拷.
 */
#define TP_SAVE_ADDR_BASE   40

/**
 * @brief       锟斤拷锟斤拷校准锟斤拷锟斤拷
 *   @note      锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷EEPROM芯片锟斤拷锟斤拷(24C02),锟斤拷始锟斤拷址为TP_SAVE_ADDR_BASE.
 *              占锟矫达拷小为13锟街斤拷
 * @param       锟斤拷
 * @retval      锟斤拷
 */
void tp_save_adjust_data(void)
{
    uint8_t *p = (uint8_t *)&tp_dev.xfac;   /* 指锟斤拷锟阶碉拷址 */

    /* p指锟斤拷tp_dev.xfac锟侥碉拷址, p+4锟斤拷锟斤拷tp_dev.yfac锟侥碉拷址
     * p+8锟斤拷锟斤拷tp_dev.xoff锟侥碉拷址,p+10,锟斤拷锟斤拷tp_dev.yoff锟侥碉拷址
     * 锟杰癸拷占锟斤拷12锟斤拷锟街斤拷(4锟斤拷锟斤拷锟斤拷)
     * p+12锟斤拷锟节达拷疟锟角碉拷锟借触锟斤拷锟斤拷锟角凤拷校准锟斤拷锟斤拷锟斤拷(0X0A)
     * 锟斤拷p[12]写锟斤拷0X0A. 锟斤拷锟斤拷丫锟叫Ｗ硷拷锟�.
     */
    at24cxx_write(TP_SAVE_ADDR_BASE, p, 12);                /* 锟斤拷锟斤拷12锟斤拷锟街斤拷锟斤拷锟斤拷(xfac,yfac,xc,yc) */
    at24cxx_write_one_byte(TP_SAVE_ADDR_BASE + 12, 0X0A);   /* 锟斤拷锟斤拷校准值 */
}

/**
 * @brief       锟斤拷取锟斤拷锟斤拷锟斤拷EEPROM锟斤拷锟斤拷锟叫Ｗ贾�
 * @param       锟斤拷
 * @retval      0锟斤拷锟斤拷取失锟杰ｏ拷要锟斤拷锟斤拷校准
 *              1锟斤拷锟缴癸拷锟斤拷取锟斤拷锟斤拷
 */
uint8_t tp_get_adjust_data(void)
{
    uint8_t *p = (uint8_t *)&tp_dev.xfac;
    uint8_t temp = 0;

    /* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷直锟斤拷指锟斤拷tp_dev.xfac锟斤拷址锟斤拷锟叫憋拷锟斤拷锟�, 锟斤拷取锟斤拷时锟斤拷,锟斤拷锟斤拷取锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷
     * 写锟斤拷指锟斤拷tp_dev.xfac锟斤拷锟阶碉拷址, 锟酵匡拷锟皆伙拷原写锟斤拷锟饺ワ拷锟街�, 锟斤拷锟斤拷锟斤拷要锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷
     * 锟斤拷锟斤拷锟斤拷. 锟剿凤拷锟斤拷锟斤拷锟斤拷锟节革拷锟斤拷锟斤拷锟斤拷(锟斤拷锟斤拷锟结构锟斤拷)锟侥憋拷锟斤拷/锟斤拷取(锟斤拷锟斤拷锟结构锟斤拷).
     */
    at24cxx_read(TP_SAVE_ADDR_BASE, p, 12);                 /* 锟斤拷取12锟街斤拷锟斤拷锟斤拷 */
    temp = at24cxx_read_one_byte(TP_SAVE_ADDR_BASE + 12);   /* 锟斤拷取校准状态锟斤拷锟� */

    if (temp == 0X0A)
    {
        return 1;
    }

    return 0;
}

/* 锟斤拷示锟街凤拷锟斤拷 */
char *const TP_REMIND_MSG_TBL = "Please use the stylus click the cross on the screen.The cross will always move until the screen adjustment is completed.";

/**
 * @brief       锟斤拷示校准锟斤拷锟�(锟斤拷锟斤拷锟斤拷锟斤拷)
 * @param       xy[5][2]: 5锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷值
 * @param       px,py   : x,y锟斤拷锟斤拷谋锟斤拷锟斤拷锟斤拷锟�(约锟接斤拷1越锟斤拷)
 * @retval      锟斤拷
 */
static void tp_adjust_info_show(uint16_t xy[5][2], double px, double py)
{
    uint8_t i;
    char sbuf[20];

    for (i = 0; i < 5; i++)   /* 锟斤拷示5锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷值 */
    {
        sprintf(sbuf, "x%d:%d", i + 1, xy[i][0]);
        lcd_show_string(40, 160 + (i * 20), lcddev.width, lcddev.height, 16, sbuf, RED);
        sprintf(sbuf, "y%d:%d", i + 1, xy[i][1]);
        lcd_show_string(40 + 80, 160 + (i * 20), lcddev.width, lcddev.height, 16, sbuf, RED);
    }

    /* 锟斤拷示X/Y锟斤拷锟斤拷谋锟斤拷锟斤拷锟斤拷锟� */
    lcd_fill(40, 160 + (i * 20), lcddev.width - 1, 16, WHITE);  /* 锟斤拷锟街帮拷锟絧x,py锟斤拷示 */
    sprintf(sbuf, "px:%0.2f", px);
    sbuf[7] = 0; /* 锟斤拷锟接斤拷锟斤拷锟斤拷 */
    lcd_show_string(40, 160 + (i * 20), lcddev.width, lcddev.height, 16, sbuf, RED);
    sprintf(sbuf, "py:%0.2f", py);
    sbuf[7] = 0; /* 锟斤拷锟接斤拷锟斤拷锟斤拷 */
    lcd_show_string(40 + 80, 160 + (i * 20), lcddev.width, lcddev.height, 16, sbuf, RED);
}

/**
 * @brief       锟斤拷锟斤拷锟斤拷校准锟斤拷锟斤拷
 *   @note      使锟斤拷锟斤拷锟叫Ｗ硷拷锟�(锟斤拷锟斤拷原锟斤拷锟斤拷俣锟�)
 *              锟斤拷锟斤拷锟斤拷锟矫碉拷x锟斤拷/y锟斤拷锟斤拷锟斤拷锟斤拷锟絰fac/yfac锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷值(xc,yc)锟斤拷4锟斤拷锟斤拷锟斤拷
 *              锟斤拷锟角规定: 锟斤拷锟斤拷锟斤拷锟疥即AD锟缴硷拷锟斤拷锟斤拷锟斤拷锟斤拷值,锟斤拷围锟斤拷0~4095.
 *                        锟竭硷拷锟斤拷锟疥即LCD锟斤拷幕锟斤拷锟斤拷锟斤拷, 锟斤拷围为LCD锟斤拷幕锟侥分憋拷锟斤拷.
 *
 * @param       锟斤拷
 * @retval      锟斤拷
 */
void tp_adjust(void)
{
    uint16_t pxy[5][2];     /* 锟斤拷锟斤拷锟斤拷锟疥缓锟斤拷值 */
    uint8_t  cnt = 0;
    short s1, s2, s3, s4;   /* 4锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷值 */
    double px, py;          /* X,Y锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟�,锟斤拷锟斤拷锟叫讹拷锟角凤拷校准锟缴癸拷 */
    uint16_t outtime = 0;
    cnt = 0;

    lcd_clear(WHITE);       /* 锟斤拷锟斤拷 */
    lcd_show_string(40, 40, 160, 100, 16, TP_REMIND_MSG_TBL, RED); /* 锟斤拷示锟斤拷示锟斤拷息 */
    tp_draw_touch_point(20, 20, RED);   /* 锟斤拷锟斤拷1 */
    tp_dev.sta = 0;         /* 锟斤拷锟斤拷锟斤拷锟斤拷锟脚猴拷 */

    while (1)               /* 锟斤拷锟斤拷锟斤拷锟�10锟斤拷锟斤拷没锟叫帮拷锟斤拷,锟斤拷锟皆讹拷锟剿筹拷 */
    {
        tp_dev.scan(1);     /* 扫锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 */

        if ((tp_dev.sta & 0xc000) == TP_CATH_PRES)   /* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷一锟斤拷(锟斤拷时锟斤拷锟斤拷锟缴匡拷锟斤拷.) */
        {
            outtime = 0;
            tp_dev.sta &= ~TP_CATH_PRES;    /* 锟斤拷前锟斤拷锟斤拷丫锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟�. */

            pxy[cnt][0] = tp_dev.x[0];      /* 锟斤拷锟斤拷X锟斤拷锟斤拷锟斤拷锟斤拷 */
            pxy[cnt][1] = tp_dev.y[0];      /* 锟斤拷锟斤拷Y锟斤拷锟斤拷锟斤拷锟斤拷 */
            cnt++;

            switch (cnt)
            {
                case 1:
                    tp_draw_touch_point(20, 20, WHITE);                 /* 锟斤拷锟斤拷锟�1 */
                    tp_draw_touch_point(lcddev.width - 20, 20, RED);    /* 锟斤拷锟斤拷2 */
                    break;

                case 2:
                    tp_draw_touch_point(lcddev.width - 20, 20, WHITE);  /* 锟斤拷锟斤拷锟�2 */
                    tp_draw_touch_point(20, lcddev.height - 20, RED);   /* 锟斤拷锟斤拷3 */
                    break;

                case 3:
                    tp_draw_touch_point(20, lcddev.height - 20, WHITE); /* 锟斤拷锟斤拷锟�3 */
                    tp_draw_touch_point(lcddev.width - 20, lcddev.height - 20, RED);    /* 锟斤拷锟斤拷4 */
                    break;

                case 4:
                    lcd_clear(WHITE);   /* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟�, 直锟斤拷锟斤拷锟斤拷 */
                    tp_draw_touch_point(lcddev.width / 2, lcddev.height / 2, RED);  /* 锟斤拷锟斤拷5 */
                    break;

                case 5:     /* 全锟斤拷5锟斤拷锟斤拷锟窖撅拷锟矫碉拷 */
                    s1 = pxy[1][0] - pxy[0][0]; /* 锟斤拷2锟斤拷锟斤拷偷锟�1锟斤拷锟斤拷锟絏锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟街�(AD值) */
                    s3 = pxy[3][0] - pxy[2][0]; /* 锟斤拷4锟斤拷锟斤拷偷锟�3锟斤拷锟斤拷锟絏锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟街�(AD值) */
                    s2 = pxy[3][1] - pxy[1][1]; /* 锟斤拷4锟斤拷锟斤拷偷锟�2锟斤拷锟斤拷锟結锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟街�(AD值) */
                    s4 = pxy[2][1] - pxy[0][1]; /* 锟斤拷3锟斤拷锟斤拷偷锟�1锟斤拷锟斤拷锟結锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟街�(AD值) */

                    px = (double)s1 / s3;       /* X锟斤拷锟斤拷锟斤拷锟斤拷锟� */
                    py = (double)s2 / s4;       /* Y锟斤拷锟斤拷锟斤拷锟斤拷锟� */

                    if (px < 0) px = -px;       /* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 */
                    if (py < 0) py = -py;       /* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 */

                    if (px < 0.95 || px > 1.05 || py < 0.95 || py > 1.05 ||     /* 锟斤拷锟斤拷锟斤拷锟较革拷 */
                            abs(s1) > 4095 || abs(s2) > 4095 || abs(s3) > 4095 || abs(s4) > 4095 ||  /* 锟斤拷值锟斤拷锟较革拷, 锟斤拷锟斤拷锟斤拷锟疥范围 */
                            abs(s1) == 0 || abs(s2) == 0 || abs(s3) == 0 || abs(s4) == 0             /* 锟斤拷值锟斤拷锟较革拷, 锟斤拷锟斤拷0 */
                       )
                    {
                        cnt = 0;
                        tp_draw_touch_point(lcddev.width / 2, lcddev.height / 2, WHITE); /* 锟斤拷锟斤拷锟�5 */
                        tp_draw_touch_point(20, 20, RED);   /* 锟斤拷锟铰伙拷锟斤拷1 */
                        tp_adjust_info_show(pxy, px, py);   /* 锟斤拷示锟斤拷前锟斤拷息,锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 */
                        continue;
                    }

                    tp_dev.xfac = (float)(s1 + s3) / (2 * (lcddev.width - 40));
                    tp_dev.yfac = (float)(s2 + s4) / (2 * (lcddev.height - 40));

                    tp_dev.xc = pxy[4][0];      /* X锟斤拷,锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 */
                    tp_dev.yc = pxy[4][1];      /* Y锟斤拷,锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 */

                    lcd_clear(WHITE);   /* 锟斤拷锟斤拷 */
                    lcd_show_string(35, 110, lcddev.width, lcddev.height, 16, "Touch Screen Adjust OK!", BLUE); /* 校锟斤拷锟斤拷锟� */
                    delay_ms(1000);
                    tp_save_adjust_data();

                    lcd_clear(WHITE);   /* 锟斤拷锟斤拷 */
                    return; /* 校锟斤拷锟斤拷锟� */
            }
        }

        delay_ms(10);
        outtime++;

        if (outtime > 1000)
        {
            tp_get_adjust_data();
            break;
        }
    }

}

/**
 * @brief       锟斤拷锟斤拷锟斤拷锟斤拷始锟斤拷
 * @param       锟斤拷
 * @retval      0,没锟叫斤拷锟斤拷校准
 *              1,锟斤拷锟叫癸拷校准
 */
uint8_t tp_init(void)
{
    GPIO_InitTypeDef gpio_init_struct;
    
    tp_dev.touchtype = 0;                   /* 默锟斤拷锟斤拷锟斤拷(锟斤拷锟斤拷锟斤拷 & 锟斤拷锟斤拷) */
    tp_dev.touchtype |= lcddev.dir & 0X01;  /* 锟斤拷锟斤拷LCD锟叫讹拷锟角猴拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 */

    if (lcddev.id == 0x7796)    /* 3.5锟斤拷锟斤拷锟斤拷锟斤拷锟街ｏ拷一锟斤拷锟斤拷幕ID为0x5510锟斤拷锟斤拷锟借触锟斤拷锟斤拷锟斤拷一锟斤拷锟斤拷幕ID为0x7796锟斤拷GT锟酵号的碉拷锟捷达拷锟斤拷锟斤拷 */
    {
        if (gt9xxx_init() == 0) /* 锟斤拷始锟斤拷GT系锟叫达拷锟斤拷锟斤拷锟缴癸拷,锟斤拷锟斤拷前3.5锟斤拷锟斤拷为锟斤拷锟捷达拷锟斤拷锟斤拷 */
        {
            tp_dev.scan = gt9xxx_scan;  /* 扫锟借函锟斤拷指锟斤拷GT9147锟斤拷锟斤拷锟斤拷扫锟斤拷 */
            tp_dev.touchtype |= 0X80;   /* 锟斤拷锟斤拷锟斤拷 */
            return 0;
        }
    }

    if (lcddev.id == 0X5510 || lcddev.id == 0X9806 || lcddev.id == 0X4342 || lcddev.id == 0X4384 || lcddev.id == 0X1018)  /* 锟斤拷锟捷达拷锟斤拷锟斤拷,4.3锟斤拷/10.1锟斤拷锟斤拷 */
    {
        gt9xxx_init();
        tp_dev.scan = gt9xxx_scan;  /* 扫锟借函锟斤拷指锟斤拷GT9147锟斤拷锟斤拷锟斤拷扫锟斤拷 */
        tp_dev.touchtype |= 0X80;   /* 锟斤拷锟斤拷锟斤拷 */
        return 0;
    }
    else if (lcddev.id == 0X1963 || lcddev.id == 0X7084 || lcddev.id == 0X7016)     /* SSD1963 7锟斤拷锟斤拷锟斤拷锟斤拷 7锟斤拷800*480/1024*600 RGB锟斤拷 */
    {
        if (!ft5206_init())             /* 锟斤拷锟斤拷IC锟斤拷FT系锟叫的撅拷执锟斤拷ft5206_init锟斤拷锟斤拷锟皆硷拷使锟斤拷ft5206_scan扫锟借函锟斤拷 */
        {
            tp_dev.scan = ft5206_scan;  /* 扫锟借函锟斤拷指锟斤拷FT5206锟斤拷锟斤拷锟斤拷扫锟斤拷 */
        }
        else                            /* 锟斤拷锟斤拷IC锟斤拷GT系锟叫的撅拷执锟斤拷gt9xxx_init锟斤拷锟斤拷锟皆硷拷使锟斤拷gt9xxx_scan扫锟借函锟斤拷 */
        {
            gt9xxx_init();
            tp_dev.scan = gt9xxx_scan;  /* 扫锟借函锟斤拷指锟斤拷GT9147锟斤拷锟斤拷锟斤拷扫锟斤拷 */
        }
        tp_dev.touchtype |= 0X80;       /* 锟斤拷锟斤拷锟斤拷 */
        return 0;
    }
    else
    {
        T_PEN_GPIO_CLK_ENABLE();    /* T_PEN锟斤拷时锟斤拷使锟斤拷 */
        T_CS_GPIO_CLK_ENABLE();     /* T_CS锟斤拷时锟斤拷使锟斤拷 */
        T_MISO_GPIO_CLK_ENABLE();   /* T_MISO锟斤拷时锟斤拷使锟斤拷 */
        T_MOSI_GPIO_CLK_ENABLE();   /* T_MOSI锟斤拷时锟斤拷使锟斤拷 */
        T_CLK_GPIO_CLK_ENABLE();    /* T_CLK锟斤拷时锟斤拷使锟斤拷 */

        gpio_init_struct.Pin = T_PEN_GPIO_PIN;
        gpio_init_struct.Mode = GPIO_MODE_INPUT;                 /* 锟斤拷锟斤拷 */
        gpio_init_struct.Pull = GPIO_PULLUP;                     /* 锟斤拷锟斤拷 */
        gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;      /* 锟斤拷锟斤拷 */
        HAL_GPIO_Init(T_PEN_GPIO_PORT, &gpio_init_struct);       /* 锟斤拷始锟斤拷T_PEN锟斤拷锟斤拷 */

        gpio_init_struct.Pin = T_MISO_GPIO_PIN;
        HAL_GPIO_Init(T_MISO_GPIO_PORT, &gpio_init_struct);      /* 锟斤拷始锟斤拷T_MISO锟斤拷锟斤拷 */

        gpio_init_struct.Pin = T_MOSI_GPIO_PIN;
        gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;             /* 锟斤拷锟斤拷锟斤拷锟� */
        gpio_init_struct.Pull = GPIO_PULLUP;                     /* 锟斤拷锟斤拷 */
        gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;      /* 锟斤拷锟斤拷 */
        HAL_GPIO_Init(T_MOSI_GPIO_PORT, &gpio_init_struct);      /* 锟斤拷始锟斤拷T_MOSI锟斤拷锟斤拷 */

        gpio_init_struct.Pin = T_CLK_GPIO_PIN;
        HAL_GPIO_Init(T_CLK_GPIO_PORT, &gpio_init_struct);       /* 锟斤拷始锟斤拷T_CLK锟斤拷锟斤拷 */

        gpio_init_struct.Pin = T_CS_GPIO_PIN;
        HAL_GPIO_Init(T_CS_GPIO_PORT, &gpio_init_struct);        /* 锟斤拷始锟斤拷T_CS锟斤拷锟斤拷 */

        tp_read_xy(&tp_dev.x[0], &tp_dev.y[0]); /* 锟斤拷一锟轿讹拷取锟斤拷始锟斤拷 */
        at24cxx_init();         /* 锟斤拷始锟斤拷24CXX */

        if (tp_get_adjust_data())
        {
            return 0;           /* 锟窖撅拷校准 */
        }
        else                    /* 未校准? */
        {
            lcd_clear(WHITE);   /* 锟斤拷锟斤拷 */
            tp_adjust();        /* 锟斤拷幕校准 */
            tp_save_adjust_data();
        }

        tp_get_adjust_data();
    }

    return 1;
}









