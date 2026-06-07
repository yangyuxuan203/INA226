/**
  ******************************************************************************
  * @file    can_app.c
  * @brief   CAN application layer - send/receive functions
  ******************************************************************************
  */

#include "can_app.h"
#include "can.h"
#include <string.h>
#include "cmsis_os.h"
#include "queue.h"
#include "usart.h"
#include <stdio.h>

QueueHandle_t xCanRxQueue;

/* Received battery data from STM32F1 */
CAN_BatteryData_t g_f1_battery = {0};
volatile uint8_t g_f1_battery_updated = 0;

static CAN_RxHeaderTypeDef g_rx_header;
static uint8_t g_rx_data[8];

static CAN_FilterTypeDef g_can_filter;

void CAN_App_Init(void)
{
    HAL_StatusTypeDef ret;

    /* Create queue for CAN RX commands (depth 16, each item is CAN_CtrlCmd_t) */
    xCanRxQueue = xQueueCreate(16, sizeof(CAN_CtrlCmd_t));

    /* Configure CAN filter to accept all messages */
    g_can_filter.FilterBank           = 0;
    g_can_filter.FilterMode           = CAN_FILTERMODE_IDMASK;
    g_can_filter.FilterScale          = CAN_FILTERSCALE_32BIT;
    g_can_filter.FilterIdHigh         = 0x0000;
    g_can_filter.FilterIdLow          = 0x0000;
    g_can_filter.FilterMaskIdHigh     = 0x0000;
    g_can_filter.FilterMaskIdLow      = 0x0000;
    g_can_filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    g_can_filter.FilterActivation     = ENABLE;

    ret = HAL_CAN_ConfigFilter(&hcan1, &g_can_filter);
    printf("CAN ConfigFilter: %d\r\n", ret);

    ret = HAL_CAN_Start(&hcan1);
    printf("CAN Start: %d\r\n", ret);

    ret = HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING);
    printf("CAN ActivateNotification: %d\r\n", ret);

    printf("CAN State: %d\r\n", HAL_CAN_GetState(&hcan1));
}

/**
  * @brief  Send raw data via CAN
  * @param  id: CAN message ID (11-bit standard)
  * @param  data: pointer to data (max 8 bytes)
  * @param  len: data length (0~8)
  * @retval 0=OK, 1=error
  */
uint8_t CAN_App_Send(uint32_t id, uint8_t *data, uint8_t len)
{
    CAN_TxHeaderTypeDef header;
    uint32_t mailbox;

    header.StdId = id;
    header.ExtId = 0;
    header.RTR = CAN_RTR_DATA;
    header.IDE = CAN_ID_STD;
    header.DLC = len > 8 ? 8 : len;

    if (HAL_CAN_AddTxMessage(&hcan1, &header, data, &mailbox) != HAL_OK)
        return 1;

    /* Wait until transmitted (timeout ~10ms) */
    uint32_t tick = HAL_GetTick();
    while (HAL_CAN_IsTxMessagePending(&hcan1, mailbox))
    {
        if (HAL_GetTick() - tick > 10) return 1;
    }
    return 0;
}

void CAN_App_TransmitBattery(const CAN_BatteryData_t *data)
{
    CAN_TxHeaderTypeDef tx_header;
    uint8_t tx_data[8];
    uint32_t mailbox;

    /* Send voltage */
    tx_header.StdId   = CAN_ID_BAT_VOLTAGE;
    tx_header.ExtId   = 0;
    tx_header.RTR     = CAN_RTR_DATA;
    tx_header.IDE     = CAN_ID_STD;
    tx_header.DLC     = 4;
    *((float *)tx_data) = data->voltage;
    HAL_CAN_AddTxMessage(&hcan1, &tx_header, tx_data, &mailbox);

    /* Send current */
    tx_header.StdId = CAN_ID_BAT_CURRENT;
    *((float *)tx_data) = data->current;
    HAL_CAN_AddTxMessage(&hcan1, &tx_header, tx_data, &mailbox);

    /* Send temperature */
    tx_header.StdId = CAN_ID_BAT_TEMP;
    *((float *)tx_data) = data->temperature;
    HAL_CAN_AddTxMessage(&hcan1, &tx_header, tx_data, &mailbox);

    /* Send status */
    tx_header.StdId = CAN_ID_BAT_STATUS;
    tx_header.DLC   = 1;
    tx_data[0] = (uint8_t)data->status;
    HAL_CAN_AddTxMessage(&hcan1, &tx_header, tx_data, &mailbox);
}

/* CAN RX FIFO0 callback */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan_handle)
{
    if (hcan_handle->Instance == CAN1)
    {
        /* Read all messages from FIFO - no printf in ISR! */
        while (HAL_CAN_GetRxMessage(hcan_handle, CAN_RX_FIFO0, &g_rx_header, g_rx_data) == HAL_OK)
        {
            /* Only process standard data frames */
            if (g_rx_header.RTR != CAN_RTR_DATA || g_rx_header.IDE != CAN_ID_STD)
            {
                continue;
            }

            switch (g_rx_header.StdId)
            {
                case CAN_ID_BAT_VOLTAGE:
                    memcpy(&g_f1_battery.voltage, g_rx_data, sizeof(float));
                    g_f1_battery_updated = 1;
                    break;

                case CAN_ID_BAT_CURRENT:
                    memcpy(&g_f1_battery.current, g_rx_data, sizeof(float));
                    g_f1_battery_updated = 1;
                    break;

                case CAN_ID_BAT_TEMP:
                    memcpy(&g_f1_battery.temperature, g_rx_data, sizeof(float));
                    g_f1_battery_updated = 1;
                    break;

                case CAN_ID_BAT_STATUS:
                    g_f1_battery.status = (BatteryStatus_t)g_rx_data[0];
                    g_f1_battery_updated = 1;
                    break;

                case CAN_ID_BAT_SOC:
                {
                    float f1_soc;
                    memcpy(&f1_soc, g_rx_data, sizeof(float));
                    g_f1_battery.soc_pct = (uint8_t)(f1_soc + 0.5f);
                    g_f1_battery_updated = 1;
                    break;
                }

                case CAN_ID_CTRL_CMD:
                {
                    CAN_CtrlCmd_t cmd;
                    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                    memcpy(&cmd, g_rx_data, sizeof(CAN_CtrlCmd_t));
                    xQueueSendFromISR(xCanRxQueue, &cmd, &xHigherPriorityTaskWoken);
                    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
                    break;
                }

                default:
                    break;
            }
        }
    }
}
