#include "ina226_3.h"
#include "delay.h"
#include <stdio.h>

/* Software I2C Low-level Functions for INA226 #3 (PB13=SCL, PB12=SDA) */

static void INA226_3_IIC_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = INA226_3_SCL_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(INA226_3_SCL_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = INA226_3_SDA_PIN;
    HAL_GPIO_Init(INA226_3_SDA_PORT, &GPIO_InitStruct);

    INA226_3_SCL_HIGH();
    INA226_3_SDA_HIGH();
}

static void INA226_3_IIC_Delay(void) { delay_us(5); }

static void INA226_3_IIC_Start(void)
{
    INA226_3_SDA_HIGH();
    INA226_3_SCL_HIGH();
    INA226_3_IIC_Delay();
    INA226_3_SDA_LOW();
    INA226_3_IIC_Delay();
    INA226_3_SCL_LOW();
    INA226_3_IIC_Delay();
}

static void INA226_3_IIC_Stop(void)
{
    INA226_3_SDA_LOW();
    INA226_3_IIC_Delay();
    INA226_3_SCL_HIGH();
    INA226_3_IIC_Delay();
    INA226_3_SDA_HIGH();
    INA226_3_IIC_Delay();
}

static uint8_t INA226_3_IIC_WaitAck(void)
{
    uint8_t waittime = 0;
    INA226_3_SDA_HIGH();
    INA226_3_IIC_Delay();
    INA226_3_SCL_HIGH();
    INA226_3_IIC_Delay();
    while (INA226_3_SDA_READ())
    {
        if (++waittime > 250) { INA226_3_IIC_Stop(); return 1; }
        INA226_3_IIC_Delay();
    }
    INA226_3_SCL_LOW();
    INA226_3_IIC_Delay();
    return 0;
}

static void INA226_3_IIC_Ack(void)
{
    INA226_3_SDA_LOW();
    INA226_3_IIC_Delay();
    INA226_3_SCL_HIGH();
    INA226_3_IIC_Delay();
    INA226_3_SCL_LOW();
    INA226_3_IIC_Delay();
    INA226_3_SDA_HIGH();
    INA226_3_IIC_Delay();
}

static void INA226_3_IIC_NAck(void)
{
    INA226_3_SDA_HIGH();
    INA226_3_IIC_Delay();
    INA226_3_SCL_HIGH();
    INA226_3_IIC_Delay();
    INA226_3_SCL_LOW();
    INA226_3_IIC_Delay();
}

static void INA226_3_IIC_WriteByte(uint8_t data)
{
    uint8_t t;
    for (t = 0; t < 8; t++)
    {
        if (data & 0x80) INA226_3_SDA_HIGH();
        else             INA226_3_SDA_LOW();
        data <<= 1;
        INA226_3_IIC_Delay();
        INA226_3_SCL_HIGH();
        INA226_3_IIC_Delay();
        INA226_3_SCL_LOW();
        INA226_3_IIC_Delay();
    }
    INA226_3_SDA_HIGH();
}

static uint8_t INA226_3_IIC_ReadByte(uint8_t ack)
{
    uint8_t i, receive = 0;
    for (i = 0; i < 8; i++)
    {
        receive <<= 1;
        INA226_3_SCL_HIGH();
        INA226_3_IIC_Delay();
        if (INA226_3_SDA_READ()) receive++;
        INA226_3_SCL_LOW();
        INA226_3_IIC_Delay();
    }
    if (!ack) INA226_3_IIC_NAck();
    else      INA226_3_IIC_Ack();
    return receive;
}

static uint8_t INA226_3_WriteReg(uint8_t reg, uint16_t data)
{
    INA226_3_IIC_Start();
    INA226_3_IIC_WriteByte((INA226_3_ADDR << 1) | 0);
    if (INA226_3_IIC_WaitAck()) return 1;
    INA226_3_IIC_WriteByte(reg);
    if (INA226_3_IIC_WaitAck()) return 1;
    INA226_3_IIC_WriteByte((uint8_t)(data >> 8));
    if (INA226_3_IIC_WaitAck()) return 1;
    INA226_3_IIC_WriteByte((uint8_t)(data & 0xFF));
    if (INA226_3_IIC_WaitAck()) return 1;
    INA226_3_IIC_Stop();
    return 0;
}

static uint16_t INA226_3_ReadReg(uint8_t reg)
{
    uint16_t data;
    INA226_3_IIC_Start();
    INA226_3_IIC_WriteByte((INA226_3_ADDR << 1) | 0);
    if (INA226_3_IIC_WaitAck()) return 0;
    INA226_3_IIC_WriteByte(reg);
    if (INA226_3_IIC_WaitAck()) return 0;
    INA226_3_IIC_Start();
    INA226_3_IIC_WriteByte((INA226_3_ADDR << 1) | 1);
    if (INA226_3_IIC_WaitAck()) return 0;
    data = INA226_3_IIC_ReadByte(1);
    data = (data << 8) | INA226_3_IIC_ReadByte(0);
    INA226_3_IIC_Stop();
    return data;
}

void INA226_3_Init(void)
{
    INA226_3_IIC_Init();
    delay_ms(10);
    INA226_3_WriteReg(INA226_REG_CONFIG, INA226_CONFIG_RESET);
    delay_ms(10);
    INA226_3_WriteReg(INA226_REG_CONFIG, 0x0927);
    delay_ms(10);
    INA226_3_WriteReg(INA226_REG_CALIBRATION, 0x0066);
    delay_ms(10);
    printf("INA226_3: init done (PB3=SCL, PB2=SDA, addr=0x%02X)\r\n", INA226_3_ADDR);
}

/*
 * INA226 #3 calibration (R_shunt = 0.2Ω, Cal = 0x0066 = 102):
 *   Current_LSB = 0.00512 / (102 × 0.2) = 0.000251A = 251µA/bit
 *   Power_LSB = 25 × Current_LSB = 0.006275W = 6.275mW/bit
 */
#define INA226_3_CURRENT_LSB    0.000251f   /* A/bit */
#define INA226_3_POWER_LSB      0.006275f   /* W/bit */

uint8_t INA226_3_ReadData(INA226_Data *data)
{
    if (data == NULL) return 1;

    data->bus_voltage = INA226_3_ReadReg(INA226_REG_BUS_VOLT) * INA226_BUS_VOLT_LSB;
    data->shunt_voltage = (int16_t)INA226_3_ReadReg(INA226_REG_SHUNT_VOLT) * INA226_SHUNT_VOLT_LSB;
    data->current = (int16_t)INA226_3_ReadReg(INA226_REG_CURRENT) * INA226_3_CURRENT_LSB;
    data->power = INA226_3_ReadReg(INA226_REG_POWER) * INA226_3_POWER_LSB;

    return 0;
}
