/**
    ************************************************************
    * 文件名：    esp8266.h
    * 说明：      ESP8266 AT指令驱动头文件（非透传模式）
    ************************************************************
**/

#ifndef _ESP8266_H_
#define _ESP8266_H_

#include "stm32f10x.h"

#define REV_OK      0   // 接收完成
#define REV_WAIT    1   // 等待中

/* 函数声明 */
void ESP8266_Init(void);
void ESP8266_Clear(void);
_Bool ESP8266_SendCmd(char *cmd, char *res, uint32_t timeout);
void ESP8266_SendCmdNoWait(char *cmd);
uint8_t ESP8266_SendData(unsigned char *data, unsigned short len);
unsigned char *ESP8266_GetIPD(unsigned short timeOut);
unsigned char *ESP8266_WaitMQTTData(void);

/* 外部变量声明 */
extern unsigned char esp8266_buf[512];
extern unsigned short esp8266_cnt;

#endif
