#include "ina226_3.h"
#include "delay.h"
#include <stdio.h>

#define INA226_3_VERBOSE_LOG 0U

/* Software I2C low-level functions for INA226 #3 (PC11=SCL, PC12=SDA) */

static uint8_t s_ina226_3_addr = INA226_3_ADDR;

static void INA226_3_IIC_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

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

static uint8_t INA226_3_ProbeAddr(uint8_t addr)
{
    uint8_t failed;

    INA226_3_IIC_Start();
    INA226_3_IIC_WriteByte((addr << 1) | 0);
    failed = INA226_3_IIC_WaitAck();
    INA226_3_IIC_Stop();

    return failed ? 1U : 0U;
}

static uint8_t INA226_3_WriteReg(uint8_t reg, uint16_t data)
{
    INA226_3_IIC_Start();
    INA226_3_IIC_WriteByte((s_ina226_3_addr << 1) | 0);
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

static uint8_t INA226_3_ReadReg(uint8_t reg, uint16_t *data)
{
    uint16_t value;

    if (data == NULL) return 1;

    INA226_3_IIC_Start();
    INA226_3_IIC_WriteByte((s_ina226_3_addr << 1) | 0);
    if (INA226_3_IIC_WaitAck()) return 1;
    INA226_3_IIC_WriteByte(reg);
    if (INA226_3_IIC_WaitAck()) return 1;
    INA226_3_IIC_Start();
    INA226_3_IIC_WriteByte((s_ina226_3_addr << 1) | 1);
    if (INA226_3_IIC_WaitAck()) return 1;
    value = INA226_3_IIC_ReadByte(1);
    value = (value << 8) | INA226_3_IIC_ReadByte(0);
    INA226_3_IIC_Stop();
    *data = value;
    return 0;
}

void INA226_3_Init(void)
{
    uint8_t init_failed = 0;

    INA226_3_IIC_Init();
    delay_ms(10);
    init_failed |= INA226_3_WriteReg(INA226_REG_CONFIG, INA226_CONFIG_RESET);
    delay_ms(10);
    init_failed |= INA226_3_WriteReg(INA226_REG_CONFIG, 0x0927);
    delay_ms(10);
    init_failed |= INA226_3_WriteReg(INA226_REG_CALIBRATION, 0x00CC);
    delay_ms(10);

    if (init_failed)
    {
        for (uint8_t addr = 0x40; addr <= 0x4F; addr++)
        {
            if (INA226_3_ProbeAddr(addr) == 0)
            {
                s_ina226_3_addr = addr;
                init_failed = 0;
                init_failed |= INA226_3_WriteReg(INA226_REG_CONFIG, INA226_CONFIG_RESET);
                delay_ms(10);
                init_failed |= INA226_3_WriteReg(INA226_REG_CONFIG, 0x0927);
                delay_ms(10);
                init_failed |= INA226_3_WriteReg(INA226_REG_CALIBRATION, 0x00CC);
                delay_ms(10);
                break;
            }
        }
    }

    if (INA226_3_VERBOSE_LOG)
    {
        printf("INA226_3: init %s (PC11=SCL, PC12=SDA, addr=0x%02X)\r\n",
               init_failed ? "failed" : "done", s_ina226_3_addr);
    }
}

/*
 * INA226 #3 calibration (R_shunt = 0.1 ohm, Cal = 0x00CC = 204):
 *   Current_LSB = 0.00512 / (204 * 0.1) = 0.000251A = 251uA/bit
 *   Power_LSB = 25 * Current_LSB = 0.006275W = 6.275mW/bit
 */
#define INA226_3_CURRENT_LSB    0.000251f   /* A/bit */
#define INA226_3_POWER_LSB      0.006275f   /* W/bit */

uint8_t INA226_3_ReadData(INA226_Data *data)
{
    uint16_t raw_bus;
    uint16_t raw_shunt;
    uint16_t raw_current;
    uint16_t raw_power;

    if (data == NULL) return 1;

    if (INA226_3_ReadReg(INA226_REG_BUS_VOLT, &raw_bus) != 0) return 1;
    if (INA226_3_ReadReg(INA226_REG_SHUNT_VOLT, &raw_shunt) != 0) return 1;
    if (INA226_3_ReadReg(INA226_REG_CURRENT, &raw_current) != 0) return 1;
    if (INA226_3_ReadReg(INA226_REG_POWER, &raw_power) != 0) return 1;

    data->bus_voltage = raw_bus * INA226_BUS_VOLT_LSB;
    data->shunt_voltage = (int16_t)raw_shunt * INA226_SHUNT_VOLT_LSB;
    data->current = (int16_t)raw_current * INA226_3_CURRENT_LSB;
    data->power = raw_power * INA226_3_POWER_LSB;

    return 0;
}
