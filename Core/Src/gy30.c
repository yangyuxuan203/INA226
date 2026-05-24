#include "gy30.h"
#include "delay.h"

/* Software I2C Low-level Functions */

static void GY30_IIC_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitStruct.Pin = GY30_SCL_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GY30_SCL_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GY30_SDA_PIN;
    HAL_GPIO_Init(GY30_SDA_PORT, &GPIO_InitStruct);

    GY30_SCL_HIGH();
    GY30_SDA_HIGH();
}

static void GY30_IIC_Delay(void)
{
    delay_us(5);
}

static void GY30_IIC_Start(void)
{
    GY30_SDA_HIGH();
    GY30_SCL_HIGH();
    GY30_IIC_Delay();
    GY30_SDA_LOW();
    GY30_IIC_Delay();
    GY30_SCL_LOW();
    GY30_IIC_Delay();
}

static void GY30_IIC_Stop(void)
{
    GY30_SDA_LOW();
    GY30_IIC_Delay();
    GY30_SCL_HIGH();
    GY30_IIC_Delay();
    GY30_SDA_HIGH();
    GY30_IIC_Delay();
}

static uint8_t GY30_IIC_WaitAck(void)
{
    uint8_t waittime = 0;

    GY30_SDA_HIGH();
    GY30_IIC_Delay();
    GY30_SCL_HIGH();
    GY30_IIC_Delay();

    while (GY30_SDA_READ())
    {
        waittime++;
        if (waittime > 250)
        {
            GY30_IIC_Stop();
            return 1;
        }
        GY30_IIC_Delay();
    }

    GY30_SCL_LOW();
    GY30_IIC_Delay();
    return 0;
}

static void GY30_IIC_Ack(void)
{
    GY30_SDA_LOW();
    GY30_IIC_Delay();
    GY30_SCL_HIGH();
    GY30_IIC_Delay();
    GY30_SCL_LOW();
    GY30_IIC_Delay();
    GY30_SDA_HIGH();
    GY30_IIC_Delay();
}

static void GY30_IIC_NAck(void)
{
    GY30_SDA_HIGH();
    GY30_IIC_Delay();
    GY30_SCL_HIGH();
    GY30_IIC_Delay();
    GY30_SCL_LOW();
    GY30_IIC_Delay();
}

static void GY30_IIC_WriteByte(uint8_t data)
{
    uint8_t t;
    for (t = 0; t < 8; t++)
    {
        if (data & 0x80)
            GY30_SDA_HIGH();
        else
            GY30_SDA_LOW();
        data <<= 1;
        GY30_IIC_Delay();
        GY30_SCL_HIGH();
        GY30_IIC_Delay();
        GY30_SCL_LOW();
        GY30_IIC_Delay();
    }
    GY30_SDA_HIGH();
}

static uint8_t GY30_IIC_ReadByte(uint8_t ack)
{
    uint8_t i, receive = 0;
    for (i = 0; i < 8; i++)
    {
        receive <<= 1;
        GY30_SCL_HIGH();
        GY30_IIC_Delay();
        if (GY30_SDA_READ())
            receive++;
        GY30_SCL_LOW();
        GY30_IIC_Delay();
    }
    if (!ack)
        GY30_IIC_NAck();
    else
        GY30_IIC_Ack();
    return receive;
}

/* BH1750 (GY30) Functions */

static uint8_t GY30_WriteCmd(uint8_t cmd)
{
    GY30_IIC_Start();
    GY30_IIC_WriteByte((GY30_ADDR << 1) | 0);
    if (GY30_IIC_WaitAck()) return 1;
    GY30_IIC_WriteByte(cmd);
    if (GY30_IIC_WaitAck()) return 1;
    GY30_IIC_Stop();
    return 0;
}

static uint16_t GY30_ReadRaw(void)
{
    uint16_t raw;

    GY30_IIC_Start();
    GY30_IIC_WriteByte((GY30_ADDR << 1) | 1);
    if (GY30_IIC_WaitAck()) return 0;

    raw = GY30_IIC_ReadByte(1);
    raw = (raw << 8) | GY30_IIC_ReadByte(0);
    GY30_IIC_Stop();
    return raw;
}

void GY30_Init(void)
{
    GY30_IIC_Init();
    delay_ms(10);

    GY30_WriteCmd(GY30_POWER_ON);
    delay_ms(10);

    GY30_WriteCmd(GY30_CONT_H_RES);
    delay_ms(180);  /* H-Resolution mode needs ~180ms for first measurement */
}

uint8_t GY30_ReadLight(float *lux)
{
    uint16_t raw;

    if (lux == NULL) return 1;

    raw = GY30_ReadRaw();
    if (raw == 0) return 1;

    /* BH1750 H-Resolution Mode: lux = raw / 1.2 */
    *lux = raw / 1.2f;
    return 0;
}
