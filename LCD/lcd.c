/**
 ****************************************************************************************************
 * @file        lcd.c
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

#include "stdlib.h"
#include "lcd.h"
#include "lcdfont.h"
#include "delay.h"
#include <stdio.h>

/* lcd_ex.c registers init code, included directly by lcd.c */
#include "lcd_ex.c"


SRAM_HandleTypeDef g_sram_handle;   /* SRAM锟斤拷锟�(锟斤拷锟节匡拷锟斤拷LCD) */

/* LCD锟侥伙拷锟斤拷锟斤拷色锟酵憋拷锟斤拷色 */
uint32_t g_point_color = 0xF800;    /* 锟斤拷锟斤拷锟斤拷色 */
uint32_t g_back_color  = 0xFFFF;    /* 锟斤拷锟斤拷色 */

/* 锟斤拷锟斤拷LCD锟斤拷要锟斤拷锟斤拷 */
_lcd_dev lcddev;

/**
 * @brief       LCD写锟斤拷锟斤拷
 * @param       data: 要写锟斤拷锟斤拷锟斤拷锟�
 * @retval      锟斤拷
 */
void lcd_wr_data(volatile uint16_t data)
{
    data = data;            /* 使锟斤拷-O2锟脚伙拷锟斤拷时锟斤拷,锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷时 */
    LCD->LCD_RAM = data;
}

/**
 * @brief       LCD写锟侥达拷锟斤拷锟斤拷锟�/锟斤拷址锟斤拷锟斤拷
 * @param       regno: 锟侥达拷锟斤拷锟斤拷锟�/锟斤拷址
 * @retval      锟斤拷
 */
void lcd_wr_regno(volatile uint16_t regno)
{
    regno = regno;          /* 使锟斤拷-O2锟脚伙拷锟斤拷时锟斤拷,锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷时 */
    LCD->LCD_REG = regno;   /* 写锟斤拷要写锟侥寄达拷锟斤拷锟斤拷锟� */
}

/**
 * @brief       LCD写锟侥达拷锟斤拷
 * @param       regno:锟侥达拷锟斤拷锟斤拷锟�/锟斤拷址
 * @param       data:要写锟斤拷锟斤拷锟斤拷锟�
 * @retval      锟斤拷
 */
void lcd_write_reg(uint16_t regno, uint16_t data)
{
    LCD->LCD_REG = regno;   /* 写锟斤拷要写锟侥寄达拷锟斤拷锟斤拷锟� */
    LCD->LCD_RAM = data;    /* 写锟斤拷锟斤拷锟斤拷 */
}

/**
 * @brief       LCD锟斤拷时锟斤拷锟斤拷,锟斤拷锟斤拷锟节诧拷锟斤拷锟斤拷mdk -O1时锟斤拷锟脚伙拷时锟斤拷要锟斤拷锟矫的地凤拷
 * @param       t:锟斤拷时锟斤拷锟斤拷值
 * @retval      锟斤拷
 */
static void lcd_opt_delay(uint32_t i)
{
    while (i--); /* 使锟斤拷AC6时锟斤拷循锟斤拷锟斤拷锟杰憋拷锟脚伙拷,锟斤拷使锟斤拷while(1) __asm volatile(""); */
}

/**
 * @brief       LCD锟斤拷锟斤拷锟斤拷
 * @param       锟斤拷
 * @retval      锟斤拷取锟斤拷锟斤拷锟斤拷锟斤拷
 */
static uint16_t lcd_rd_data(void)
{
    volatile uint16_t ram;  /* 锟斤拷止锟斤拷锟脚伙拷 */
    lcd_opt_delay(2);
    ram = LCD->LCD_RAM;
    return ram;
}

/**
 * @brief       准锟斤拷写GRAM
 * @param       锟斤拷
 * @retval      锟斤拷
 */
void lcd_write_ram_prepare(void)
{
    LCD->LCD_REG = lcddev.wramcmd;
}

/**
 * @brief       锟斤拷取锟斤拷某锟斤拷锟斤拷锟缴�
 * @param       x,y:锟斤拷锟斤拷
 * @retval      锟剿碉拷锟斤拷锟缴�(32位锟斤拷色,锟斤拷锟斤拷锟斤拷锟絃TDC)
 */
uint32_t lcd_read_point(uint16_t x, uint16_t y)
{
    uint16_t r = 0, g = 0, b = 0;

    if (x >= lcddev.width || y >= lcddev.height)
    {
        return 0;   /* 锟斤拷锟斤拷锟剿凤拷围,直锟接凤拷锟斤拷 */
    }

    lcd_set_cursor(x, y);       /* 锟斤拷锟斤拷锟斤拷锟斤拷 */

    if (lcddev.id == 0x5510)
    {
        lcd_wr_regno(0x2E00);   /* 5510 锟斤拷锟酵讹拷GRAM指锟斤拷 */
    }
    else
    {
        lcd_wr_regno(0x2E);     /* 9341/5310/1963/7789/7796/9806 锟饺凤拷锟酵讹拷GRAM指锟斤拷 */
    }


    r = lcd_rd_data();          /* 锟劫讹拷(dummy read) */

    if (lcddev.id == 0x1963)
    {
        return r;   /* 1963直锟接讹拷锟酵匡拷锟斤拷 */
    }

    r = lcd_rd_data();          /* 实锟斤拷锟斤拷锟斤拷锟斤拷色 */
    
    if (lcddev.id == 0x7796)    /* 7796 一锟轿讹拷取一锟斤拷锟斤拷锟斤拷值 */
    {
        return r;
    }
    
    /* 9341/5310/5510/7789/9806要锟斤拷2锟轿讹拷锟斤拷 */
    b = lcd_rd_data();
    g = r & 0xFF;               /* 锟斤拷锟斤拷9341/5310/5510/7789/9806,锟斤拷一锟轿讹拷取锟斤拷锟斤拷RG锟斤拷值,R锟斤拷前,G锟节猴拷,锟斤拷占8位 */
    g <<= 8;
    
    return (((r >> 11) << 11) | ((g >> 10) << 5) | (b >> 11));  /* ILI9341/NT35310/NT35510/ST7789/ILI9806锟斤拷要锟斤拷式转锟斤拷一锟斤拷 */
}

/**
 * @brief       LCD锟斤拷锟斤拷锟斤拷示
 * @param       锟斤拷
 * @retval      锟斤拷
 */
void lcd_display_on(void)
{
    if (lcddev.id == 0x5510)
    {
        lcd_wr_regno(0x2900);   /* 锟斤拷锟斤拷锟斤拷示 */
    }
    else                        /* 9341/5310/1963/7789/7796/9806 锟饺凤拷锟酵匡拷锟斤拷锟斤拷示指锟斤拷 */
    {
        lcd_wr_regno(0x29);     /* 锟斤拷锟斤拷锟斤拷示 */
    }
}

/**
 * @brief       LCD锟截憋拷锟斤拷示
 * @param       锟斤拷
 * @retval      锟斤拷
 */
void lcd_display_off(void)
{
    if (lcddev.id == 0x5510)
    {
        lcd_wr_regno(0x2800);   /* 锟截憋拷锟斤拷示 */
    }
    else                        /* 9341/5310/1963/7789/7796/9806 锟饺凤拷锟酵关憋拷锟斤拷示指锟斤拷 */
    {
        lcd_wr_regno(0x28);     /* 锟截憋拷锟斤拷示 */
    }
}

/**
 * @brief       锟斤拷锟矫癸拷锟轿伙拷锟�(锟斤拷RGB锟斤拷锟斤拷效)
 * @param       x,y: 锟斤拷锟斤拷
 * @retval      锟斤拷
 */
void lcd_set_cursor(uint16_t x, uint16_t y)
{
    if (lcddev.id == 0x1963)
    {
        if (lcddev.dir == 0)    /* 锟斤拷锟斤拷模式, x锟斤拷锟斤拷锟斤拷要锟戒换 */
        {
            x = lcddev.width - 1 - x;
            lcd_wr_regno(lcddev.setxcmd);
            lcd_wr_data(0);
            lcd_wr_data(0);
            lcd_wr_data(x >> 8);
            lcd_wr_data(x & 0xFF);
        }
        else                    /* 锟斤拷锟斤拷模式 */
        {
            lcd_wr_regno(lcddev.setxcmd);
            lcd_wr_data(x >> 8);
            lcd_wr_data(x & 0xFF);
            lcd_wr_data((lcddev.width - 1) >> 8);
            lcd_wr_data((lcddev.width - 1) & 0xFF);
        }

        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(y >> 8);
        lcd_wr_data(y & 0xFF);
        lcd_wr_data((lcddev.height - 1) >> 8);
        lcd_wr_data((lcddev.height - 1) & 0xFF);

    }
    else if (lcddev.id == 0x5510)
    {
        lcd_wr_regno(lcddev.setxcmd);
        lcd_wr_data(x >> 8);
        lcd_wr_regno(lcddev.setxcmd + 1);
        lcd_wr_data(x & 0xFF);
        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(y >> 8);
        lcd_wr_regno(lcddev.setycmd + 1);
        lcd_wr_data(y & 0xFF);
    }
    else    /* 9341/5310/7789/7796/9806 锟斤拷 锟斤拷锟斤拷锟斤拷锟斤拷 */
    {
        lcd_wr_regno(lcddev.setxcmd);
        lcd_wr_data(x >> 8);
        lcd_wr_data(x & 0xFF);
        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(y >> 8);
        lcd_wr_data(y & 0xFF);
    }
}

/**
 * @brief       锟斤拷锟斤拷LCD锟斤拷锟皆讹拷扫锟借方锟斤拷(锟斤拷RGB锟斤拷锟斤拷效)
 *   @note
 *              9341/5310/5510/1963/7789/7796/9806锟斤拷IC锟窖撅拷实锟绞诧拷锟斤拷
 *              注锟斤拷:锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟杰伙拷锟杰碉拷锟剿猴拷锟斤拷锟斤拷锟矫碉拷影锟斤拷(锟斤拷锟斤拷锟斤拷9341),
 *              锟斤拷锟斤拷,一锟斤拷锟斤拷锟斤拷为L2R_U2D锟斤拷锟斤拷,锟斤拷锟斤拷锟斤拷锟轿拷锟斤拷锟缴拷璺绞�,锟斤拷锟杰碉拷锟斤拷锟斤拷示锟斤拷锟斤拷锟斤拷.
 *
 * @param       dir:0~7,锟斤拷锟斤拷8锟斤拷锟斤拷锟斤拷(锟斤拷锟藉定锟斤拷锟絣cd.h)
 * @retval      锟斤拷
 */
void lcd_scan_dir(uint8_t dir)
{
    uint16_t regval = 0;
    uint16_t dirreg = 0;
    uint16_t temp;

    /* 锟斤拷锟斤拷时锟斤拷锟斤拷1963锟斤拷锟侥憋拷扫锟借方锟斤拷锟斤拷锟斤拷时1963锟侥变方锟斤拷(锟斤拷锟斤拷锟斤拷锟斤拷锟�1963锟斤拷锟斤拷锟解处锟斤拷,锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷IC锟斤拷效) */
    if ((lcddev.dir == 1 && lcddev.id != 0x1963) || (lcddev.dir == 0 && lcddev.id == 0x1963))
    {
        switch (dir)   /* 锟斤拷锟斤拷转锟斤拷 */
        {
            case 0:
                dir = 6;
                break;

            case 1:
                dir = 7;
                break;

            case 2:
                dir = 4;
                break;

            case 3:
                dir = 5;
                break;

            case 4:
                dir = 1;
                break;

            case 5:
                dir = 0;
                break;

            case 6:
                dir = 3;
                break;

            case 7:
                dir = 2;
                break;
        }
    }


    /* 锟斤拷锟斤拷扫锟借方式 锟斤拷锟斤拷 0x36/0x3600 锟侥达拷锟斤拷 bit 5,6,7 位锟斤拷值 */
    switch (dir)
    {
        case L2R_U2D:   /* 锟斤拷锟斤拷锟斤拷,锟斤拷锟较碉拷锟斤拷 */
            regval |= (0 << 7) | (0 << 6) | (0 << 5);
            break;

        case L2R_D2U:   /* 锟斤拷锟斤拷锟斤拷,锟斤拷锟铰碉拷锟斤拷 */
            regval |= (1 << 7) | (0 << 6) | (0 << 5);
            break;

        case R2L_U2D:   /* 锟斤拷锟揭碉拷锟斤拷,锟斤拷锟较碉拷锟斤拷 */
            regval |= (0 << 7) | (1 << 6) | (0 << 5);
            break;

        case R2L_D2U:   /* 锟斤拷锟揭碉拷锟斤拷,锟斤拷锟铰碉拷锟斤拷 */
            regval |= (1 << 7) | (1 << 6) | (0 << 5);
            break;

        case U2D_L2R:   /* 锟斤拷锟较碉拷锟斤拷,锟斤拷锟斤拷锟斤拷 */
            regval |= (0 << 7) | (0 << 6) | (1 << 5);
            break;

        case U2D_R2L:   /* 锟斤拷锟较碉拷锟斤拷,锟斤拷锟揭碉拷锟斤拷 */
            regval |= (0 << 7) | (1 << 6) | (1 << 5);
            break;

        case D2U_L2R:   /* 锟斤拷锟铰碉拷锟斤拷,锟斤拷锟斤拷锟斤拷 */
            regval |= (1 << 7) | (0 << 6) | (1 << 5);
            break;

        case D2U_R2L:   /* 锟斤拷锟铰碉拷锟斤拷,锟斤拷锟揭碉拷锟斤拷 */
            regval |= (1 << 7) | (1 << 6) | (1 << 5);
            break;
    }

    dirreg = 0x36;  /* 锟皆撅拷锟襟部凤拷锟斤拷锟斤拷IC, 锟斤拷0x36锟侥达拷锟斤拷锟斤拷锟斤拷 */

    if (lcddev.id == 0x5510)
    {
        dirreg = 0x3600;    /* 锟斤拷锟斤拷5510, 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷ic锟侥寄达拷锟斤拷锟叫诧拷锟斤拷 */
    }

     /* 9341 & 7789 & 7796 要锟斤拷锟斤拷BGR位 */
    if (lcddev.id == 0x9341 || lcddev.id == 0x7789 || lcddev.id == 0x7796)
    {
        regval |= 0x08;
    }

    lcd_write_reg(dirreg, regval);

    if (lcddev.id != 0x1963)                    /* 1963锟斤拷锟斤拷锟斤拷锟疥处锟斤拷 */
    {
        if (regval & 0x20)
        {
            if (lcddev.width < lcddev.height)   /* 锟斤拷锟斤拷X,Y */
            {
                temp = lcddev.width;
                lcddev.width = lcddev.height;
                lcddev.height = temp;
            }
        }
        else
        {
            if (lcddev.width > lcddev.height)   /* 锟斤拷锟斤拷X,Y */
            {
                temp = lcddev.width;
                lcddev.width = lcddev.height;
                lcddev.height = temp;
            }
        }
    }

    /* 锟斤拷锟斤拷锟斤拷示锟斤拷锟斤拷(锟斤拷锟斤拷)锟斤拷小 */
    if (lcddev.id == 0x5510)
    {
        lcd_wr_regno(lcddev.setxcmd);
        lcd_wr_data(0);
        lcd_wr_regno(lcddev.setxcmd + 1);
        lcd_wr_data(0);
        lcd_wr_regno(lcddev.setxcmd + 2);
        lcd_wr_data((lcddev.width - 1) >> 8);
        lcd_wr_regno(lcddev.setxcmd + 3);
        lcd_wr_data((lcddev.width - 1) & 0xFF);
        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(0);
        lcd_wr_regno(lcddev.setycmd + 1);
        lcd_wr_data(0);
        lcd_wr_regno(lcddev.setycmd + 2);
        lcd_wr_data((lcddev.height - 1) >> 8);
        lcd_wr_regno(lcddev.setycmd + 3);
        lcd_wr_data((lcddev.height - 1) & 0xFF);
    }
    else
    {
        lcd_wr_regno(lcddev.setxcmd);
        lcd_wr_data(0);
        lcd_wr_data(0);
        lcd_wr_data((lcddev.width - 1) >> 8);
        lcd_wr_data((lcddev.width - 1) & 0xFF);
        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(0);
        lcd_wr_data(0);
        lcd_wr_data((lcddev.height - 1) >> 8);
        lcd_wr_data((lcddev.height - 1) & 0xFF);
    }
}

/**
 * @brief       锟斤拷锟斤拷
 * @param       x,y: 锟斤拷锟斤拷
 * @param       color: 锟斤拷锟斤拷锟缴�(32位锟斤拷色,锟斤拷锟斤拷锟斤拷锟絃TDC)
 * @retval      锟斤拷
 */
void lcd_draw_point(uint16_t x, uint16_t y, uint32_t color)
{
    lcd_set_cursor(x, y);       /* 锟斤拷锟矫癸拷锟轿伙拷锟� */
    lcd_write_ram_prepare();    /* 锟斤拷始写锟斤拷GRAM */
    LCD->LCD_RAM = color;
}

/**
 * @brief       SSD1963锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟矫猴拷锟斤拷
 * @param       pwm: 锟斤拷锟斤拷燃锟�,0~100.越锟斤拷越锟斤拷.
 * @retval      锟斤拷
 */
void lcd_ssd_backlight_set(uint8_t pwm)
{
    lcd_wr_regno(0xBE);         /* 锟斤拷锟斤拷PWM锟斤拷锟� */
    lcd_wr_data(0x05);          /* 1锟斤拷锟斤拷PWM频锟斤拷 */
    lcd_wr_data(pwm * 2.55);    /* 2锟斤拷锟斤拷PWM占锟秸憋拷 */
    lcd_wr_data(0x01);          /* 3锟斤拷锟斤拷C */
    lcd_wr_data(0xFF);          /* 4锟斤拷锟斤拷D */
    lcd_wr_data(0x00);          /* 5锟斤拷锟斤拷E */
    lcd_wr_data(0x00);          /* 6锟斤拷锟斤拷F */
}

/**
 * @brief       锟斤拷锟斤拷LCD锟斤拷示锟斤拷锟斤拷
 * @param       dir:0,锟斤拷锟斤拷; 1,锟斤拷锟斤拷
 * @retval      锟斤拷
 */
void lcd_display_dir(uint8_t dir)
{
    lcddev.dir = dir;   /* 锟斤拷锟斤拷/锟斤拷锟斤拷 */

    if (dir == 0)       /* 锟斤拷锟斤拷 */
    {
        lcddev.width = 240;
        lcddev.height = 320;

        if (lcddev.id == 0x5510)
        {
            lcddev.wramcmd = 0x2C00;
            lcddev.setxcmd = 0x2A00;
            lcddev.setycmd = 0x2B00;
            lcddev.width = 480;
            lcddev.height = 800;
        }
        else if (lcddev.id == 0x1963)
        {
            lcddev.wramcmd = 0x2C;  /* 锟斤拷锟斤拷写锟斤拷GRAM锟斤拷指锟斤拷 */
            lcddev.setxcmd = 0x2B;  /* 锟斤拷锟斤拷写X锟斤拷锟斤拷指锟斤拷 */
            lcddev.setycmd = 0x2A;  /* 锟斤拷锟斤拷写Y锟斤拷锟斤拷指锟斤拷 */
            lcddev.width = 480;     /* 锟斤拷锟矫匡拷锟斤拷480 */
            lcddev.height = 800;    /* 锟斤拷锟矫高讹拷800 */
        }
        else   /* 锟斤拷锟斤拷IC, 锟斤拷锟斤拷: 9341/5310/7789/7796/9806锟斤拷IC */
        {
            lcddev.wramcmd = 0x2C;
            lcddev.setxcmd = 0x2A;
            lcddev.setycmd = 0x2B;
        }

        if (lcddev.id == 0x5310 || lcddev.id == 0x7796)     /* 锟斤拷锟斤拷锟�5310/7796 锟斤拷锟绞撅拷锟� 320*480锟街憋拷锟斤拷 */
        {
            lcddev.width = 320;
            lcddev.height = 480;
        }
        
        if (lcddev.id == 0X9806)    /* 锟斤拷锟斤拷锟�9806 锟斤拷锟绞撅拷锟� 480*800 锟街憋拷锟斤拷 */
        {
            lcddev.width = 480;
            lcddev.height = 800;
        }  
    }
    else        /* 锟斤拷锟斤拷 */
    {
        lcddev.width = 320;         /* 默锟较匡拷锟斤拷 */
        lcddev.height = 240;        /* 默锟较高讹拷 */

        if (lcddev.id == 0x5510)
        {
            lcddev.wramcmd = 0x2C00;
            lcddev.setxcmd = 0x2A00;
            lcddev.setycmd = 0x2B00;
            lcddev.width = 800;
            lcddev.height = 480;
        }
        else if (lcddev.id == 0x1963 || lcddev.id == 0x9806)
        {
            lcddev.wramcmd = 0x2C;  /* 锟斤拷锟斤拷写锟斤拷GRAM锟斤拷指锟斤拷 */
            lcddev.setxcmd = 0x2A;  /* 锟斤拷锟斤拷写X锟斤拷锟斤拷指锟斤拷 */
            lcddev.setycmd = 0x2B;  /* 锟斤拷锟斤拷写Y锟斤拷锟斤拷指锟斤拷 */
            lcddev.width = 800;     /* 锟斤拷锟矫匡拷锟斤拷800 */
            lcddev.height = 480;    /* 锟斤拷锟矫高讹拷480 */
        }
        else   /* 锟斤拷锟斤拷IC, 锟斤拷锟斤拷:9341/5310/7789/7796锟斤拷IC */
        {
            lcddev.wramcmd = 0x2C;
            lcddev.setxcmd = 0x2A;
            lcddev.setycmd = 0x2B;
        }

        if (lcddev.id == 0x5310 || lcddev.id == 0x7796)     /* 锟斤拷锟斤拷锟�5310/7796 锟斤拷锟绞撅拷锟� 320*480锟街憋拷锟斤拷 */
        {
            lcddev.width = 480;
            lcddev.height = 320;
        }
    }

    lcd_scan_dir(DFT_SCAN_DIR);     /* 默锟斤拷扫锟借方锟斤拷 */
}

/**
 * @brief       锟斤拷锟矫达拷锟斤拷(锟斤拷RGB锟斤拷锟斤拷效), 锟斤拷锟皆讹拷锟斤拷锟矫伙拷锟斤拷锟斤拷锟疥到锟斤拷锟斤拷锟斤拷锟较斤拷(sx,sy).
 * @param       sx,sy:锟斤拷锟斤拷锟斤拷始锟斤拷锟斤拷(锟斤拷锟较斤拷)
 * @param       width,height:锟斤拷锟节匡拷锟饺和高讹拷,锟斤拷锟斤拷锟斤拷锟�0!!
 *   @note      锟斤拷锟斤拷锟叫�:width*height.
 *
 * @retval      锟斤拷
 */
void lcd_set_window(uint16_t sx, uint16_t sy, uint16_t width, uint16_t height)
{
    uint16_t twidth, theight;
    twidth = sx + width - 1;
    theight = sy + height - 1;

   
   if (lcddev.id == 0x1963 && lcddev.dir != 1)     /* 1963锟斤拷锟斤拷锟斤拷锟解处锟斤拷 */
    {
        sx = lcddev.width - width - sx;
        height = sy + height - 1;
        lcd_wr_regno(lcddev.setxcmd);
        lcd_wr_data(sx >> 8);
        lcd_wr_data(sx & 0xFF);
        lcd_wr_data((sx + width - 1) >> 8);
        lcd_wr_data((sx + width - 1) & 0xFF);
        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(sy >> 8);
        lcd_wr_data(sy & 0xFF);
        lcd_wr_data(height >> 8);
        lcd_wr_data(height & 0xFF);
    }
    else if (lcddev.id == 0x5510)
    {
        lcd_wr_regno(lcddev.setxcmd);
        lcd_wr_data(sx >> 8);
        lcd_wr_regno(lcddev.setxcmd + 1);
        lcd_wr_data(sx & 0xFF);
        lcd_wr_regno(lcddev.setxcmd + 2);
        lcd_wr_data(twidth >> 8);
        lcd_wr_regno(lcddev.setxcmd + 3);
        lcd_wr_data(twidth & 0xFF);
        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(sy >> 8);
        lcd_wr_regno(lcddev.setycmd + 1);
        lcd_wr_data(sy & 0xFF);
        lcd_wr_regno(lcddev.setycmd + 2);
        lcd_wr_data(theight >> 8);
        lcd_wr_regno(lcddev.setycmd + 3);
        lcd_wr_data(theight & 0xFF);
    }
    else    /* 9341/5310/7789/1963/7796/9806锟斤拷锟斤拷 锟斤拷 锟斤拷锟矫达拷锟斤拷 */
    {
        lcd_wr_regno(lcddev.setxcmd);
        lcd_wr_data(sx >> 8);
        lcd_wr_data(sx & 0xFF);
        lcd_wr_data(twidth >> 8);
        lcd_wr_data(twidth & 0xFF);
        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(sy >> 8);
        lcd_wr_data(sy & 0xFF);
        lcd_wr_data(theight >> 8);
        lcd_wr_data(theight & 0xFF);
    }
}

/**
 * @brief       SRAM锟阶诧拷锟斤拷锟斤拷锟斤拷时锟斤拷使锟杰ｏ拷锟斤拷锟脚凤拷锟斤拷
 * @note        锟剿猴拷锟斤拷锟结被HAL_SRAM_Init()锟斤拷锟斤拷,锟斤拷始锟斤拷锟斤拷写锟斤拷锟斤拷锟斤拷锟斤拷
 * @param       hsram:SRAM锟斤拷锟�
 * @retval      锟斤拷
 */
void HAL_SRAM_MspInit(SRAM_HandleTypeDef *hsram)
{
    GPIO_InitTypeDef gpio_init_struct;

    __HAL_RCC_FSMC_CLK_ENABLE();            /* 使能FSMC时钟 */
    __HAL_RCC_GPIOD_CLK_ENABLE();           /* 使能GPIOD时钟 */
    __HAL_RCC_GPIOE_CLK_ENABLE();           /* 使能GPIOE时钟 */
    __HAL_RCC_GPIOF_CLK_ENABLE();           /* 使能GPIOF时钟 */
    __HAL_RCC_GPIOG_CLK_ENABLE();           /* 使能GPIOG时钟 */

    /* 初始化PD0,1, 4,5,8,9,10,14,15 (FSMC数据总线D0~D15及控制线) */
    gpio_init_struct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_8 \
                           | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15;
    gpio_init_struct.Mode = GPIO_MODE_AF_PP;            /* 复用推挽 */
    gpio_init_struct.Pull = GPIO_PULLUP;                /* 上拉 */
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;      /* 高速 */
    gpio_init_struct.Alternate = GPIO_AF12_FSMC;        /* 复用为FSMC */

    HAL_GPIO_Init(GPIOD, &gpio_init_struct);            /* 初始化 */

    /* 初始化PE7,8,9,10,11,12,13,14,15 (FSMC数据总线D4~D12) */
    gpio_init_struct.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 \
                           | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gpio_init_struct);

    /* 初始化PF12(A6, LCD_RS), PG12(NE4, LCD_CS) */
    gpio_init_struct.Pin = GPIO_PIN_12;
    HAL_GPIO_Init(GPIOF, &gpio_init_struct);            /* PF12 = FSMC_A6 = LCD_RS */

    gpio_init_struct.Pin = GPIO_PIN_12;
    HAL_GPIO_Init(GPIOG, &gpio_init_struct);            /* PG12 = FSMC_NE4 = LCD_CS */
}

/**
 * @brief       锟斤拷始锟斤拷LCD
 *   @note      锟矫筹拷始锟斤拷锟斤拷锟斤拷锟斤拷锟皆筹拷始锟斤拷锟斤拷锟斤拷锟酵号碉拷LCD(锟斤拷锟斤拷锟�.c锟侥硷拷锟斤拷前锟斤拷锟斤拷锟斤拷锟�)
 *
 * @param       锟斤拷
 * @retval      锟斤拷
 */
void lcd_init(void)
{
    GPIO_InitTypeDef gpio_init_struct;
    FSMC_NORSRAM_TimingTypeDef fsmc_read_handle;
    FSMC_NORSRAM_TimingTypeDef fsmc_write_handle;

    LCD_CS_GPIO_CLK_ENABLE();   /* LCD_CS锟斤拷时锟斤拷使锟斤拷 */
    LCD_WR_GPIO_CLK_ENABLE();   /* LCD_WR锟斤拷时锟斤拷使锟斤拷 */
    LCD_RD_GPIO_CLK_ENABLE();   /* LCD_RD锟斤拷时锟斤拷使锟斤拷 */
    LCD_RS_GPIO_CLK_ENABLE();   /* LCD_RS锟斤拷时锟斤拷使锟斤拷 */
    LCD_BL_GPIO_CLK_ENABLE();   /* LCD_BL锟斤拷时锟斤拷使锟斤拷 */
    
    gpio_init_struct.Pin = LCD_CS_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_AF_PP;                /* 锟斤拷锟届复锟斤拷 */
    gpio_init_struct.Pull = GPIO_PULLUP;                    /* 锟斤拷锟斤拷 */
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;          /* 锟斤拷锟斤拷 */
    gpio_init_struct.Alternate = GPIO_AF12_FSMC;            /* 锟斤拷锟斤拷为FSMC */
    HAL_GPIO_Init(LCD_CS_GPIO_PORT, &gpio_init_struct);     /* 锟斤拷始锟斤拷LCD_CS锟斤拷锟斤拷 */

    gpio_init_struct.Pin = LCD_WR_GPIO_PIN;
    HAL_GPIO_Init(LCD_WR_GPIO_PORT, &gpio_init_struct);     /* 锟斤拷始锟斤拷LCD_WR锟斤拷锟斤拷 */

    gpio_init_struct.Pin = LCD_RD_GPIO_PIN;
    HAL_GPIO_Init(LCD_RD_GPIO_PORT, &gpio_init_struct);     /* 锟斤拷始锟斤拷LCD_RD锟斤拷锟斤拷 */

    gpio_init_struct.Pin = LCD_RS_GPIO_PIN;
    HAL_GPIO_Init(LCD_RS_GPIO_PORT, &gpio_init_struct);     /* 锟斤拷始锟斤拷LCD_RS锟斤拷锟斤拷 */

    gpio_init_struct.Pin = LCD_BL_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;            /* 锟斤拷锟斤拷锟斤拷锟� */
    HAL_GPIO_Init(LCD_BL_GPIO_PORT, &gpio_init_struct);     /* LCD_BL锟斤拷锟斤拷模式锟斤拷锟斤拷(锟斤拷锟斤拷锟斤拷锟�) */

    g_sram_handle.Instance = FSMC_NORSRAM_DEVICE;
    g_sram_handle.Extended = FSMC_NORSRAM_EXTENDED_DEVICE;
    
    g_sram_handle.Init.NSBank = FSMC_NORSRAM_BANK4;                        /* 使锟斤拷NE4 */
    g_sram_handle.Init.DataAddressMux = FSMC_DATA_ADDRESS_MUX_DISABLE;     /* 锟斤拷址/锟斤拷锟斤拷锟竭诧拷锟斤拷锟斤拷 */
    g_sram_handle.Init.MemoryDataWidth = FSMC_NORSRAM_MEM_BUS_WIDTH_16;    /* 16位锟斤拷锟捷匡拷锟斤拷 */
    g_sram_handle.Init.BurstAccessMode = FSMC_BURST_ACCESS_MODE_DISABLE;   /* 锟角凤拷使锟斤拷突锟斤拷锟斤拷锟斤拷,锟斤拷锟斤拷同锟斤拷突锟斤拷锟芥储锟斤拷锟斤拷效,锟剿达拷未锟矫碉拷 */
    g_sram_handle.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW; /* 锟饺达拷锟脚号的硷拷锟斤拷,锟斤拷锟斤拷突锟斤拷模式锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 */
    g_sram_handle.Init.WaitSignalActive = FSMC_WAIT_TIMING_BEFORE_WS;      /* 锟芥储锟斤拷锟斤拷锟节等达拷锟斤拷锟斤拷之前锟斤拷一锟斤拷时锟斤拷锟斤拷锟节伙拷锟角等达拷锟斤拷锟斤拷锟节硷拷使锟斤拷NWAIT */
    g_sram_handle.Init.WriteOperation = FSMC_WRITE_OPERATION_ENABLE;       /* 锟芥储锟斤拷写使锟斤拷 */
    g_sram_handle.Init.WaitSignal = FSMC_WAIT_SIGNAL_DISABLE;              /* 锟饺达拷使锟斤拷位,锟剿达拷未锟矫碉拷 */
    g_sram_handle.Init.ExtendedMode = FSMC_EXTENDED_MODE_ENABLE;           /* 锟斤拷写使锟矫诧拷同锟斤拷时锟斤拷 */
    g_sram_handle.Init.AsynchronousWait = FSMC_ASYNCHRONOUS_WAIT_DISABLE;  /* 锟角凤拷使锟斤拷同锟斤拷锟斤拷锟斤拷模式锟铰的等达拷锟脚猴拷,锟剿达拷未锟矫碉拷 */
    g_sram_handle.Init.WriteBurst = FSMC_WRITE_BURST_DISABLE;              /* 锟斤拷止突锟斤拷写 */
    
    /* FSMC锟斤拷时锟斤拷锟斤拷萍拇锟斤拷锟� */
    fsmc_read_handle.AddressSetupTime = 0x0F;           /* 锟斤拷址锟斤拷锟斤拷时锟斤拷(ADDSET)为15锟斤拷fsmc_ker_ck(1/168=6)锟斤拷6*15=90ns */
    fsmc_read_handle.AddressHoldTime = 0x00;            /* 锟斤拷址锟斤拷锟斤拷时锟斤拷(ADDHLD) 模式A锟斤拷没锟斤拷锟矫碉拷 */
    fsmc_read_handle.DataSetupTime = 60;                /* 锟斤拷锟捷憋拷锟斤拷时锟斤拷(DATAST)为60锟斤拷fsmc_ker_ck=6*60=360ns */
                                                        /* 锟斤拷为液锟斤拷锟斤拷锟斤拷IC锟侥讹拷锟斤拷锟捷碉拷时锟斤拷,锟劫度诧拷锟斤拷太锟斤拷,锟斤拷锟斤拷锟角革拷锟斤拷锟斤拷锟斤拷芯片 */
    fsmc_read_handle.AccessMode = FSMC_ACCESS_MODE_A;   /* 模式A */
    
    /* FSMC写时锟斤拷锟斤拷萍拇锟斤拷锟� */
    fsmc_write_handle.AddressSetupTime = 9;             /* 锟斤拷址锟斤拷锟斤拷时锟斤拷(ADDSET)为9锟斤拷fsmc_ker_ck=6*9=54ns */
    fsmc_write_handle.AddressHoldTime = 0x00;           /* 锟斤拷址锟斤拷锟斤拷时锟斤拷(ADDHLD) 模式A锟斤拷没锟斤拷锟矫碉拷 */
    fsmc_write_handle.DataSetupTime = 9;                /* 锟斤拷锟捷憋拷锟斤拷时锟斤拷(DATAST)为9锟斤拷fsmc_ker_ck=6*9=54ns */
                                                        /* 注锟解：某些液锟斤拷锟斤拷锟斤拷IC锟斤拷写锟脚猴拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷也锟斤拷50ns */
    fsmc_write_handle.AccessMode = FSMC_ACCESS_MODE_A;  /* 模式A */
    
    HAL_SRAM_Init(&g_sram_handle, &fsmc_read_handle, &fsmc_write_handle);
    delay_ms(50);

    /* 锟斤拷锟斤拷9341 ID锟侥讹拷取 */
    lcd_wr_regno(0xD3);
    lcddev.id = lcd_rd_data();  /* dummy read */
    lcddev.id = lcd_rd_data();  /* 锟斤拷锟斤拷0x00 */
    lcddev.id = lcd_rd_data();  /* 锟斤拷取93 */
    lcddev.id <<= 8;
    lcddev.id |= lcd_rd_data(); /* 锟斤拷取41 */

    if (lcddev.id != 0x9341)    /* 锟斤拷锟斤拷 9341 , 锟斤拷锟皆匡拷锟斤拷锟角诧拷锟斤拷 ST7789 */
    {
        lcd_wr_regno(0x04);
        lcddev.id = lcd_rd_data();      /* dummy read */
        lcddev.id = lcd_rd_data();      /* 锟斤拷锟斤拷0x85 */
        lcddev.id = lcd_rd_data();      /* 锟斤拷取0x85 */
        lcddev.id <<= 8;
        lcddev.id |= lcd_rd_data();     /* 锟斤拷取0x52 */
        
        if (lcddev.id == 0x8552)        /* 锟斤拷8552锟斤拷ID转锟斤拷锟斤拷7789 */
        {
            lcddev.id = 0x7789;
        }

        if (lcddev.id != 0x7789)        /* 也锟斤拷锟斤拷ST7789, 锟斤拷锟斤拷锟角诧拷锟斤拷 NT35310 */
        {
            lcd_wr_regno(0xD4);
            lcddev.id = lcd_rd_data();  /* dummy read */
            lcddev.id = lcd_rd_data();  /* 锟斤拷锟斤拷0x01 */
            lcddev.id = lcd_rd_data();  /* 锟斤拷锟斤拷0x53 */
            lcddev.id <<= 8;
            lcddev.id |= lcd_rd_data(); /* 锟斤拷锟斤拷锟斤拷锟�0x10 */

            if (lcddev.id != 0x5310)    /* 也锟斤拷锟斤拷NT35310,锟斤拷锟皆匡拷锟斤拷锟角诧拷锟斤拷ST7796 */
            {
                lcd_wr_regno(0XD3);
                lcddev.id = lcd_rd_data();  /* dummy read */
                lcddev.id = lcd_rd_data();  /* 锟斤拷锟斤拷0X00 */
                lcddev.id = lcd_rd_data();  /* 锟斤拷取0X77 */
                lcddev.id <<= 8;
                lcddev.id |= lcd_rd_data(); /* 锟斤拷取0X96 */
                
                if (lcddev.id != 0x7796)    /* 也锟斤拷锟斤拷ST7796,锟斤拷锟皆匡拷锟斤拷锟角诧拷锟斤拷NT35510 */
                {
                    /* 锟斤拷锟斤拷锟斤拷钥锟斤拷锟斤拷锟斤拷锟结供锟斤拷 */
                    lcd_write_reg(0xF000, 0x0055);
                    lcd_write_reg(0xF001, 0x00AA);
                    lcd_write_reg(0xF002, 0x0052);
                    lcd_write_reg(0xF003, 0x0008);
                    lcd_write_reg(0xF004, 0x0001);
                    
                    lcd_wr_regno(0xC500);       /* 锟斤拷取ID锟酵帮拷位 */
                    lcddev.id = lcd_rd_data();  /* 锟斤拷锟斤拷0x80 */
                    lcddev.id <<= 8;

                    lcd_wr_regno(0xC501);       /* 锟斤拷取ID锟竭帮拷位 */
                    lcddev.id |= lcd_rd_data(); /* 锟斤拷锟斤拷0x00 */
                    
                    delay_ms(5);                /* 锟饺达拷5ms, 锟斤拷为0XC501指锟斤拷锟�1963锟斤拷说锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷位指锟斤拷, 锟饺达拷5ms锟斤拷1963锟斤拷位锟斤拷锟斤拷俨锟斤拷锟� */

                    if (lcddev.id != 0x5510)    /* 也锟斤拷锟斤拷NT5510,锟斤拷锟皆匡拷锟斤拷锟角诧拷锟斤拷ILI9806 */
                    {
                        lcd_wr_regno(0XD3);
                        lcddev.id = lcd_rd_data();  /* dummy read */
                        lcddev.id = lcd_rd_data();  /* 锟斤拷锟斤拷0X00 */
                        lcddev.id = lcd_rd_data();  /* 锟斤拷锟斤拷0X98 */
                        lcddev.id <<= 8;
                        lcddev.id |= lcd_rd_data(); /* 锟斤拷锟斤拷0X06 */
                        
                        if (lcddev.id != 0x9806)    /* 也锟斤拷锟斤拷ILI9806,锟斤拷锟皆匡拷锟斤拷锟角诧拷锟斤拷SSD1963 */
                        {
                            lcd_wr_regno(0xA1);
                            lcddev.id = lcd_rd_data();
                            lcddev.id = lcd_rd_data();  /* 锟斤拷锟斤拷0x57 */
                            lcddev.id <<= 8;
                            lcddev.id |= lcd_rd_data(); /* 锟斤拷锟斤拷0x61 */

                            if (lcddev.id == 0x5761) lcddev.id = 0x1963; /* SSD1963锟斤拷锟截碉拷ID锟斤拷5761H,为锟斤拷锟斤拷锟斤拷锟斤拷,锟斤拷锟斤拷强锟斤拷锟斤拷锟斤拷为1963 */
                        }
                    }
                }
            }
        }
    }

    /* 锟截憋拷注锟斤拷, 锟斤拷锟斤拷锟絤ain锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟轿达拷锟斤拷1锟斤拷始锟斤拷, 锟斤拷峥拷锟斤拷锟絧rintf
     * 锟斤拷锟斤拷(锟斤拷锟斤拷锟斤拷f_putc锟斤拷锟斤拷), 锟斤拷锟斤拷, 锟斤拷锟斤拷锟绞硷拷锟斤拷锟斤拷锟�1, 锟斤拷锟斤拷锟斤拷锟轿碉拷锟斤拷锟斤拷
     * 锟斤拷锟斤拷 printf 锟斤拷锟� !!!!!!!
     */
    printf("LCD ID:%x\r\n", lcddev.id); /* 锟斤拷印LCD ID */

    if (lcddev.id == 0x7789)
    {
        lcd_ex_st7789_reginit();    /* 执锟斤拷ST7789锟斤拷始锟斤拷 */
    }
    else if (lcddev.id == 0x9341)
    {
        lcd_ex_ili9341_reginit();   /* 执锟斤拷ILI9341锟斤拷始锟斤拷 */
    }
    else if (lcddev.id == 0x5310)
    {
        lcd_ex_nt35310_reginit();   /* 执锟斤拷NT35310锟斤拷始锟斤拷 */
    }
    else if (lcddev.id == 0x7796)
    {
        lcd_ex_st7796_reginit();    /* 执锟斤拷ST7796锟斤拷始锟斤拷 */
    }
    else if (lcddev.id == 0x5510)
    {
        lcd_ex_nt35510_reginit();   /* 执锟斤拷NT35510锟斤拷始锟斤拷 */
    }
    else if (lcddev.id == 0x9806)
    {
        lcd_ex_ili9806_reginit();   /* 执锟斤拷ILI9806锟斤拷始锟斤拷 */
    }
    else if (lcddev.id == 0x1963)
    {
        lcd_ex_ssd1963_reginit();   /* 执锟斤拷SSD1963锟斤拷始锟斤拷 */
        lcd_ssd_backlight_set(100); /* 锟斤拷锟斤拷锟斤拷锟斤拷为锟斤拷锟斤拷 */
    }

    /* 锟斤拷锟节诧拷同锟斤拷幕锟斤拷写时锟斤拷同锟斤拷锟斤拷锟斤拷锟绞憋拷锟斤拷锟皆革拷锟斤拷锟皆硷拷锟斤拷锟斤拷幕锟斤拷锟斤拷锟睫革拷
      锟斤拷锟斤拷锟斤拷锟较筹拷锟斤拷锟竭讹拷时锟斤拷也锟斤拷锟斤拷影锟届，锟斤拷要锟皆硷拷锟斤拷锟斤拷锟斤拷锟斤拷薷模锟� */
    /* 锟斤拷始锟斤拷锟斤拷锟斤拷院锟�,锟斤拷锟斤拷 */
    if (lcddev.id == 0x7789)
    {
        /* 锟斤拷锟斤拷锟斤拷锟斤拷写时锟斤拷锟斤拷萍拇锟斤拷锟斤拷锟绞憋拷锟� */
        fsmc_write_handle.AddressSetupTime = 3; /* 锟斤拷址锟斤拷锟斤拷时锟斤拷(ADDSET)为3锟斤拷fsmc_ker_ck=6*3=18ns */
        fsmc_write_handle.DataSetupTime = 3;    /* 锟斤拷锟捷憋拷锟斤拷时锟斤拷(DATAST)为3锟斤拷fsmc_ker_ck=6*3=18ns */
        FSMC_NORSRAM_Extended_Timing_Init(g_sram_handle.Extended, &fsmc_write_handle, g_sram_handle.Init.NSBank, g_sram_handle.Init.ExtendedMode);
    }
    else if (lcddev.id == 0x9806 || lcddev.id == 0x9341 || lcddev.id == 0x5510)
    {
        /* 锟斤拷锟斤拷锟斤拷锟斤拷写时锟斤拷锟斤拷萍拇锟斤拷锟斤拷锟绞憋拷锟� */
        fsmc_write_handle.AddressSetupTime = 2; /* 锟斤拷址锟斤拷锟斤拷时锟斤拷(ADDSET)为2锟斤拷fsmc_ker_ck=6*2=12ns */
        fsmc_write_handle.DataSetupTime = 2;    /* 锟斤拷锟捷憋拷锟斤拷时锟斤拷(DATAST)为2锟斤拷fsmc_ker_ck=6*2=12ns */
        FSMC_NORSRAM_Extended_Timing_Init(g_sram_handle.Extended, &fsmc_write_handle, g_sram_handle.Init.NSBank, g_sram_handle.Init.ExtendedMode);
    }
    else if (lcddev.id == 0x5310 || lcddev.id == 0x7796 || lcddev.id == 0x1963)
    {
        /* 锟斤拷锟斤拷锟斤拷锟斤拷写时锟斤拷锟斤拷萍拇锟斤拷锟斤拷锟绞憋拷锟� */
        fsmc_write_handle.AddressSetupTime = 1; /* 锟斤拷址锟斤拷锟斤拷时锟斤拷(ADDSET)为1锟斤拷fsmc_ker_ck=6*1=6ns */
        fsmc_write_handle.DataSetupTime = 1;    /* 锟斤拷锟捷憋拷锟斤拷时锟斤拷(DATAST)为1锟斤拷fsmc_ker_ck=6*1=6ns */
        FSMC_NORSRAM_Extended_Timing_Init(g_sram_handle.Extended, &fsmc_write_handle, g_sram_handle.Init.NSBank, g_sram_handle.Init.ExtendedMode);
    }

    lcd_display_dir(0); /* 默锟斤拷为锟斤拷锟斤拷 */
    LCD_BL(1);          /* 锟斤拷锟斤拷锟斤拷锟斤拷 */
    lcd_clear(WHITE);
}

/**
 * @brief       锟斤拷锟斤拷锟斤拷锟斤拷
 * @param       color: 要锟斤拷锟斤拷锟斤拷锟斤拷色
 * @retval      锟斤拷
 */
void lcd_clear(uint16_t color)
{
    uint32_t index = 0;
    uint32_t totalpoint = lcddev.width;

    totalpoint *= lcddev.height;    /* 锟矫碉拷锟杰碉拷锟斤拷 */
    lcd_set_cursor(0x00, 0x0000);   /* 锟斤拷锟矫癸拷锟轿伙拷锟� */
    lcd_write_ram_prepare();        /* 锟斤拷始写锟斤拷GRAM */

    for (index = 0; index < totalpoint; index++)
    {
        LCD->LCD_RAM = color;
    }
}

/**
 * @brief       锟斤拷指锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷涞ワ拷锟斤拷锟缴�
 * @param       (sx,sy),(ex,ey):锟斤拷锟斤拷锟轿对斤拷锟斤拷锟斤拷,锟斤拷锟斤拷锟叫∥�:(ex - sx + 1) * (ey - sy + 1)
 * @param       color:  要锟斤拷锟斤拷锟斤拷色(32位锟斤拷色,锟斤拷锟斤拷锟斤拷锟絃TDC)
 * @retval      锟斤拷
 */
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color)
{
    uint16_t i, j;
    uint16_t xlen = 0;
    xlen = ex - sx + 1;

    for (i = sy; i <= ey; i++)
    {
        lcd_set_cursor(sx, i);      /* 锟斤拷锟矫癸拷锟轿伙拷锟� */
        lcd_write_ram_prepare();    /* 锟斤拷始写锟斤拷GRAM */

        for (j = 0; j < xlen; j++)
        {
            LCD->LCD_RAM = color;   /* 锟斤拷示锟斤拷色 */
        }
    }
}

/**
 * @brief       锟斤拷指锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟街革拷锟斤拷锟缴拷锟�
 * @param       (sx,sy),(ex,ey):锟斤拷锟斤拷锟轿对斤拷锟斤拷锟斤拷,锟斤拷锟斤拷锟叫∥�:(ex - sx + 1) * (ey - sy + 1)
 * @param       color: 要锟斤拷锟斤拷锟斤拷色锟斤拷锟斤拷锟阶碉拷址
 * @retval      锟斤拷
 */
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color)
{
    uint16_t height, width;
    uint16_t i, j;

    width = ex - sx + 1;            /* 锟矫碉拷锟斤拷锟侥匡拷锟斤拷 */
    height = ey - sy + 1;           /* 锟竭讹拷 */

    for (i = 0; i < height; i++)
    {
        lcd_set_cursor(sx, sy + i); /* 锟斤拷锟矫癸拷锟轿伙拷锟� */
        lcd_write_ram_prepare();    /* 锟斤拷始写锟斤拷GRAM */

        for (j = 0; j < width; j++)
        {
            LCD->LCD_RAM = color[i * width + j]; /* 写锟斤拷锟斤拷锟斤拷 */
        }
    }
}

/**
 * @brief       锟斤拷锟斤拷
 * @param       x1,y1: 锟斤拷锟斤拷锟斤拷锟�
 * @param       x2,y2: 锟秸碉拷锟斤拷锟斤拷
 * @param       color: 锟竭碉拷锟斤拷色
 * @retval      锟斤拷
 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    uint16_t t;
    int xerr = 0, yerr = 0, delta_x, delta_y, distance;
    int incx, incy, row, col;
    delta_x = x2 - x1;      /* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 */
    delta_y = y2 - y1;
    row = x1;
    col = y1;

    if (delta_x > 0)
    {
        incx = 1;       /* 锟斤拷锟矫碉拷锟斤拷锟斤拷锟斤拷 */
    }
    else if (delta_x == 0)
    {
        incx = 0;       /* 锟斤拷直锟斤拷 */
    }
    else
    {
        incx = -1;
        delta_x = -delta_x;
    }

    if (delta_y > 0)
    {
        incy = 1;
    }
    else if (delta_y == 0)
    {
        incy = 0;       /* 水平锟斤拷 */
    }
    else
    {
        incy = -1;
        delta_y = -delta_y;
    }

    if ( delta_x > delta_y)
    {
        distance = delta_x;  /* 选取锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 */
    }
    else
    {
        distance = delta_y;
    }

    for (t = 0; t <= distance + 1; t++)     /* 锟斤拷锟斤拷锟斤拷锟� */
    {
        lcd_draw_point(row, col, color);    /* 锟斤拷锟斤拷 */
        xerr += delta_x;
        yerr += delta_y;

        if (xerr > distance)
        {
            xerr -= distance;
            row += incx;
        }

        if (yerr > distance)
        {
            yerr -= distance;
            col += incy;
        }
    }
}

/**
 * @brief       锟斤拷水平锟斤拷
 * @param       x,y   : 锟斤拷锟斤拷锟斤拷锟�
 * @param       len   : 锟竭筹拷锟斤拷
 * @param       color : 锟斤拷锟轿碉拷锟斤拷色
 * @retval      锟斤拷
 */
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color)
{
    if ((len == 0) || (x > lcddev.width) || (y > lcddev.height))
    {
        return;
    }

    lcd_fill(x, y, x + len - 1, y, color);
}

/**
 * @brief       锟斤拷锟斤拷锟斤拷
 * @param       x1,y1: 锟斤拷锟斤拷锟斤拷锟�
 * @param       x2,y2: 锟秸碉拷锟斤拷锟斤拷
 * @param       color: 锟斤拷锟轿碉拷锟斤拷色
 * @retval      锟斤拷
 */
void lcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    lcd_draw_line(x1, y1, x2, y1, color);
    lcd_draw_line(x1, y1, x1, y2, color);
    lcd_draw_line(x1, y2, x2, y2, color);
    lcd_draw_line(x2, y1, x2, y2, color);
}

/**
 * @brief       锟斤拷圆
 * @param       x0,y0 : 圆锟斤拷锟斤拷锟斤拷锟斤拷
 * @param       r     : 锟诫径
 * @param       color : 圆锟斤拷锟斤拷色
 * @retval      锟斤拷
 */
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color)
{
    int a, b;
    int di;

    a = 0;
    b = r;
    di = 3 - (r << 1);       /* 锟叫讹拷锟铰革拷锟斤拷位锟矫的憋拷志 */

    while (a <= b)
    {
        lcd_draw_point(x0 + a, y0 - b, color);  /* 5 */
        lcd_draw_point(x0 + b, y0 - a, color);  /* 0 */
        lcd_draw_point(x0 + b, y0 + a, color);  /* 4 */
        lcd_draw_point(x0 + a, y0 + b, color);  /* 6 */
        lcd_draw_point(x0 - a, y0 + b, color);  /* 1 */
        lcd_draw_point(x0 - b, y0 + a, color);
        lcd_draw_point(x0 - a, y0 - b, color);  /* 2 */
        lcd_draw_point(x0 - b, y0 - a, color);  /* 7 */
        a++;

        /* 使锟斤拷Bresenham锟姐法锟斤拷圆 */
        if (di < 0)
        {
            di += 4 * a + 6;
        }
        else
        {
            di += 10 + 4 * (a - b);
            b--;
        }
    }
}

/**
 * @brief       锟斤拷锟绞碉拷锟皆�
 * @param       x,y  : 圆锟斤拷锟斤拷锟斤拷锟斤拷
 * @param       r    : 锟诫径
 * @param       color: 圆锟斤拷锟斤拷色
 * @retval      锟斤拷
 */
void lcd_fill_circle(uint16_t x, uint16_t y, uint16_t r, uint16_t color)
{
    uint32_t i;
    uint32_t imax = ((uint32_t)r * 707) / 1000 + 1;
    uint32_t sqmax = (uint32_t)r * (uint32_t)r + (uint32_t)r / 2;
    uint32_t xr = r;

    lcd_draw_hline(x - r, y, 2 * r, color);

    for (i = 1; i <= imax; i++)
    {
        if ((i * i + xr * xr) > sqmax)
        {
            /* draw lines from outside */
            if (xr > imax)
            {
                lcd_draw_hline (x - i + 1, y + xr, 2 * (i - 1), color);
                lcd_draw_hline (x - i + 1, y - xr, 2 * (i - 1), color);
            }

            xr--;
        }

        /* draw lines from inside (center) */
        lcd_draw_hline(x - xr, y + i, 2 * xr, color);
        lcd_draw_hline(x - xr, y - i, 2 * xr, color);
    }
}

/**
 * @brief       锟斤拷指锟斤拷位锟斤拷锟斤拷示一锟斤拷锟街凤拷
 * @param       x,y  : 锟斤拷锟斤拷
 * @param       chr  : 要锟斤拷示锟斤拷锟街凤拷:" "--->"~"
 * @param       size : 锟斤拷锟斤拷锟叫� 12/16/24/32
 * @param       mode : 锟斤拷锟接凤拷式(1); 锟角碉拷锟接凤拷式(0);
 * @param       color : 锟街凤拷锟斤拷锟斤拷色;
 * @retval      锟斤拷
 */
void lcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t temp, t1, t;
    uint16_t y0 = y;
    uint8_t csize = 0;
    uint8_t *pfont = 0;

    csize = (size / 8 + ((size % 8) ? 1 : 0)) * (size / 2); /* 锟矫碉拷锟斤拷锟斤拷一锟斤拷锟街凤拷锟斤拷应锟斤拷锟斤拷锟斤拷占锟斤拷锟街斤拷锟斤拷 */
    chr = chr - ' ';    /* 锟矫碉拷偏锟狡猴拷锟街碉拷锟紸SCII锟街匡拷锟角从空革拷始取模锟斤拷锟斤拷锟斤拷-' '锟斤拷锟角讹拷应锟街凤拷锟斤拷锟街库） */

    switch (size)
    {
        case 12:
            pfont = (uint8_t *)asc2_1206[chr];  /* 锟斤拷锟斤拷1206锟斤拷锟斤拷 */
            break;

        case 16:
            pfont = (uint8_t *)asc2_1608[chr];  /* 锟斤拷锟斤拷1608锟斤拷锟斤拷 */
            break;

        case 24:
            pfont = (uint8_t *)asc2_2412[chr];  /* 锟斤拷锟斤拷2412锟斤拷锟斤拷 */
            break;

        case 32:
            pfont = (uint8_t *)asc2_3216[chr];  /* 锟斤拷锟斤拷3216锟斤拷锟斤拷 */
            break;

        default:
            return ;
    }

    for (t = 0; t < csize; t++)
    {
        temp = pfont[t];                            /* 锟斤拷取锟街凤拷锟侥碉拷锟斤拷锟斤拷锟斤拷 */

        for (t1 = 0; t1 < 8; t1++)                  /* 一锟斤拷锟街斤拷8锟斤拷锟斤拷 */
        {
            if (temp & 0x80)                        /* 锟斤拷效锟斤拷,锟斤拷要锟斤拷示 */
            {
                lcd_draw_point(x, y, color);        /* 锟斤拷锟斤拷锟斤拷锟�,要锟斤拷示锟斤拷锟斤拷锟� */
            }
            else if (mode == 0)                     /* 锟斤拷效锟斤拷,锟斤拷锟斤拷示 */
            {
                lcd_draw_point(x, y, g_back_color); /* 锟斤拷锟斤拷锟斤拷色,锟洁当锟斤拷锟斤拷锟斤拷悴伙拷锟绞�(注锟解背锟斤拷色锟斤拷全锟街憋拷锟斤拷锟斤拷锟斤拷) */
            }

            temp <<= 1;                             /* 锟斤拷位, 锟皆憋拷锟饺★拷锟揭伙拷锟轿伙拷锟阶刺� */
            y++;

            if (y >= lcddev.height)return;          /* 锟斤拷锟斤拷锟斤拷锟斤拷 */

            if ((y - y0) == size)                   /* 锟斤拷示锟斤拷一锟斤拷锟斤拷? */
            {
                y = y0; /* y锟斤拷锟疥复位 */
                x++;    /* x锟斤拷锟斤拷锟斤拷锟� */

                if (x >= lcddev.width)
                {
                    return;       /* x锟斤拷锟疥超锟斤拷锟斤拷锟斤拷 */
                }

                break;
            }
        }
    }
}

/**
 * @brief       平锟斤拷锟斤拷锟斤拷, m^n
 * @param       m: 锟斤拷锟斤拷
 * @param       n: 指锟斤拷
 * @retval      m锟斤拷n锟轿凤拷
 */
static uint32_t lcd_pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;

    while (n--)
    {
        result *= m;
    }

    return result;
}

/**
 * @brief       锟斤拷示len锟斤拷锟斤拷锟斤拷
 * @param       x,y : 锟斤拷始锟斤拷锟斤拷
 * @param       num : 锟斤拷值(0 ~ 2^32)
 * @param       len : 锟斤拷示锟斤拷锟街碉拷位锟斤拷
 * @param       size: 选锟斤拷锟斤拷锟斤拷 12/16/24/32
 * @param       color : 锟斤拷锟街碉拷锟斤拷色;
 * @retval      锟斤拷
 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;

    for (t = 0; t < len; t++)   /* 锟斤拷锟斤拷锟斤拷示位锟斤拷循锟斤拷 */
    {
        temp = (num / lcd_pow(10, len - t - 1)) % 10;   /* 锟斤拷取锟斤拷应位锟斤拷锟斤拷锟斤拷 */

        if (enshow == 0 && t < (len - 1))               /* 没锟斤拷使锟斤拷锟斤拷示,锟揭伙拷锟斤拷位要锟斤拷示 */
        {
            if (temp == 0)
            {
                lcd_show_char(x + (size / 2) * t, y, ' ', size, 0, color);  /* 锟斤拷示锟秸革拷,占位 */
                continue;       /* 锟斤拷锟斤拷锟铰革拷一位 */
            }
            else
            {
                enshow = 1;     /* 使锟斤拷锟斤拷示 */
            }
        }

        lcd_show_char(x + (size / 2) * t, y, temp + '0', size, 0, color);   /* 锟斤拷示锟街凤拷 */
    }
}

/**
 * @brief       锟斤拷展锟斤拷示len锟斤拷锟斤拷锟斤拷(锟斤拷位锟斤拷0也锟斤拷示)
 * @param       x,y : 锟斤拷始锟斤拷锟斤拷
 * @param       num : 锟斤拷值(0 ~ 2^32)
 * @param       len : 锟斤拷示锟斤拷锟街碉拷位锟斤拷
 * @param       size: 选锟斤拷锟斤拷锟斤拷 12/16/24/32
 * @param       mode: 锟斤拷示模式
 *              [7]:0,锟斤拷锟斤拷锟�;1,锟斤拷锟�0.
 *              [6:1]:锟斤拷锟斤拷
 *              [0]:0,锟角碉拷锟斤拷锟斤拷示;1,锟斤拷锟斤拷锟斤拷示.
 * @param       color : 锟斤拷锟街碉拷锟斤拷色;
 * @retval      锟斤拷
 */
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;

    for (t = 0; t < len; t++)       /* 锟斤拷锟斤拷锟斤拷示位锟斤拷循锟斤拷 */
    {
        temp = (num / lcd_pow(10, len - t - 1)) % 10;    /* 锟斤拷取锟斤拷应位锟斤拷锟斤拷锟斤拷 */

        if (enshow == 0 && t < (len - 1))   /* 没锟斤拷使锟斤拷锟斤拷示,锟揭伙拷锟斤拷位要锟斤拷示 */
        {
            if (temp == 0)
            {
                if (mode & 0x80)    /* 锟斤拷位锟斤拷要锟斤拷锟�0 */
                {
                    lcd_show_char(x + (size / 2) * t, y, '0', size, mode & 0x01, color);    /* 锟斤拷0占位 */
                }
                else
                {
                    lcd_show_char(x + (size / 2) * t, y, ' ', size, mode & 0x01, color);    /* 锟矫空革拷占位 */
                }

                continue;
            }
            else
            {
                enshow = 1;         /* 使锟斤拷锟斤拷示 */
            }

        }

        lcd_show_char(x + (size / 2) * t, y, temp + '0', size, mode & 0x01, color);
    }
}

/**
 * @brief       锟斤拷示锟街凤拷锟斤拷
 * @param       x,y         : 锟斤拷始锟斤拷锟斤拷
 * @param       width,height: 锟斤拷锟斤拷锟叫�
 * @param       size        : 选锟斤拷锟斤拷锟斤拷 12/16/24/32
 * @param       p           : 锟街凤拷锟斤拷锟阶碉拷址
 * @param       color       : 锟街凤拷锟斤拷锟斤拷锟斤拷色;
 * @retval      锟斤拷
 */
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color)
{
    uint8_t x0 = x;
    
    width += x;
    height += y;

    while ((*p <= '~') && (*p >= ' '))   /* 锟叫讹拷锟角诧拷锟角非凤拷锟街凤拷! */
    {
        if (x >= width)
        {
            x = x0;
            y += size;
        }

        if (y >= height)
        {
            break;      /* 锟剿筹拷 */
        }

        lcd_show_char(x, y, *p, size, 0, color);
        x += size / 2;
        p++;
    }
}















