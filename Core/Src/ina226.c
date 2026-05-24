#include "ina226.h"
#include "delay.h"

/* Software I2C Low-level Functions */

static void INA226_IIC_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = INA226_SCL_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(INA226_SCL_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = INA226_SDA_PIN;
    HAL_GPIO_Init(INA226_SDA_PORT, &GPIO_InitStruct);

    INA226_SCL_HIGH();
    INA226_SDA_HIGH();
}

static void INA226_IIC_Delay(void)
{
    delay_us(5);
}

static void INA226_IIC_Start(void)
{
    INA226_SDA_HIGH();
    INA226_SCL_HIGH();
    INA226_IIC_Delay();
    INA226_SDA_LOW();
    INA226_IIC_Delay();
    INA226_SCL_LOW();
    INA226_IIC_Delay();
}

static void INA226_IIC_Stop(void)
{
    INA226_SDA_LOW();
    INA226_IIC_Delay();
    INA226_SCL_HIGH();
    INA226_IIC_Delay();
    INA226_SDA_HIGH();
    INA226_IIC_Delay();
}

static uint8_t INA226_IIC_WaitAck(void)
{
    uint8_t waittime = 0;

    INA226_SDA_HIGH();
    INA226_IIC_Delay();
    INA226_SCL_HIGH();
    INA226_IIC_Delay();

    while (INA226_SDA_READ())
    {
        waittime++;
        if (waittime > 250)
        {
            INA226_IIC_Stop();
            return 1;
        }
        INA226_IIC_Delay();
    }

    INA226_SCL_LOW();
    INA226_IIC_Delay();
    return 0;
}

static void INA226_IIC_Ack(void)
{
    INA226_SDA_LOW();
    INA226_IIC_Delay();
    INA226_SCL_HIGH();
    INA226_IIC_Delay();
    INA226_SCL_LOW();
    INA226_IIC_Delay();
    INA226_SDA_HIGH();
    INA226_IIC_Delay();
}

static void INA226_IIC_NAck(void)
{
    INA226_SDA_HIGH();
    INA226_IIC_Delay();
    INA226_SCL_HIGH();
    INA226_IIC_Delay();
    INA226_SCL_LOW();
    INA226_IIC_Delay();
}

static void INA226_IIC_WriteByte(uint8_t data)
{
    uint8_t t;
    for (t = 0; t < 8; t++)
    {
        if (data & 0x80)
            INA226_SDA_HIGH();
        else
            INA226_SDA_LOW();
        data <<= 1;
        INA226_IIC_Delay();
        INA226_SCL_HIGH();
        INA226_IIC_Delay();
        INA226_SCL_LOW();
        INA226_IIC_Delay();
    }
    INA226_SDA_HIGH();
}

static uint8_t INA226_IIC_ReadByte(uint8_t ack)
{
    uint8_t i, receive = 0;
    for (i = 0; i < 8; i++)
    {
        receive <<= 1;
        INA226_SCL_HIGH();
        INA226_IIC_Delay();
        if (INA226_SDA_READ())
            receive++;
        INA226_SCL_LOW();
        INA226_IIC_Delay();
    }
    if (!ack)
        INA226_IIC_NAck();
    else
        INA226_IIC_Ack();
    return receive;
}

/* INA226 Register Read/Write */

static uint8_t INA226_WriteReg(uint8_t reg, uint16_t data)
{
    INA226_IIC_Start();
    INA226_IIC_WriteByte((INA226_ADDR << 1) | 0);
    if (INA226_IIC_WaitAck()) return 1;
    INA226_IIC_WriteByte(reg);
    if (INA226_IIC_WaitAck()) return 1;
    INA226_IIC_WriteByte((uint8_t)(data >> 8));
    if (INA226_IIC_WaitAck()) return 1;
    INA226_IIC_WriteByte((uint8_t)(data & 0xFF));
    if (INA226_IIC_WaitAck()) return 1;
    INA226_IIC_Stop();
    return 0;
}

static uint16_t INA226_ReadReg(uint8_t reg)
{
    uint16_t data;

    INA226_IIC_Start();
    INA226_IIC_WriteByte((INA226_ADDR << 1) | 0);
    if (INA226_IIC_WaitAck()) return 0;
    INA226_IIC_WriteByte(reg);
    if (INA226_IIC_WaitAck()) return 0;

    INA226_IIC_Start();
    INA226_IIC_WriteByte((INA226_ADDR << 1) | 1);
    if (INA226_IIC_WaitAck()) return 0;

    data = INA226_IIC_ReadByte(1);
    data = (data << 8) | INA226_IIC_ReadByte(0);
    INA226_IIC_Stop();
    return data;
}

/* Public Functions */

void INA226_Init(void)
{
    INA226_IIC_Init();
    delay_ms(10);

    /* Reset the device */
    INA226_WriteReg(INA226_REG_CONFIG, INA226_CONFIG_RESET);
    delay_ms(10);

    /*
     * Configuration Register (0x00):
     * [11:9]  AVG    = 0b100 (16 samples averaging)
     * [8:6]  VBUSCT  = 0b100 (1.1ms bus voltage conversion time)
     * [5:3]  VSHCT   = 0b100 (1.1ms shunt voltage conversion time)
     * [2:0]  MODE    = 0b111 (shunt and bus, continuous)
     *
     * Value = 0b0000_1001_0010_0111 = 0x0927
     */
    INA226_WriteReg(INA226_REG_CONFIG, 0x0927);
    delay_ms(10);

    /*
     * Calibration Register:
     * Current_LSB = 0.5mA (for a 0.1 ohm shunt resistor)
     * Cal = 0.00512 / (Current_LSB * R_shunt)
     * Cal = 0.00512 / (0.0005 * 0.1) = 102.4 -> 102 (0x0066)
     */
    INA226_WriteReg(INA226_REG_CALIBRATION, 0x0066);
    delay_ms(10);
}

static int16_t INA226_ReadShuntVoltage(void)
{
    return (int16_t)INA226_ReadReg(INA226_REG_SHUNT_VOLT);
}

static uint16_t INA226_ReadBusVoltageRaw(void)
{
    return INA226_ReadReg(INA226_REG_BUS_VOLT);
}

static int16_t INA226_ReadCurrentRaw(void)
{
    return (int16_t)INA226_ReadReg(INA226_REG_CURRENT);
}

static uint16_t INA226_ReadPowerRaw(void)
{
    return INA226_ReadReg(INA226_REG_POWER);
}

float INA226_ReadBusVoltage(void)
{
    uint16_t raw = INA226_ReadBusVoltageRaw();
    return raw * INA226_BUS_VOLT_LSB;
}

float INA226_ReadCurrent(void)
{
    int16_t raw = INA226_ReadCurrentRaw();
    /* Current_LSB = 0.5mA based on calibration value 102 */
    return raw * 0.0005f;
}

float INA226_ReadPower(void)
{
    uint16_t raw = INA226_ReadPowerRaw();
    /* Power_LSB = 25 * Current_LSB = 25 * 0.5mA = 12.5mW */
    return raw * 0.0125f;
}

uint8_t INA226_ReadData(INA226_Data *data)
{
    if (data == NULL) return 1;

    data->bus_voltage = INA226_ReadBusVoltage();
    data->shunt_voltage = INA226_ReadShuntVoltage() * INA226_SHUNT_VOLT_LSB;
    data->current = INA226_ReadCurrent();
    data->power = INA226_ReadPower();
    return 0;
}

uint16_t INA226_ReadManufacturerID(void)
{
    return INA226_ReadReg(INA226_REG_MANUFACTURER);
}

uint16_t INA226_ReadRawReg(uint8_t reg)
{
    return INA226_ReadReg(reg);
}
