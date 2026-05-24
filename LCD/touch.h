/**
 ****************************************************************************************************
 * @file        touch.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.1
 * @date        2023-05-29
 * @brief       触摸屏 驱动代码
 *   @note      支持电阻式/电容式触摸屏
 *
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 探索者 F407开发板
 * 论坛    :www.openedv.com
 * 公司地址:www.alientek.com
 * 淘宝地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#ifndef __TOUCH_H
#define __TOUCH_H

#include "stdlib.h"
#include "main.h"


/******************************************************************************************/
/* T_PEN/T_CS/T_MISO/T_MOSI/T_CLK 引脚定义 */

/* T_PEN (PC1)  中断脚 */
#define T_PEN_GPIO_PORT             GPIOC
#define T_PEN_GPIO_PIN              GPIO_PIN_1
#define T_PEN_GPIO_CLK_ENABLE()     do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)

/* T_CS (PC2)   片选脚 */
#define T_CS_GPIO_PORT              GPIOC
#define T_CS_GPIO_PIN               GPIO_PIN_2
#define T_CS_GPIO_CLK_ENABLE()      do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)

/* T_MISO (PB2) SPI_MISO */
#define T_MISO_GPIO_PORT            GPIOB
#define T_MISO_GPIO_PIN             GPIO_PIN_2
#define T_MISO_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)

/* T_MOSI (PF11) SPI_MOSI */
#define T_MOSI_GPIO_PORT            GPIOF
#define T_MOSI_GPIO_PIN             GPIO_PIN_11
#define T_MOSI_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOF_CLK_ENABLE(); }while(0)

/* T_CLK (PB0)  SPI_CLK */
#define T_CLK_GPIO_PORT             GPIOB
#define T_CLK_GPIO_PIN              GPIO_PIN_0
#define T_CLK_GPIO_CLK_ENABLE()     do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)

/******************************************************************************************/
/* IO操作函数定义 */

#define T_PEN       HAL_GPIO_ReadPin(T_PEN_GPIO_PORT, T_PEN_GPIO_PIN)
#define T_MISO      HAL_GPIO_ReadPin(T_MISO_GPIO_PORT, T_MISO_GPIO_PIN)

#define T_MOSI(x)   do{ x ? \
                        HAL_GPIO_WritePin(T_MOSI_GPIO_PORT, T_MOSI_GPIO_PIN, GPIO_PIN_SET) : \
                        HAL_GPIO_WritePin(T_MOSI_GPIO_PORT, T_MOSI_GPIO_PIN, GPIO_PIN_RESET); \
                    }while(0)

#define T_CLK(x)    do{ x ? \
                        HAL_GPIO_WritePin(T_CLK_GPIO_PORT, T_CLK_GPIO_PIN, GPIO_PIN_SET) : \
                        HAL_GPIO_WritePin(T_CLK_GPIO_PORT, T_CLK_GPIO_PIN, GPIO_PIN_RESET); \
                    }while(0)

#define T_CS(x)     do{ x ? \
                        HAL_GPIO_WritePin(T_CS_GPIO_PORT, T_CS_GPIO_PIN, GPIO_PIN_SET) : \
                        HAL_GPIO_WritePin(T_CS_GPIO_PORT, T_CS_GPIO_PIN, GPIO_PIN_RESET); \
                    }while(0)

/******************************************************************************************/

/* 触摸屏状态 */
#define TP_PRES_DOWN    0x8000      /* 触屏被按下 */
#define TP_CATH_PRES    0x4000      /* 有按键按下了 */

/* 最大支持的触点数 */
#define CT_MAX_TOUCH    5           /* 电容触摸屏最大支持5点触摸 */

/* 触摸屏控制器 */
typedef struct
{
    uint8_t (*init)(void);          /* 初始化触摸屏控制器 */
    uint8_t (*scan)(uint8_t mode);  /* 扫描触摸屏.0,屏幕扫描; 1,物理坐标; */
    void (*adjust)(void);           /* 触摸屏校准 */
    float xfac;                     /* 触摸屏X方向校准参数(电阻屏) */
    float yfac;                     /* 触摸屏Y方向校准参数(电阻屏) */
    short xc;                       /* 中心坐标X(电阻屏) */
    short yc;                       /* 中心坐标Y(电阻屏) */
    uint16_t x[CT_MAX_TOUCH];      /* 当前坐标 */
    uint16_t y[CT_MAX_TOUCH];      /* 当前坐标 */
    uint16_t sta;                   /* 笔的状态 */
    uint8_t touchtype;              /* 0XFF:电容屏  0X00:电阻屏 */
} _m_tp_dev;

extern _m_tp_dev tp_dev;

/* 函数声明 */
uint8_t tp_init(void);
uint8_t tp_scan(uint8_t mode);
void tp_adjust(void);
void tp_save_adjust_data(void);
uint8_t tp_get_adjust_data(void);
void tp_draw_big_point(uint16_t x, uint16_t y, uint16_t color);

#endif
