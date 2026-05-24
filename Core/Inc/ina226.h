#ifndef __INA226_H
#define __INA226_H

#include "main.h"

/* INA226 I2C Address (default, A0=GND, A1=GND) */
#define INA226_ADDR             0x40

/* INA226 Register Addresses */
#define INA226_REG_CONFIG       0x00
#define INA226_REG_SHUNT_VOLT   0x01
#define INA226_REG_BUS_VOLT     0x02
#define INA226_REG_POWER        0x03
#define INA226_REG_CURRENT      0x04
#define INA226_REG_CALIBRATION  0x05
#define INA226_REG_MASK_ENABLE  0x06
#define INA226_REG_ALERT_LIMIT  0x07
#define INA226_REG_MANUFACTURER 0xFE
#define INA226_REG_DIE_ID       0xFF

/* Configuration Register Bits */
#define INA226_CONFIG_RESET     0x8000

/* Shunt voltage LSB: 2.5uV */
#define INA226_SHUNT_VOLT_LSB   0.0000025f
/* Bus voltage LSB: 1.25mV */
#define INA226_BUS_VOLT_LSB     0.00125f

/* Software I2C Pin Definitions */
#define INA226_SCL_PORT         GPIOB
#define INA226_SCL_PIN          GPIO_PIN_6
#define INA226_SDA_PORT         GPIOB
#define INA226_SDA_PIN          GPIO_PIN_7

/* IO Operations */
#define INA226_SCL_HIGH()       HAL_GPIO_WritePin(INA226_SCL_PORT, INA226_SCL_PIN, GPIO_PIN_SET)
#define INA226_SCL_LOW()        HAL_GPIO_WritePin(INA226_SCL_PORT, INA226_SCL_PIN, GPIO_PIN_RESET)
#define INA226_SDA_HIGH()       HAL_GPIO_WritePin(INA226_SDA_PORT, INA226_SDA_PIN, GPIO_PIN_SET)
#define INA226_SDA_LOW()        HAL_GPIO_WritePin(INA226_SDA_PORT, INA226_SDA_PIN, GPIO_PIN_RESET)
#define INA226_SDA_READ()       HAL_GPIO_ReadPin(INA226_SDA_PORT, INA226_SDA_PIN)

/* INA226 Data Structure */
typedef struct {
    float bus_voltage;      /* Bus voltage in V */
    float shunt_voltage;    /* Shunt voltage in V */
    float current;          /* Current in A */
    float power;            /* Power in W */
} INA226_Data;

/* Function Prototypes */
void INA226_Init(void);
uint8_t INA226_ReadData(INA226_Data *data);
float INA226_ReadBusVoltage(void);
float INA226_ReadCurrent(void);
float INA226_ReadPower(void);
uint16_t INA226_ReadManufacturerID(void);
uint16_t INA226_ReadRawReg(uint8_t reg);

#endif
