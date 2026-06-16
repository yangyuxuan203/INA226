#include "ina226_pv.h"
#include "delay.h"
#include <stdio.h>

#define INA226_PV_VERBOSE_LOG 0U

/* Debug: print raw register values on first few reads */
static uint8_t pv_dbg_cnt = 3;

/* Software I2C low-level functions for PV INA226 (PB13=SCL, PB12=SDA) */

static void INA226_PV_IIC_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = INA226_PV_SCL_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(INA226_PV_SCL_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = INA226_PV_SDA_PIN;
    HAL_GPIO_Init(INA226_PV_SDA_PORT, &GPIO_InitStruct);

    INA226_PV_SCL_HIGH();
    INA226_PV_SDA_HIGH();
}

static void INA226_PV_IIC_Delay(void)
{
    delay_us(5);
}

static void INA226_PV_IIC_Start(void)
{
    INA226_PV_SDA_HIGH();
    INA226_PV_SCL_HIGH();
    INA226_PV_IIC_Delay();
    INA226_PV_SDA_LOW();
    INA226_PV_IIC_Delay();
    INA226_PV_SCL_LOW();
    INA226_PV_IIC_Delay();
}

static void INA226_PV_IIC_Stop(void)
{
    INA226_PV_SDA_LOW();
    INA226_PV_IIC_Delay();
    INA226_PV_SCL_HIGH();
    INA226_PV_IIC_Delay();
    INA226_PV_SDA_HIGH();
    INA226_PV_IIC_Delay();
}

static uint8_t INA226_PV_IIC_WaitAck(void)
{
    uint8_t waittime = 0;

    INA226_PV_SDA_HIGH();
    INA226_PV_IIC_Delay();
    INA226_PV_SCL_HIGH();
    INA226_PV_IIC_Delay();

    while (INA226_PV_SDA_READ())
    {
        waittime++;
        if (waittime > 250)
        {
            INA226_PV_IIC_Stop();
            return 1;
        }
        INA226_PV_IIC_Delay();
    }

    INA226_PV_SCL_LOW();
    INA226_PV_IIC_Delay();
    return 0;
}

static void INA226_PV_IIC_Ack(void)
{
    INA226_PV_SDA_LOW();
    INA226_PV_IIC_Delay();
    INA226_PV_SCL_HIGH();
    INA226_PV_IIC_Delay();
    INA226_PV_SCL_LOW();
    INA226_PV_IIC_Delay();
    INA226_PV_SDA_HIGH();
    INA226_PV_IIC_Delay();
}

static void INA226_PV_IIC_NAck(void)
{
    INA226_PV_SDA_HIGH();
    INA226_PV_IIC_Delay();
    INA226_PV_SCL_HIGH();
    INA226_PV_IIC_Delay();
    INA226_PV_SCL_LOW();
    INA226_PV_IIC_Delay();
}

static void INA226_PV_IIC_WriteByte(uint8_t data)
{
    uint8_t t;
    for (t = 0; t < 8; t++)
    {
        if (data & 0x80)
            INA226_PV_SDA_HIGH();
        else
            INA226_PV_SDA_LOW();
        data <<= 1;
        INA226_PV_IIC_Delay();
        INA226_PV_SCL_HIGH();
        INA226_PV_IIC_Delay();
        INA226_PV_SCL_LOW();
        INA226_PV_IIC_Delay();
    }
    INA226_PV_SDA_HIGH();
}

static uint8_t INA226_PV_IIC_ReadByte(uint8_t ack)
{
    uint8_t i, receive = 0;
    for (i = 0; i < 8; i++)
    {
        receive <<= 1;
        INA226_PV_SCL_HIGH();
        INA226_PV_IIC_Delay();
        if (INA226_PV_SDA_READ())
            receive++;
        INA226_PV_SCL_LOW();
        INA226_PV_IIC_Delay();
    }
    if (!ack)
        INA226_PV_IIC_NAck();
    else
        INA226_PV_IIC_Ack();
    return receive;
}

/* INA226 PV Register Read/Write */

static uint8_t INA226_PV_WriteReg(uint8_t reg, uint16_t data)
{
    INA226_PV_IIC_Start();
    INA226_PV_IIC_WriteByte((INA226_PV_ADDR << 1) | 0);
    if (INA226_PV_IIC_WaitAck()) return 1;
    INA226_PV_IIC_WriteByte(reg);
    if (INA226_PV_IIC_WaitAck()) return 1;
    INA226_PV_IIC_WriteByte((uint8_t)(data >> 8));
    if (INA226_PV_IIC_WaitAck()) return 1;
    INA226_PV_IIC_WriteByte((uint8_t)(data & 0xFF));
    if (INA226_PV_IIC_WaitAck()) return 1;
    INA226_PV_IIC_Stop();
    return 0;
}

static uint8_t INA226_PV_ReadReg(uint8_t reg, uint16_t *data)
{
    uint16_t value;

    if (data == NULL) return 1;

    INA226_PV_IIC_Start();
    INA226_PV_IIC_WriteByte((INA226_PV_ADDR << 1) | 0);
    if (INA226_PV_IIC_WaitAck()) return 1;
    INA226_PV_IIC_WriteByte(reg);
    if (INA226_PV_IIC_WaitAck()) return 1;

    INA226_PV_IIC_Start();
    INA226_PV_IIC_WriteByte((INA226_PV_ADDR << 1) | 1);
    if (INA226_PV_IIC_WaitAck()) return 1;

    value = INA226_PV_IIC_ReadByte(1);
    value = (value << 8) | INA226_PV_IIC_ReadByte(0);
    INA226_PV_IIC_Stop();
    *data = value;
    return 0;
}

/* Public Functions */

void INA226_PV_Init(void)
{
    uint8_t ret;
    uint16_t cfg;

    INA226_PV_IIC_Init();
    delay_ms(10);

    /* Reset the device */
    ret = INA226_PV_WriteReg(INA226_REG_CONFIG, INA226_CONFIG_RESET);
    if (INA226_PV_VERBOSE_LOG) printf("INA226_PV: reset ret=%d\r\n", ret);
    delay_ms(10);

    /* Config: 16 samples avg, 1.1ms conversion, continuous */
    ret = INA226_PV_WriteReg(INA226_REG_CONFIG, 0x0927);
    if (INA226_PV_VERBOSE_LOG) printf("INA226_PV: cfg write ret=%d\r\n", ret);
    delay_ms(10);

    /* Read back config to verify */
    if (INA226_PV_ReadReg(INA226_REG_CONFIG, &cfg) != 0) cfg = 0;
    if (INA226_PV_VERBOSE_LOG) printf("INA226_PV: cfg readback=0x%04X (expect 0x0927)\r\n", cfg);

    if ((cfg & (uint16_t)~0x4000U) != 0x0927) {
        if (INA226_PV_VERBOSE_LOG) printf("INA226_PV: CONFIG MISMATCH! Retrying...\r\n");
        INA226_PV_WriteReg(INA226_REG_CONFIG, 0x0927);
        delay_ms(10);
        if (INA226_PV_ReadReg(INA226_REG_CONFIG, &cfg) != 0) cfg = 0;
        if (INA226_PV_VERBOSE_LOG) printf("INA226_PV: cfg retry=0x%04X\r\n", cfg);
    }

    /* Calibration */
    ret = INA226_PV_WriteReg(INA226_REG_CALIBRATION, 0x0066);
    if (INA226_PV_VERBOSE_LOG) printf("INA226_PV: cal write ret=%d\r\n", ret);
    delay_ms(10);

    /* Final register dump */
    uint16_t cal = 0, mfr = 0, die = 0;
    INA226_PV_ReadReg(INA226_REG_CALIBRATION, &cal);
    INA226_PV_ReadReg(INA226_REG_MANUFACTURER, &mfr);
    INA226_PV_ReadReg(INA226_REG_DIE_ID, &die);
    if (INA226_PV_VERBOSE_LOG) printf("INA226_PV: cfg=0x%04X cal=0x%04X mfr=0x%04X die=0x%04X\r\n", cfg, cal, mfr, die);
}

/*
 * PV INA226 calibration (R_shunt = 0.1 ohm, Cal = 0x0066 = 102):
 *   Measured: 772mA reading vs 1A actual -> corrected LSB = 0.502 * (1000/772)
 *   Current_LSB = 0.000650A = 650uA/bit
 *   Power_LSB = 25 * Current_LSB = 0.01625W = 16.25mW/bit
 */
#define INA226_PV_CURRENT_LSB   0.000650f     /* A/bit */
#define INA226_PV_POWER_LSB     0.01625f      /* W/bit */

/* I2C bus recovery: toggle SCL to unstick SDA */
static void INA226_PV_IIC_Recovery(void)
{
    INA226_PV_SDA_HIGH();
    for (int i = 0; i < 9; i++) {
        INA226_PV_SCL_LOW();
        INA226_PV_IIC_Delay();
        INA226_PV_SCL_HIGH();
        INA226_PV_IIC_Delay();
    }
    INA226_PV_IIC_Stop();
}

uint8_t INA226_PV_ReadData(INA226_Data *data)
{
    uint16_t raw_v, raw_c, raw_p, raw_sv, cfg;

    if (data == NULL) return 1;

    /* Check if SDA is stuck low, recover if needed */
    if (INA226_PV_SDA_READ() == 0) {
        INA226_PV_IIC_Recovery();
        /* Re-init after recovery */
        INA226_PV_WriteReg(INA226_REG_CONFIG, 0x0927);
        INA226_PV_WriteReg(INA226_REG_CALIBRATION, 0x0066);
    }

    if (INA226_PV_ReadReg(INA226_REG_BUS_VOLT, &raw_v) != 0) return 1;
    if (INA226_PV_ReadReg(INA226_REG_SHUNT_VOLT, &raw_sv) != 0) return 1;
    if (INA226_PV_ReadReg(INA226_REG_CURRENT, &raw_c) != 0) return 1;
    if (INA226_PV_ReadReg(INA226_REG_POWER, &raw_p) != 0) return 1;
    if (INA226_PV_ReadReg(INA226_REG_CONFIG, &cfg) != 0) return 1;

    data->bus_voltage   = raw_v * INA226_BUS_VOLT_LSB;
    data->shunt_voltage = (int16_t)raw_sv * INA226_SHUNT_VOLT_LSB;
    data->current       = (int16_t)raw_c * INA226_PV_CURRENT_LSB;
    data->power         = raw_p * INA226_PV_POWER_LSB;

    if (INA226_PV_VERBOSE_LOG && pv_dbg_cnt > 0) {
        pv_dbg_cnt--;
        printf("PV: cfg=0x%04X bus=%u shunt=%d cur=%d pwr=%u V=%.3f\r\n",
               cfg, raw_v, (int16_t)raw_sv, (int16_t)raw_c, raw_p,
               (double)data->bus_voltage);
    }

    return 0;
}
