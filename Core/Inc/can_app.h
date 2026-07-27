/**
  ******************************************************************************
  * @file    can_app.h
  * @brief   CAN application layer - send/receive functions
  ******************************************************************************
  */

#ifndef __CAN_APP_H__
#define __CAN_APP_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/
/* CAN Message IDs (STM32F1 -> STM32F4) */
#define CAN_ID_BAT_VOLTAGE    0x100
#define CAN_ID_BAT_CURRENT    0x101
#define CAN_ID_BAT_TEMP       0x102
#define CAN_ID_BAT_STATUS     0x103
#define CAN_ID_BAT_SOC        0x104

/* CAN Message ID (STM32F4 -> STM32F1) */
#define CAN_ID_CTRL_CMD       0x200

/* Exported types ------------------------------------------------------------*/
/* Battery status codes */
typedef enum {
    BAT_STATUS_IDLE      = 0x00,
    BAT_STATUS_CHARGING  = 0x01,
    BAT_STATUS_DISCHARGE = 0x02,
    BAT_STATUS_FAULT     = 0x03,
    BAT_STATUS_TILTED    = 0x04
} BatteryStatus_t;

/* CAN TX data structure (from STM32F1) */
typedef struct {
    float   voltage;
    float   current;
    float   temperature;
    BatteryStatus_t status;
    uint8_t soc_pct;            /* SOC from STM32F1 via 0x104 */
} CAN_BatteryData_t;

/* CAN RX control command (to STM32F1) */
typedef struct {
    uint8_t cmd;
    uint8_t param;
    uint8_t reserved[6];
} CAN_CtrlCmd_t;

/* Exported functions --------------------------------------------------------*/
uint8_t CAN_App_Init(void);
uint8_t CAN_App_Send(uint32_t id, uint8_t *data, uint8_t len);
void CAN_App_TransmitBattery(const CAN_BatteryData_t *data);
void CAN_App_GetBatterySnapshot(CAN_BatteryData_t *data);
uint8_t CAN_App_TryReceiveCommand(CAN_CtrlCmd_t *command);

#ifdef __cplusplus
}
#endif

#endif /* __CAN_APP_H__ */
