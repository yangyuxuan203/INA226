/**
 ****************************************************************************************************
 * @file        ft5206.c
 * @author      锟斤拷锟斤拷原锟斤拷锟脚讹拷(ALIENTEK)
 * @version     V1.1
 * @date        2023-05-29
 * @brief       7锟斤拷锟斤拷荽锟斤拷锟斤拷锟�-FT5206/FT5426 锟斤拷锟斤拷锟斤拷锟斤拷
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
 * 锟斤拷锟斤拷7锟斤拷 CST340锟斤拷锟斤拷锟斤拷
 ****************************************************************************************************
 */

#include "string.h"
#include "lcd.h"
#include "touch.h"
#include "ctiic.h"
#include "ft5206.h"
#include "delay.h"
#include <stdio.h>


/**
 * @brief       锟斤拷FT5206写锟斤拷一锟斤拷锟斤拷锟斤拷
 * @param       reg : 锟斤拷始锟侥达拷锟斤拷锟斤拷址
 * @param       buf : 锟斤拷锟捷伙拷锟斤拷锟斤拷锟斤拷
 * @param       len : 写锟斤拷锟捷筹拷锟斤拷
 * @retval      0, 锟缴癸拷; 1, 失锟斤拷;
 */
uint8_t ft5206_wr_reg(uint16_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;
    uint8_t ret = 0;
    
    ct_iic_start();
    ct_iic_send_byte(FT5206_CMD_WR);    /* 锟斤拷锟斤拷写锟斤拷锟斤拷 */
    ct_iic_wait_ack();
    ct_iic_send_byte(reg & 0XFF);       /* 锟斤拷锟酵碉拷8位锟斤拷址 */
    ct_iic_wait_ack();

    for (i = 0; i < len; i++)
    {
        ct_iic_send_byte(buf[i]);       /* 锟斤拷锟斤拷锟斤拷 */
        ret = ct_iic_wait_ack();

        if (ret)break;
    }

    ct_iic_stop();  /* 锟斤拷锟斤拷一锟斤拷停止锟斤拷锟斤拷 */
    return ret;
}

/**
 * @brief       锟斤拷FT5206锟斤拷锟斤拷一锟斤拷锟斤拷锟斤拷
 * @param       reg : 锟斤拷始锟侥达拷锟斤拷锟斤拷址
 * @param       buf : 锟斤拷锟捷伙拷锟斤拷锟斤拷锟斤拷
 * @param       len : 锟斤拷锟斤拷锟捷筹拷锟斤拷
 * @retval      锟斤拷
 */
void ft5206_rd_reg(uint16_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;
    
    ct_iic_start();
    ct_iic_send_byte(FT5206_CMD_WR);    /* 锟斤拷锟斤拷写锟斤拷锟斤拷 */
    ct_iic_wait_ack();
    ct_iic_send_byte(reg & 0XFF);       /* 锟斤拷锟酵碉拷8位锟斤拷址 */
    ct_iic_wait_ack();
    ct_iic_start();
    ct_iic_send_byte(FT5206_CMD_RD);    /* 锟斤拷锟酵讹拷锟斤拷锟斤拷 */
    ct_iic_wait_ack();

    for (i = 0; i < len; i++)
    {
        buf[i] = ct_iic_read_byte(i == (len - 1) ? 0 : 1);  /* 锟斤拷取锟斤拷锟斤拷 */
    }

    ct_iic_stop();  /* 锟斤拷锟斤拷一锟斤拷停止锟斤拷锟斤拷 */
}

/**
 * @brief       锟斤拷始锟斤拷FT5206锟斤拷锟斤拷锟斤拷
 * @param       锟斤拷
 * @retval      0, 锟斤拷始锟斤拷锟缴癸拷; 1, 锟斤拷始锟斤拷失锟斤拷;
 */
uint8_t ft5206_init(void)
{
    GPIO_InitTypeDef gpio_init_struct;
    uint8_t temp[2];

    FT5206_RST_GPIO_CLK_ENABLE();   /* RST锟斤拷锟斤拷时锟斤拷使锟斤拷 */
    FT5206_INT_GPIO_CLK_ENABLE();   /* INT锟斤拷锟斤拷时锟斤拷使锟斤拷 */

    gpio_init_struct.Pin = FT5206_RST_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;            /* 锟斤拷锟斤拷锟斤拷锟� */
    gpio_init_struct.Pull = GPIO_PULLUP;                    /* 锟斤拷锟斤拷 */
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;     /* 锟斤拷锟斤拷 */
    HAL_GPIO_Init(FT5206_RST_GPIO_PORT, &gpio_init_struct); /* 锟斤拷始锟斤拷RST锟斤拷锟斤拷 */

    gpio_init_struct.Pin = FT5206_INT_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_INPUT;                /* 锟斤拷锟斤拷 */
    gpio_init_struct.Pull = GPIO_PULLUP;                    /* 锟斤拷锟斤拷 */
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;     /* 锟斤拷锟斤拷 */
    HAL_GPIO_Init(FT5206_INT_GPIO_PORT, &gpio_init_struct); /* 锟斤拷始锟斤拷INT锟斤拷锟斤拷 */

    ct_iic_init();      /* 锟斤拷始锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷I2C锟斤拷锟斤拷 */
    FT5206_RST(0);      /* 锟斤拷位 */
    delay_ms(20);
    FT5206_RST(1);      /* 锟酵放革拷位 */
    delay_ms(50);
    temp[0] = 0;
    ft5206_wr_reg(FT5206_DEVIDE_MODE, temp, 1); /* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷模式 */
    ft5206_wr_reg(FT5206_ID_G_MODE, temp, 1);   /* 锟斤拷询模式 */
    temp[0] = 22;                               /* 锟斤拷锟斤拷锟斤拷效值锟斤拷22锟斤拷越小越锟斤拷锟斤拷 */
    ft5206_wr_reg(FT5206_ID_G_THGROUP, temp, 1);/* 锟斤拷锟矫达拷锟斤拷锟斤拷效值 */
    temp[0] = 12;                               /* 锟斤拷锟斤拷锟斤拷锟节ｏ拷锟斤拷锟斤拷小锟斤拷12锟斤拷锟斤拷锟�14 */
    ft5206_wr_reg(FT5206_ID_G_PERIODACTIVE, temp, 1);
    
    /* 锟斤拷取锟芥本锟脚ｏ拷锟轿匡拷值锟斤拷0x3003 */
    ft5206_rd_reg(FT5206_ID_G_LIB_VERSION, &temp[0], 2);
    
    if ((temp[0] == 0X30 && temp[1] == 0X03) || temp[1] == 0X01 || temp[1] == 0X02 || (temp[0] == 0x0 && temp[1] == 0X0))   /* 锟芥本:0X3003/0X0001/0X0002/CST340 */
    {
        printf("CTP ID:%x\r\n", ((uint16_t)temp[0] << 8) + temp[1]);
        return 0;
    }

    return 1;
}

/* FT5206 5锟斤拷锟斤拷锟斤拷锟斤拷 锟斤拷应锟侥寄达拷锟斤拷锟斤拷 */
const uint16_t FT5206_TPX_TBL[5] = {FT5206_TP1_REG, FT5206_TP2_REG, FT5206_TP3_REG, FT5206_TP4_REG, FT5206_TP5_REG};

/**
 * @brief       扫锟借触锟斤拷锟斤拷(锟斤拷锟矫诧拷询锟斤拷式)
 * @param       mode : 锟斤拷锟斤拷锟斤拷未锟矫碉拷锟轿诧拷锟斤拷, 为锟剿硷拷锟捷碉拷锟斤拷锟斤拷
 * @retval      锟斤拷前锟斤拷锟斤拷状态
 *   @arg       0, 锟斤拷锟斤拷锟睫达拷锟斤拷; 
 *   @arg       1, 锟斤拷锟斤拷锟叫达拷锟斤拷;
 */
uint8_t ft5206_scan(uint8_t mode)
{
    uint8_t sta = 0;
    uint8_t buf[4];
    uint8_t i = 0;
    uint8_t res = 0;
    uint16_t temp;
    static uint8_t t = 0;   /* 锟斤拷锟狡诧拷询锟斤拷锟�,锟接讹拷锟斤拷锟斤拷CPU占锟斤拷锟斤拷 */
    
    t++;
    
    if ((t % 10) == 0 || t < 10)   /* 锟斤拷锟斤拷时,每锟斤拷锟斤拷10锟斤拷CTP_Scan锟斤拷锟斤拷锟脚硷拷锟�1锟斤拷,锟接讹拷锟斤拷省CPU使锟斤拷锟斤拷 */
    {
        ft5206_rd_reg(FT5206_REG_NUM_FINGER, &sta, 1);  /* 锟斤拷取锟斤拷锟斤拷锟斤拷锟阶刺� */

        if ((sta & 0XF) && ((sta & 0XF) < 6))
        {
            temp = 0XFFFF << (sta & 0XF);           /* 锟斤拷锟斤拷母锟斤拷锟阶拷锟轿�1锟斤拷位锟斤拷,匹锟斤拷tp_dev.sta锟斤拷锟斤拷 */
            tp_dev.sta = (~temp) | TP_PRES_DOWN | TP_CATH_PRES;
            delay_ms(4);    /* 锟斤拷要锟斤拷锟斤拷时锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷为锟叫帮拷锟斤拷锟斤拷锟斤拷 */
            
            for (i = 0; i < 5; i++)
            {
                if (tp_dev.sta & (1 << i))          /* 锟斤拷锟斤拷锟斤拷效? */
                {
                    ft5206_rd_reg(FT5206_TPX_TBL[i], buf, 4);   /* 锟斤拷取XY锟斤拷锟斤拷值 */

                    if (tp_dev.touchtype & 0X01)    /* 锟斤拷锟斤拷 */
                    {
                        tp_dev.y[i] = ((uint16_t)(buf[0] & 0X0F) << 8) + buf[1];
                        tp_dev.x[i] = ((uint16_t)(buf[2] & 0X0F) << 8) + buf[3];
                    }
                    else
                    {
                        tp_dev.x[i] = lcddev.width - (((uint16_t)(buf[0] & 0X0F) << 8) + buf[1]);
                        tp_dev.y[i] = ((uint16_t)(buf[2] & 0X0F) << 8) + buf[3];
                    }

                    if ((buf[0] & 0XF0) != 0X80)tp_dev.x[i] = tp_dev.y[i] = 0;      /* 锟斤拷锟斤拷锟斤拷contact锟铰硷拷锟斤拷锟斤拷锟斤拷为锟斤拷效 */

                    //printf("x[%d]:%d,y[%d]:%d\r\n", i, tp_dev.x[i], i, tp_dev.y[i]);
                }
            }

            res = 1;

            if (tp_dev.x[0] == 0 && tp_dev.y[0] == 0)sta = 0;   /* 锟斤拷锟斤拷锟斤拷锟斤拷锟捷讹拷锟斤拷0,锟斤拷锟斤拷源舜锟斤拷锟斤拷锟� */

            t = 0;  /* 锟斤拷锟斤拷一锟斤拷,锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷10锟斤拷,锟接讹拷锟斤拷锟斤拷锟斤拷锟斤拷锟� */
        }
    }

    if ((sta & 0X1F) == 0)  /* 锟睫达拷锟斤拷锟姐按锟斤拷 */
    {
        if (tp_dev.sta & TP_PRES_DOWN)      /* 之前锟角憋拷锟斤拷锟铰碉拷 */
        {
            tp_dev.sta &= ~TP_PRES_DOWN;    /* 锟斤拷前锟斤拷锟斤拷煽锟� */
        }
        else    /* 之前锟斤拷没锟叫憋拷锟斤拷锟斤拷 */
        {
            tp_dev.x[0] = 0xffff;
            tp_dev.y[0] = 0xffff;
            tp_dev.sta &= 0XE000;           /* 锟斤拷锟斤拷锟斤拷锟叫э拷锟斤拷 */
        }
    }

    if (t > 240)t = 10; /* 锟斤拷锟铰达拷10锟斤拷始锟斤拷锟斤拷 */

    return res;
}




























