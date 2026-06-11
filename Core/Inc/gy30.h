#ifndef __GY30_H
#define __GY30_H

#include "main.h"

/* BH1750 (GY30) I2C Address */
#define GY30_ADDR           0X23    /* ADDR pin = floating/VCC */

/* BH1750 Instruction Set */
#define GY30_POWER_OFF      0x00
#define GY30_POWER_ON       0x01
#define GY30_RESET          0x07
#define GY30_CONT_H_RES     0x10    /* Continuous H-Resolution Mode */
#define GY30_CONT_H_RES2    0x11    /* Continuous H-Resolution Mode 2 */
#define GY30_CONT_L_RES     0x13    /* Continuous L-Resolution Mode */
#define GY30_ONE_H_RES      0x20    /* One-Time H-Resolution Mode */
#define GY30_ONE_H_RES2     0x21    /* One-Time H-Resolution Mode 2 */
#define GY30_ONE_L_RES      0x23    /* One-Time L-Resolution Mode */

/* Software I2C Pin Definitions (PC3=SCL, PC4=SDA) */
#define GY30_SCL_PORT       GPIOC
#define GY30_SCL_PIN        GPIO_PIN_3
#define GY30_SDA_PORT       GPIOC
#define GY30_SDA_PIN        GPIO_PIN_4

/* IO Operations */
#define GY30_SCL_HIGH()     HAL_GPIO_WritePin(GY30_SCL_PORT, GY30_SCL_PIN, GPIO_PIN_SET)
#define GY30_SCL_LOW()      HAL_GPIO_WritePin(GY30_SCL_PORT, GY30_SCL_PIN, GPIO_PIN_RESET)
#define GY30_SDA_HIGH()     HAL_GPIO_WritePin(GY30_SDA_PORT, GY30_SDA_PIN, GPIO_PIN_SET)
#define GY30_SDA_LOW()      HAL_GPIO_WritePin(GY30_SDA_PORT, GY30_SDA_PIN, GPIO_PIN_RESET)
#define GY30_SDA_READ()     HAL_GPIO_ReadPin(GY30_SDA_PORT, GY30_SDA_PIN)

/* Function Prototypes */
void GY30_Init(void);
uint8_t GY30_ReadLight(float *lux);

#endif
