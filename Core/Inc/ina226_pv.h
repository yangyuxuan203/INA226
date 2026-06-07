#ifndef __INA226_PV_H
#define __INA226_PV_H

#include "main.h"

/* INA226 PV I2C Address (A0=VCC, A1=GND → 0x41) */
#define INA226_PV_ADDR          0x41

/* Software I2C Pin Definitions (PB1=SCL, PB2=SDA) */
#define INA226_PV_SCL_PORT      GPIOB
#define INA226_PV_SCL_PIN       GPIO_PIN_1
#define INA226_PV_SDA_PORT      GPIOB
#define INA226_PV_SDA_PIN       GPIO_PIN_2

/* IO Operations */
#define INA226_PV_SCL_HIGH()    HAL_GPIO_WritePin(INA226_PV_SCL_PORT, INA226_PV_SCL_PIN, GPIO_PIN_SET)
#define INA226_PV_SCL_LOW()     HAL_GPIO_WritePin(INA226_PV_SCL_PORT, INA226_PV_SCL_PIN, GPIO_PIN_RESET)
#define INA226_PV_SDA_HIGH()    HAL_GPIO_WritePin(INA226_PV_SDA_PORT, INA226_PV_SDA_PIN, GPIO_PIN_SET)
#define INA226_PV_SDA_LOW()     HAL_GPIO_WritePin(INA226_PV_SDA_PORT, INA226_PV_SDA_PIN, GPIO_PIN_RESET)
#define INA226_PV_SDA_READ()    HAL_GPIO_ReadPin(INA226_PV_SDA_PORT, INA226_PV_SDA_PIN)

/* Reuse INA226 register defines from ina226.h */
#include "ina226.h"

/* Function Prototypes */
void INA226_PV_Init(void);
uint8_t INA226_PV_ReadData(INA226_Data *data);

#endif
