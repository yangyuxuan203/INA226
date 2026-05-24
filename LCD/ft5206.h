/**
 ****************************************************************************************************
 * @file        ft5206.h
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
 
#ifndef __FT5206_H
#define __FT5206_H

#include "main.h"


/******************************************************************************************/
/* FT5206 INT 锟斤拷 RST 锟斤拷锟斤拷 锟斤拷锟斤拷 */

#define FT5206_RST_GPIO_PORT            GPIOC
#define FT5206_RST_GPIO_PIN             GPIO_PIN_13
#define FT5206_RST_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)   /* PC锟斤拷时锟斤拷使锟斤拷 */

#define FT5206_INT_GPIO_PORT            GPIOB
#define FT5206_INT_GPIO_PIN             GPIO_PIN_1
#define FT5206_INT_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* PB锟斤拷时锟斤拷使锟斤拷 */

/******************************************************************************************/

/* 锟斤拷锟斤拷荽锟斤拷锟斤拷锟斤拷锟斤拷拥锟叫酒拷锟斤拷锟�(未锟斤拷锟斤拷IIC锟斤拷锟斤拷) 
 * IO锟斤拷锟斤拷锟斤拷锟斤拷 
 */
#define FT5206_RST(x)     do{ x ? \
                              HAL_GPIO_WritePin(FT5206_RST_GPIO_PORT, FT5206_RST_GPIO_PIN, GPIO_PIN_SET) : \
                              HAL_GPIO_WritePin(FT5206_RST_GPIO_PORT, FT5206_RST_GPIO_PIN, GPIO_PIN_RESET); \
                          }while(0)       /* 锟斤拷位锟斤拷锟斤拷 */

#define FT5206_INT        HAL_GPIO_ReadPin(FT5206_INT_GPIO_PORT, FT5206_INT_GPIO_PIN)     /* 锟斤拷取锟斤拷锟斤拷锟斤拷锟斤拷 */

/* IIC锟斤拷写锟斤拷锟斤拷 */
#define FT5206_CMD_WR               0X70        /* 写锟斤拷锟斤拷(锟斤拷锟轿晃�0) */
#define FT5206_CMD_RD               0X71        /* 锟斤拷锟斤拷锟斤拷(锟斤拷锟轿晃�1) */

/* FT5206 锟斤拷锟街寄达拷锟斤拷锟斤拷锟斤拷  */
#define FT5206_DEVIDE_MODE          0x00        /* FT5206模式锟斤拷锟狡寄达拷锟斤拷 */
#define FT5206_REG_NUM_FINGER       0x02        /* 锟斤拷锟斤拷状态锟侥达拷锟斤拷 */

#define FT5206_TP1_REG              0X03        /* 锟斤拷一锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟捷碉拷址 */
#define FT5206_TP2_REG              0X09        /* 锟节讹拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟捷碉拷址 */
#define FT5206_TP3_REG              0X0F        /* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟捷碉拷址 */
#define FT5206_TP4_REG              0X15        /* 锟斤拷锟侥革拷锟斤拷锟斤拷锟斤拷锟斤拷锟捷碉拷址 */
#define FT5206_TP5_REG              0X1B        /* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷莸锟街� */ 


#define	FT5206_ID_G_LIB_VERSION     0xA1        /* 锟芥本 */
#define FT5206_ID_G_MODE            0xA4        /* FT5206锟叫讹拷模式锟斤拷锟狡寄达拷锟斤拷 */
#define FT5206_ID_G_THGROUP         0x80        /* 锟斤拷锟斤拷锟斤拷效值锟斤拷锟矫寄达拷锟斤拷 */
#define FT5206_ID_G_PERIODACTIVE    0x88        /* 锟斤拷锟斤拷状态锟斤拷锟斤拷锟斤拷锟矫寄达拷锟斤拷 */


uint8_t ft5206_wr_reg(uint16_t reg,uint8_t *buf,uint8_t len);
void ft5206_rd_reg(uint16_t reg,uint8_t *buf,uint8_t len);
uint8_t ft5206_init(void);
uint8_t ft5206_scan(uint8_t mode);

#endif

















