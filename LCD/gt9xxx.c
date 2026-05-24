/**
 ****************************************************************************************************
 * @file        gt9xxx.h
 * @author      锟斤拷锟斤拷原锟斤拷锟脚讹拷(ALIENTEK)
 * @version     V1.1
 * @date        2023-05-29
 * @brief       4.3锟斤拷锟斤拷荽锟斤拷锟斤拷锟�-GT9xxx 锟斤拷锟斤拷锟斤拷锟斤拷
 *   @note      GT系锟叫碉拷锟捷达拷锟斤拷锟斤拷IC通锟斤拷锟斤拷锟斤拷,锟斤拷锟斤拷锟斤拷支锟斤拷: GT9147/GT917S/GT968/GT1151/GT9271 锟饺讹拷锟斤拷
 *              锟斤拷锟斤拷IC, 锟斤拷些锟斤拷锟斤拷IC锟斤拷ID锟斤拷一锟斤拷, 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷要锟斤拷锟轿猴拷锟睫改硷拷锟斤拷通锟斤拷锟斤拷锟斤拷锟斤拷直锟斤拷锟斤拷锟斤拷
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
 * 1, 锟斤拷锟斤拷锟斤拷ST7796 3.5锟斤拷锟斤拷/ILI9806 4.3锟斤拷锟斤拷GT1151锟斤拷支锟斤拷
 * 2, gt9xxx_init锟斤拷锟斤拷锟斤拷锟斤拷锟接达拷锟斤拷IC锟叫讹拷锟斤拷锟斤拷锟斤拷锟斤拷锟截讹拷锟斤拷锟斤拷IC锟酵凤拷锟斤拷1锟斤拷示锟斤拷始锟斤拷失锟斤拷
 ****************************************************************************************************
 */

#include "string.h"
#include "lcd.h"
#include "touch.h"
#include "ctiic.h"
#include "gt9xxx.h"
#include "delay.h"
#include <stdio.h>


/* 注锟斤拷: 锟斤拷锟斤拷GT9271支锟斤拷10锟姐触锟斤拷之锟斤拷, 锟斤拷锟斤拷锟斤拷锟斤拷芯片只支锟斤拷 5锟姐触锟斤拷 */
uint8_t g_gt_tnum = 5;      /* 默锟斤拷支锟街的达拷锟斤拷锟斤拷锟斤拷锟斤拷(5锟姐触锟斤拷) */

/**
 * @brief       锟斤拷gt9xxx写锟斤拷一锟斤拷锟斤拷锟斤拷
 * @param       reg : 锟斤拷始锟侥达拷锟斤拷锟斤拷址
 * @param       buf : 锟斤拷锟捷伙拷锟斤拷锟斤拷锟斤拷
 * @param       len : 写锟斤拷锟捷筹拷锟斤拷
 * @retval      0, 锟缴癸拷; 1, 失锟斤拷;
 */
uint8_t gt9xxx_wr_reg(uint16_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;
    uint8_t ret = 0;

    ct_iic_start();
    ct_iic_send_byte(GT9XXX_CMD_WR);    /* 锟斤拷锟斤拷写锟斤拷锟斤拷 */
    ct_iic_wait_ack();
    ct_iic_send_byte(reg >> 8);         /* 锟斤拷锟酵革拷8位锟斤拷址 */
    ct_iic_wait_ack();
    ct_iic_send_byte(reg & 0XFF);       /* 锟斤拷锟酵碉拷8位锟斤拷址 */
    ct_iic_wait_ack();

    for (i = 0; i < len; i++)
    {
        ct_iic_send_byte(buf[i]);       /* 锟斤拷锟斤拷锟斤拷 */
        ret = ct_iic_wait_ack();

        if (ret) break;
    }

    ct_iic_stop();  /* 锟斤拷锟斤拷一锟斤拷停止锟斤拷锟斤拷 */
    return ret;
}

/**
 * @brief       锟斤拷gt9xxx锟斤拷锟斤拷一锟斤拷锟斤拷锟斤拷
 * @param       reg : 锟斤拷始锟侥达拷锟斤拷锟斤拷址
 * @param       buf : 锟斤拷锟捷伙拷锟斤拷锟斤拷锟斤拷
 * @param       len : 锟斤拷锟斤拷锟捷筹拷锟斤拷
 * @retval      锟斤拷
 */
void gt9xxx_rd_reg(uint16_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    ct_iic_start();
    ct_iic_send_byte(GT9XXX_CMD_WR);    /* 锟斤拷锟斤拷写锟斤拷锟斤拷 */
    ct_iic_wait_ack();
    ct_iic_send_byte(reg >> 8);         /* 锟斤拷锟酵革拷8位锟斤拷址 */
    ct_iic_wait_ack();
    ct_iic_send_byte(reg & 0XFF);       /* 锟斤拷锟酵碉拷8位锟斤拷址 */
    ct_iic_wait_ack();
    ct_iic_start();
    ct_iic_send_byte(GT9XXX_CMD_RD);    /* 锟斤拷锟酵讹拷锟斤拷锟斤拷 */
    ct_iic_wait_ack();

    for (i = 0; i < len; i++)
    {
        buf[i] = ct_iic_read_byte(i == (len - 1) ? 0 : 1);  /* 锟斤拷取锟斤拷锟斤拷 */
    }

    ct_iic_stop();  /* 锟斤拷锟斤拷一锟斤拷停止锟斤拷锟斤拷 */
}

/**
 * @brief       锟斤拷始锟斤拷gt9xxx锟斤拷锟斤拷锟斤拷
 * @param       锟斤拷
 * @retval      0, 锟斤拷始锟斤拷锟缴癸拷; 1, 锟斤拷始锟斤拷失锟斤拷;
 */
uint8_t gt9xxx_init(void)
{
    GPIO_InitTypeDef gpio_init_struct;
    uint8_t temp[5];

    GT9XXX_RST_GPIO_CLK_ENABLE();   /* RST锟斤拷锟斤拷时锟斤拷使锟斤拷 */
    GT9XXX_INT_GPIO_CLK_ENABLE();   /* INT锟斤拷锟斤拷时锟斤拷使锟斤拷 */

    gpio_init_struct.Pin = GT9XXX_RST_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;            /* 锟斤拷锟斤拷锟斤拷锟� */
    gpio_init_struct.Pull = GPIO_PULLUP;                    /* 锟斤拷锟斤拷 */
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;     /* 锟斤拷锟斤拷 */
    HAL_GPIO_Init(GT9XXX_RST_GPIO_PORT, &gpio_init_struct); /* 锟斤拷始锟斤拷RST锟斤拷锟斤拷 */

    gpio_init_struct.Pin = GT9XXX_INT_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_INPUT;                /* 锟斤拷锟斤拷 */
    gpio_init_struct.Pull = GPIO_PULLUP;                    /* 锟斤拷锟斤拷 */
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;     /* 锟斤拷锟斤拷 */
    HAL_GPIO_Init(GT9XXX_INT_GPIO_PORT, &gpio_init_struct); /* 锟斤拷始锟斤拷INT锟斤拷锟斤拷 */

    ct_iic_init();      /* 锟斤拷始锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷I2C锟斤拷锟斤拷 */
    GT9XXX_RST(0);      /* 锟斤拷位 */
    delay_ms(10);
    GT9XXX_RST(1);      /* 锟酵放革拷位 */
    delay_ms(10);

    /* INT锟斤拷锟斤拷模式锟斤拷锟斤拷, 锟斤拷锟斤拷模式, 锟斤拷锟斤拷锟斤拷锟斤拷 */
    gpio_init_struct.Pin = GT9XXX_INT_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_INPUT;                /* 锟斤拷锟斤拷 */
    gpio_init_struct.Pull = GPIO_NOPULL;                    /* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷模式 */
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;     /* 锟斤拷锟斤拷 */
    HAL_GPIO_Init(GT9XXX_INT_GPIO_PORT, &gpio_init_struct); /* 锟斤拷始锟斤拷INT锟斤拷锟斤拷 */

    delay_ms(100);
    gt9xxx_rd_reg(GT9XXX_PID_REG, temp, 4); /* 锟斤拷取锟斤拷锟斤拷IC锟斤拷ID */
    temp[4] = 0;
    
    /* 锟叫讹拷一锟斤拷锟角凤拷锟斤拷锟截讹拷锟侥达拷锟斤拷锟斤拷 */
    if (strcmp((char *)temp, "911") && strcmp((char *)temp, "9147") && strcmp((char *)temp, "1158") && strcmp((char *)temp, "9271"))
    {
        return 1;   /* 锟斤拷锟斤拷锟角达拷锟斤拷锟斤拷锟矫碉拷锟斤拷GT911/9147/1158/9271锟斤拷锟斤拷锟绞硷拷锟绞э拷埽锟斤拷锟接诧拷锟斤拷榭达拷锟斤拷锟絀C锟酵猴拷锟皆硷拷锟介看时锟斤拷锟斤拷锟角凤拷锟斤拷确 */
    }
    
    printf("CTP ID:%s\r\n", temp);          /* 锟斤拷印ID */
    
    if (strcmp((char *)temp, "9271") == 0)  /* ID==9271, 支锟斤拷10锟姐触锟斤拷 */
    {
         g_gt_tnum = 10;    /* 支锟斤拷10锟姐触锟斤拷锟斤拷 */
    }
    
    temp[0] = 0X02;
    gt9xxx_wr_reg(GT9XXX_CTRL_REG, temp, 1);    /* 锟斤拷锟斤拷位GT9XXX */
    
    delay_ms(10);
    
    temp[0] = 0X00;
    gt9xxx_wr_reg(GT9XXX_CTRL_REG, temp, 1);    /* 锟斤拷锟斤拷锟斤拷位, 锟斤拷锟斤拷锟斤拷锟斤拷锟阶刺� */

    return 0;
}

/* GT9XXX 10锟斤拷锟斤拷锟斤拷锟斤拷(锟斤拷锟�) 锟斤拷应锟侥寄达拷锟斤拷锟斤拷 */
const uint16_t GT9XXX_TPX_TBL[10] =
{
    GT9XXX_TP1_REG, GT9XXX_TP2_REG, GT9XXX_TP3_REG, GT9XXX_TP4_REG, GT9XXX_TP5_REG,
    GT9XXX_TP6_REG, GT9XXX_TP7_REG, GT9XXX_TP8_REG, GT9XXX_TP9_REG, GT9XXX_TP10_REG,
};

/**
 * @brief       扫锟借触锟斤拷锟斤拷(锟斤拷锟矫诧拷询锟斤拷式)
 * @param       mode : 锟斤拷锟斤拷锟斤拷未锟矫碉拷锟轿诧拷锟斤拷, 为锟剿硷拷锟捷碉拷锟斤拷锟斤拷
 * @retval      锟斤拷前锟斤拷锟斤拷状态
 *   @arg       0, 锟斤拷锟斤拷锟睫达拷锟斤拷; 
 *   @arg       1, 锟斤拷锟斤拷锟叫达拷锟斤拷;
 */
uint8_t gt9xxx_scan(uint8_t mode)
{
    uint8_t buf[4];
    uint8_t i = 0;
    uint8_t res = 0;
    uint16_t temp;
    uint16_t tempsta;
    static uint8_t t = 0;   /* 锟斤拷锟狡诧拷询锟斤拷锟�,锟接讹拷锟斤拷锟斤拷CPU占锟斤拷锟斤拷 */
    t++;

    if ((t % 10) == 0 || t < 10)    /* 锟斤拷锟斤拷时,每锟斤拷锟斤拷10锟斤拷CTP_Scan锟斤拷锟斤拷锟脚硷拷锟�1锟斤拷,锟接讹拷锟斤拷省CPU使锟斤拷锟斤拷 */
    {
        gt9xxx_rd_reg(GT9XXX_GSTID_REG, &mode, 1);  /* 锟斤拷取锟斤拷锟斤拷锟斤拷锟阶刺� */

        if ((mode & 0X80) && ((mode & 0XF) <= g_gt_tnum))
        {
            i = 0;
            gt9xxx_wr_reg(GT9XXX_GSTID_REG, &i, 1); /* 锟斤拷锟街� */
        }

        if ((mode & 0XF) && ((mode & 0XF) <= g_gt_tnum))
        {
            temp = 0XFFFF << (mode & 0XF);  /* 锟斤拷锟斤拷母锟斤拷锟阶拷锟轿�1锟斤拷位锟斤拷,匹锟斤拷tp_dev.sta锟斤拷锟斤拷 */
            tempsta = tp_dev.sta;           /* 锟斤拷锟芥当前锟斤拷tp_dev.sta值 */
            tp_dev.sta = (~temp) | TP_PRES_DOWN | TP_CATH_PRES;
            tp_dev.x[g_gt_tnum - 1] = tp_dev.x[0];  /* 锟斤拷锟芥触锟斤拷0锟斤拷锟斤拷锟斤拷,锟斤拷锟斤拷锟斤拷锟斤拷锟揭伙拷锟斤拷锟� */
            tp_dev.y[g_gt_tnum - 1] = tp_dev.y[0];

            for (i = 0; i < g_gt_tnum; i++)
            {
                if (tp_dev.sta & (1 << i))  /* 锟斤拷锟斤拷锟斤拷效? */
                {
                    gt9xxx_rd_reg(GT9XXX_TPX_TBL[i], buf, 4);   /* 锟斤拷取XY锟斤拷锟斤拷值 */

                    if (lcddev.id == 0X5510 || lcddev.id == 0X9806 || lcddev.id == 0X7796)     /* 4.3锟斤拷800*480 锟斤拷 3.5锟斤拷480*320 MCU锟斤拷 */
                    {
                        if (tp_dev.touchtype & 0X01)    /* 锟斤拷锟斤拷 */
                        {
                            tp_dev.x[i] = lcddev.width - (((uint16_t)buf[3] << 8) + buf[2]);
                            tp_dev.y[i] = ((uint16_t)buf[1] << 8) + buf[0];
                        }
                        else
                        {
                            tp_dev.x[i] = ((uint16_t)buf[1] << 8) + buf[0];
                            tp_dev.y[i] = ((uint16_t)buf[3] << 8) + buf[2];
                        }
                    }
                    else    /* 锟斤拷锟斤拷锟酵猴拷 */
                    {
                        if (tp_dev.touchtype & 0X01)    /* 锟斤拷锟斤拷 */
                        {
                            tp_dev.x[i] = ((uint16_t)buf[1] << 8) + buf[0];
                            tp_dev.y[i] = ((uint16_t)buf[3] << 8) + buf[2];
                        }
                        else
                        {
                            tp_dev.x[i] = lcddev.width - (((uint16_t)buf[3] << 8) + buf[2]);
                            tp_dev.y[i] = ((uint16_t)buf[1] << 8) + buf[0];
                        }
                    }

                    //printf("x[%d]:%d,y[%d]:%d\r\n", i, tp_dev.x[i], i, tp_dev.y[i]);
                }
            }

            res = 1;

            if (tp_dev.x[0] > lcddev.width || tp_dev.y[0] > lcddev.height)  /* 锟角凤拷锟斤拷锟斤拷(锟斤拷锟疥超锟斤拷锟斤拷) */
            {
                if ((mode & 0XF) > 1)   /* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷,锟津复第讹拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷莸锟斤拷锟揭伙拷锟斤拷锟斤拷锟�. */
                {
                    tp_dev.x[0] = tp_dev.x[1];
                    tp_dev.y[0] = tp_dev.y[1];
                    t = 0;  /* 锟斤拷锟斤拷一锟斤拷,锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷10锟斤拷,锟接讹拷锟斤拷锟斤拷锟斤拷锟斤拷锟� */
                }
                else        /* 锟角凤拷锟斤拷锟斤拷,锟斤拷锟斤拷源舜锟斤拷锟斤拷锟�(锟斤拷原原锟斤拷锟斤拷) */
                {
                    tp_dev.x[0] = tp_dev.x[g_gt_tnum - 1];
                    tp_dev.y[0] = tp_dev.y[g_gt_tnum - 1];
                    mode = 0X80;
                    tp_dev.sta = tempsta;   /* 锟街革拷tp_dev.sta */
                }
            }
            else 
            {
                t = 0;      /* 锟斤拷锟斤拷一锟斤拷,锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷10锟斤拷,锟接讹拷锟斤拷锟斤拷锟斤拷锟斤拷锟� */
            }
        }
    }

    if ((mode & 0X8F) == 0X80)  /* 锟睫达拷锟斤拷锟姐按锟斤拷 */
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




























