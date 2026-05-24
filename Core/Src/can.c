/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    can.c
  * @brief   This file provides code for the configuration
  *          of the CAN instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "can.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

CAN_HandleTypeDef hcan1;

/* CAN1 init function */
void MX_CAN1_Init(void)
{

  /* USER CODE BEGIN CAN1_Init 0 */

  /* USER CODE END CAN1_Init 0 */

  /* USER CODE BEGIN CAN1_Init 1 */

  /* USER CODE END CAN1_Init 1 */
  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = 3;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_13TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;
  hcan1.Init.TimeTriggeredMode = DISABLE;
  hcan1.Init.AutoBusOff = ENABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = ENABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN1_Init 2 */

  /* USER CODE END CAN1_Init 2 */

}

void HAL_CAN_MspInit(CAN_HandleTypeDef* canHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(canHandle->Instance==CAN1)
  {
  /* USER CODE BEGIN CAN1_MspInit 0 */

  /* USER CODE END CAN1_MspInit 0 */
    /* CAN1 clock enable */
    __HAL_RCC_CAN1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**CAN1 GPIO Configuration
    PA11     ------> CAN1_RX
    PA12     ------> CAN1_TX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_11|GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* CAN1 interrupt Init */
    HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
    HAL_NVIC_SetPriority(CAN1_SCE_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(CAN1_SCE_IRQn);
  /* USER CODE BEGIN CAN1_MspInit 1 */

  /* USER CODE END CAN1_MspInit 1 */
  }
}

void HAL_CAN_MspDeInit(CAN_HandleTypeDef* canHandle)
{

  if(canHandle->Instance==CAN1)
  {
  /* USER CODE BEGIN CAN1_MspDeInit 0 */

  /* USER CODE END CAN1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_CAN1_CLK_DISABLE();

    /**CAN1 GPIO Configuration
    PA11     ------> CAN1_RX
    PA12     ------> CAN1_TX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11|GPIO_PIN_12);

    /* CAN1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(CAN1_RX0_IRQn);
    HAL_NVIC_DisableIRQ(CAN1_SCE_IRQn);
  /* USER CODE BEGIN CAN1_MspDeInit 1 */

  /* USER CODE END CAN1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

#include <string.h>
#include <stdio.h>
#include "cmsis_os.h"
#include "queue.h"

/* Get the CAN RX queue handle defined in freertos.c */
extern QueueHandle_t CAN_GetRxQueueHandle(void);

/* CAN receive buffer and flag */
static uint8_t can_rx_data[8];
static uint8_t can_rx_len = 0;
static uint32_t can_rx_id = 0;
static volatile uint8_t can_rx_flag = 0;

/**
  * @brief  Configure CAN filter and start CAN
  * @retval 0=OK, 1=error
  */
uint8_t CAN_Start(void)
{
    CAN_FilterTypeDef filter;

    /* Filter: accept all messages */
    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = 0x0000;
    filter.FilterIdLow = 0x0000;
    filter.FilterMaskIdHigh = 0x0000;
    filter.FilterMaskIdLow = 0x0000;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK) return 1;
    if (HAL_CAN_Start(&hcan1) != HAL_OK) return 1;
    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) return 1;

    printf("CAN1 started\r\n");
    return 0;
}

/**
  * @brief  Send data via CAN
  * @param  id: CAN message ID (11-bit standard)
  * @param  data: pointer to data (max 8 bytes)
  * @param  len: data length (0~8)
  * @retval 0=OK, 1=error
  */
uint8_t CAN_Send(uint32_t id, uint8_t *data, uint8_t len)
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

/**
  * @brief  Check if CAN has received data
  * @retval 1=data available, 0=no data
  */
uint8_t CAN_ReceiveReady(void)
{
    return can_rx_flag;
}

/**
  * @brief  Get received CAN data
  * @param  id: output message ID
  * @param  data: output buffer (8 bytes)
  * @param  len: output data length
  * @retval 0=OK, 1=no data
  */
uint8_t CAN_Receive(uint32_t *id, uint8_t *data, uint8_t *len)
{
    if (!can_rx_flag) return 1;

    if (id) *id = can_rx_id;
    if (len) *len = can_rx_len;
    if (data) memcpy(data, can_rx_data, can_rx_len);

    can_rx_flag = 0;
    return 0;
}

/**
  * @brief  CAN RX FIFO0 callback — posts message to FreeRTOS queue from ISR
  */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef header;
    CAN_Msg_t msg;

    if (hcan->Instance == CAN1)
    {
        if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &header, can_rx_data) == HAL_OK)
        {
            can_rx_id = header.StdId;
            can_rx_len = header.DLC;
            can_rx_flag = 1;

            /* Pack into message struct and post to queue (ISR-safe) */
            msg.id = header.StdId;
            msg.len = header.DLC;
            memcpy(msg.data, can_rx_data, header.DLC);

            QueueHandle_t q = CAN_GetRxQueueHandle();
            if (q)
            {
                BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                xQueueSendFromISR(q, &msg, &xHigherPriorityTaskWoken);
                portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            }
        }
    }
}

/* USER CODE END 1 */
