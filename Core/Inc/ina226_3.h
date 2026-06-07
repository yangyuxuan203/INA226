#ifndef __INA226_3_H
#define __INA226_3_H

#include "main.h"

/* INA226 #3 I2C Address (A0=0, A1=1 → 0x44) */
#define INA226_3_ADDR           0x44

/* Software I2C Pin Definitions (PB13=SCL, PB12=SDA) */
#define INA226_3_SCL_PORT       GPIOB
#define INA226_3_SCL_PIN        GPIO_PIN_13
#define INA226_3_SDA_PORT       GPIOB
#define INA226_3_SDA_PIN        GPIO_PIN_12

/* IO Operations */
#define INA226_3_SCL_HIGH()     HAL_GPIO_WritePin(INA226_3_SCL_PORT, INA226_3_SCL_PIN, GPIO_PIN_SET)
#define INA226_3_SCL_LOW()      HAL_GPIO_WritePin(INA226_3_SCL_PORT, INA226_3_SCL_PIN, GPIO_PIN_RESET)
#define INA226_3_SDA_HIGH()     HAL_GPIO_WritePin(INA226_3_SDA_PORT, INA226_3_SDA_PIN, GPIO_PIN_SET)
#define INA226_3_SDA_LOW()      HAL_GPIO_WritePin(INA226_3_SDA_PORT, INA226_3_SDA_PIN, GPIO_PIN_RESET)
#define INA226_3_SDA_READ()     HAL_GPIO_ReadPin(INA226_3_SDA_PORT, INA226_3_SDA_PIN)

/* Reuse INA226 register defines from ina226.h */
#include "ina226.h"

/* Function Prototypes */
void INA226_3_Init(void);
uint8_t INA226_3_ReadData(INA226_Data *data);

#endif
