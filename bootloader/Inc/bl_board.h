#ifndef BL_BOARD_H
#define BL_BOARD_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

void BL_Board_Init(void);
void BL_Board_Reset(void);
uint8_t BL_UART_Write(const uint8_t *data, uint16_t length, uint32_t timeout_ms);
uint8_t BL_UART_ReadByte(uint8_t *byte, uint32_t timeout_ms);
void BL_UART_Flush(void);

#endif /* BL_BOARD_H */
