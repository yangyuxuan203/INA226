#include "energy_io.h"

#include "can_app.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

#define ENERGY_IO_VERBOSE_LOG 0U

/* Home dispatch MOS outputs. */
#define MOS_PV_SRC_PORT       GPIOE
#define MOS_PV_SRC_PIN        GPIO_PIN_0
#define MOS_RIGID_LOAD_PORT   GPIOE
#define MOS_RIGID_LOAD_PIN    GPIO_PIN_1
#define MOS_HOME_SRC_PORT     GPIOB
#define MOS_HOME_SRC_PIN      GPIO_PIN_14
#define MOS_LED_PORT          GPIOC
#define MOS_LED_PIN           GPIO_PIN_10
#define MOS_FAN_PORT          GPIOD
#define MOS_FAN_PIN           GPIO_PIN_2
#define MOS_QI_PORT           GPIOD
#define MOS_QI_PIN            GPIO_PIN_13

/* Local keys, active low: PE2=LED, PE3=fan, PE4=Qi. */
#define KEY_LED_PORT          GPIOE
#define KEY_LED_PIN           GPIO_PIN_2
#define KEY_FAN_PORT          GPIOE
#define KEY_FAN_PIN           GPIO_PIN_3
#define KEY_QI_PORT           GPIOE
#define KEY_QI_PIN            GPIO_PIN_4

/* PV charging outputs. */
#define MOS_HOME_CHARGE_PIN   GPIO_PIN_6
#define MOS_CAR_CHARGE_PIN    GPIO_PIN_7
#define MOS_CHARGE_PORT       GPIOD
#define MOS_CHARGE_ALL_PINS   (MOS_HOME_CHARGE_PIN | MOS_CAR_CHARGE_PIN)

#define CAN_CMD_V2H_ON          0x01U
#define CAN_CMD_V2H_OFF         0x02U
#define CAN_CMD_CAR_CHARGE_ON   0x03U
#define CAN_CMD_CAR_CHARGE_OFF  0x04U
#define CAN_V2H_POWER_LIMIT     80U

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} EnergyGpio_t;

static uint8_t EnergyIo_PinIsSet(GPIO_TypeDef *port, uint16_t pin)
{
    return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET ? 1U : 0U;
}

static uint8_t EnergyIo_KeyIsPressed(GPIO_TypeDef *port, uint16_t pin)
{
    return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET ? 1U : 0U;
}

static uint8_t EnergyIo_GetOutputPin(EnergyOutputId_t output,
                                     EnergyGpio_t *gpio)
{
    if (gpio == NULL)
    {
        return 1U;
    }

    switch (output)
    {
        case ENERGY_OUTPUT_PV_SOURCE:
            gpio->port = MOS_PV_SRC_PORT;
            gpio->pin = MOS_PV_SRC_PIN;
            break;
        case ENERGY_OUTPUT_HOME_SOURCE:
            gpio->port = MOS_HOME_SRC_PORT;
            gpio->pin = MOS_HOME_SRC_PIN;
            break;
        case ENERGY_OUTPUT_RIGID:
            gpio->port = MOS_RIGID_LOAD_PORT;
            gpio->pin = MOS_RIGID_LOAD_PIN;
            break;
        case ENERGY_OUTPUT_LED:
            gpio->port = MOS_LED_PORT;
            gpio->pin = MOS_LED_PIN;
            break;
        case ENERGY_OUTPUT_FAN:
            gpio->port = MOS_FAN_PORT;
            gpio->pin = MOS_FAN_PIN;
            break;
        case ENERGY_OUTPUT_QI:
            gpio->port = MOS_QI_PORT;
            gpio->pin = MOS_QI_PIN;
            break;
        case ENERGY_OUTPUT_HOME_CHARGE:
            gpio->port = MOS_CHARGE_PORT;
            gpio->pin = MOS_HOME_CHARGE_PIN;
            break;
        case ENERGY_OUTPUT_CAR_CHARGE:
            gpio->port = MOS_CHARGE_PORT;
            gpio->pin = MOS_CAR_CHARGE_PIN;
            break;
        default:
            gpio->port = NULL;
            gpio->pin = 0U;
            return 1U;
    }

    return 0U;
}

void EnergyIo_ReadKeys(EnergyKeyState_t *keys)
{
    if (keys == NULL)
    {
        return;
    }

    keys->led = EnergyIo_KeyIsPressed(KEY_LED_PORT, KEY_LED_PIN);
    keys->fan = EnergyIo_KeyIsPressed(KEY_FAN_PORT, KEY_FAN_PIN);
    keys->qi = EnergyIo_KeyIsPressed(KEY_QI_PORT, KEY_QI_PIN);
}

void EnergyIo_WriteOutput(EnergyOutputId_t output, uint8_t on)
{
    EnergyGpio_t gpio;

    if (EnergyIo_GetOutputPin(output, &gpio) != 0U)
    {
        return;
    }

    HAL_GPIO_WritePin(gpio.port, gpio.pin,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint8_t EnergyIo_ReadOutput(EnergyOutputId_t output)
{
    EnergyGpio_t gpio;

    if (EnergyIo_GetOutputPin(output, &gpio) != 0U)
    {
        return 0U;
    }

    return EnergyIo_PinIsSet(gpio.port, gpio.pin);
}

void EnergyIo_DisableChargeOutputs(void)
{
    HAL_GPIO_WritePin(MOS_CHARGE_PORT, MOS_CHARGE_ALL_PINS, GPIO_PIN_RESET);
}

void EnergyIo_DisableAllOutputs(void)
{
    EnergyIo_WriteOutput(ENERGY_OUTPUT_PV_SOURCE, 0U);
    EnergyIo_WriteOutput(ENERGY_OUTPUT_HOME_SOURCE, 0U);
    EnergyIo_WriteOutput(ENERGY_OUTPUT_RIGID, 0U);
    EnergyIo_WriteOutput(ENERGY_OUTPUT_LED, 0U);
    EnergyIo_WriteOutput(ENERGY_OUTPUT_FAN, 0U);
    EnergyIo_WriteOutput(ENERGY_OUTPUT_QI, 0U);
    EnergyIo_DisableChargeOutputs();
}

void EnergyIo_GetOutputState(EnergyOutputState_t *state)
{
    if (state == NULL)
    {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->pv_source = EnergyIo_ReadOutput(ENERGY_OUTPUT_PV_SOURCE);
    state->home_source = EnergyIo_ReadOutput(ENERGY_OUTPUT_HOME_SOURCE);
    state->rigid = EnergyIo_ReadOutput(ENERGY_OUTPUT_RIGID);
    state->led = EnergyIo_ReadOutput(ENERGY_OUTPUT_LED);
    state->fan = EnergyIo_ReadOutput(ENERGY_OUTPUT_FAN);
    state->qi = EnergyIo_ReadOutput(ENERGY_OUTPUT_QI);
    state->home_charge = EnergyIo_ReadOutput(ENERGY_OUTPUT_HOME_CHARGE);
    state->car_charge = EnergyIo_ReadOutput(ENERGY_OUTPUT_CAR_CHARGE);
}

void EnergyIo_SendCarV2HCommand(uint8_t enable)
{
    CAN_CtrlCmd_t cmd = {0};

    cmd.cmd = enable ? CAN_CMD_V2H_ON : CAN_CMD_V2H_OFF;
    cmd.param = enable ? CAN_V2H_POWER_LIMIT : 0U;

    if (CAN_App_Send(CAN_ID_CTRL_CMD, (uint8_t *)&cmd, sizeof(cmd)) != 0U)
    {
        if (ENERGY_IO_VERBOSE_LOG)
        {
            printf("CAN V2H %s send failed\r\n", enable ? "ON" : "OFF");
        }
    }
}

void EnergyIo_SendCarChargeCommand(uint8_t enable, float charge_power_w)
{
    CAN_CtrlCmd_t cmd = {0};
    int power_0p1w;

    cmd.cmd = enable ? CAN_CMD_CAR_CHARGE_ON : CAN_CMD_CAR_CHARGE_OFF;
    if (enable)
    {
        if (charge_power_w < 0.0f)
        {
            charge_power_w = 0.0f;
        }
        power_0p1w = (int)(charge_power_w * 10.0f + 0.5f);
        if (power_0p1w > 255)
        {
            power_0p1w = 255;
        }
        cmd.param = (uint8_t)power_0p1w;
    }

    if (CAN_App_Send(CAN_ID_CTRL_CMD, (uint8_t *)&cmd, sizeof(cmd)) != 0U)
    {
        if (ENERGY_IO_VERBOSE_LOG)
        {
            printf("CAN car charge %s send failed\r\n",
                   enable ? "ON" : "OFF");
        }
    }
}
