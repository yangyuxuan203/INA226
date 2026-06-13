/**
    ************************************************************
    * 文件名：    esp8266.c
    * 说明：      ESP8266 AT指令驱动（非透传模式）
    ************************************************************
**/

#include "stm32f10x.h"
#include "esp8266.h"
#include "usart.h"
#include "delay.h"
#include <string.h>
#include <stdio.h>

#define WIFI_SSID   "april"
#define WIFI_PWD    "12345678910"

unsigned char esp8266_buf[512];
unsigned short esp8266_cnt = 0, esp8266_cntPre = 0;

static _Bool ESP8266_BufferHas(const char *word)
{
    unsigned short i;
    unsigned short word_len = strlen(word);

    if(word_len == 0 || esp8266_cnt < word_len)
        return 0;

    for(i = 0; i <= esp8266_cnt - word_len; i++)
    {
        if(memcmp(&esp8266_buf[i], word, word_len) == 0)
            return 1;
    }

    return 0;
}

void ESP8266_Clear(void)
{
    memset(esp8266_buf, 0, sizeof(esp8266_buf));
    esp8266_cnt = 0;
    esp8266_cntPre = 0;
}

_Bool ESP8266_WaitRecive(void)
{
    if(esp8266_cnt == 0)
        return REV_WAIT;
    if(esp8266_cnt == esp8266_cntPre)
    {
        esp8266_cnt = 0;
        esp8266_cntPre = 0;
        return REV_OK;
    }
    esp8266_cntPre = esp8266_cnt;
    return REV_WAIT;
}

_Bool ESP8266_SendCmd(char *cmd, char *res, uint32_t timeout)
{
    unsigned short timeOut = timeout / 10;

    ESP8266_Clear();
    UsartPrintf(USART1, "Send: %s", cmd);
    Usart_SendString(USART3, (unsigned char *)cmd, strlen(cmd));

    while(timeOut--)
    {
        if(ESP8266_WaitRecive() == REV_OK)
        {
            if(strstr((const char *)esp8266_buf, res) != NULL)
            {
                ESP8266_Clear();
                return 0;
            }
            ESP8266_Clear();
        }
        DelayXms(10);
    }
    UsartPrintf(USART1, "Timeout\r\n");
    ESP8266_Clear();
    return 1;
}

void ESP8266_SendCmdNoWait(char *cmd)
{
    UsartPrintf(USART1, "Send: %s", cmd);
    Usart_SendString(USART3, (unsigned char *)cmd, strlen(cmd));
}

/**
    ************************************************************
    * 函数名称：    ESP8266_SendData
    * 函数功能：    通过AT+CIPSEND发送数据（非透传模式）
    * 入口参数：    data-数据指针  len-数据长度
    * 返回参数：    0-成功  1-失败
    ************************************************************
*/
uint8_t ESP8266_SendData(unsigned char *data, unsigned short len)
{
    char cmd[32];
    unsigned short timeOut;

    sprintf(cmd, "AT+CIPSEND=%d\r\n", len);

    ESP8266_Clear();
    Usart_SendString(USART3, (unsigned char *)cmd, strlen(cmd));

    /* 等待 '>' 提示符 */
    timeOut = 200;
    while(timeOut--)
    {
        if(ESP8266_BufferHas(">"))
            break;
        if(ESP8266_BufferHas("ERROR") ||
           ESP8266_BufferHas("CLOSED") ||
           ESP8266_BufferHas("IPMODE"))
        {
            UsartPrintf(USART1, "CIPSEND err: %s\r\n", esp8266_buf);
            ESP8266_Clear();
            return 1;
        }
        DelayXms(10);
    }
    if(timeOut == 0)
    {
        UsartPrintf(USART1, "CIPSEND timeout: %s\r\n", esp8266_buf);
        ESP8266_Clear();
        return 1;
    }

    /* 发送实际数据 */
    ESP8266_Clear();
    Usart_SendString(USART3, data, len);

    /* Keep the RX buffer here: MQTT CONNACK can arrive with SEND OK. */
    timeOut = 300;
    while(timeOut--)
    {
        if(ESP8266_BufferHas("SEND OK"))
        {
            return 0;
        }
        if(ESP8266_BufferHas("ERROR") ||
           ESP8266_BufferHas("CLOSED"))
        {
            UsartPrintf(USART1, "Send err: %s\r\n", esp8266_buf);
            ESP8266_Clear();
            return 1;
        }
        DelayXms(10);
    }
    UsartPrintf(USART1, "SEND OK timeout\r\n");
    ESP8266_Clear();
    return 1;
}

/**
    ************************************************************
    * 函数名称：    ESP8266_GetIPD
    * 函数功能：    从+IPD响应中提取MQTT数据
    * 入口参数：    timeOut-超时时间(ms)
    * 返回参数：    数据指针(NULL表示无数据)
    * 说明：        ESP8266收到TCP数据时会回复 +IPD,<len>:<data>
    ************************************************************
*/
unsigned char *ESP8266_GetIPD(unsigned short timeOut)
{
    char *ptr = NULL;

    while(timeOut--)
    {
        if(esp8266_cnt > 0)
        {
            /* 查找 "+IPD," */
            ptr = strstr((char *)esp8266_buf, "+IPD,");
            if(ptr != NULL)
            {
                ptr += 5;  /* 跳过 "+IPD," */
                /* 解析长度 */
                /* 查找 ':' 分隔符 */
                ptr = strchr(ptr, ':');
                if(ptr != NULL)
                {
                    ptr++;  /* 跳过 ':' */
                    return (unsigned char *)ptr;
                }
            }
        }
        DelayXms(10);
    }
    return NULL;
}

/**
    ************************************************************
    * 函数名称：    ESP8266_WaitMQTTData
    * 函数功能：    等待MQTT数据（检查+IPD响应）
    * 返回参数：    数据指针(NULL表示无数据)
    ************************************************************
*/
unsigned char *ESP8266_WaitMQTTData(void)
{
    char *ptr = NULL;

    if(esp8266_cnt == 0)
        return NULL;

    ptr = strstr((char *)esp8266_buf, "+IPD,");
    if(ptr != NULL)
    {
        ptr += 5;
        while(*ptr != ':' && *ptr != '\0') ptr++;
        if(*ptr == ':')
        {
            ptr++;
            return (unsigned char *)ptr;
        }
    }
    return NULL;
}

void ESP8266_Init(void)
{
    char cmd[256];
    uint8_t retry;
    uint8_t reset_cnt;
    unsigned short t;

    ESP8266_Clear();
    DelayXms(3000);

    UsartPrintf(USART1, "=== ESP8266 Init ===\r\n");

    for(reset_cnt = 0; reset_cnt < 3; reset_cnt++)
    {
        /* 发送+++退出透传模式 */
        Usart_SendString(USART3, (unsigned char *)"+++", 3);
        DelayXms(1000);
        ESP8266_Clear();

        /* 尝试AT指令 */
        UsartPrintf(USART1, "1. AT (attempt %d)\r\n", reset_cnt + 1);
        for(retry = 0; retry < 5; retry++)
        {
            if(ESP8266_SendCmd("AT\r\n", "OK", 2000) == 0)
                goto at_ok;
            DelayXms(500);
        }

        /* AT失败，复位模块再试 */
        UsartPrintf(USART1, "AT failed, resetting...\r\n");
        Usart_SendString(USART3, (unsigned char *)"AT+RST\r\n", 8);
        DelayXms(5000);
        ESP8266_Clear();
    }

    UsartPrintf(USART1, "ESP8266 not responding! Check power!\r\n");
    while(1);  // 停在这里

at_ok:

    UsartPrintf(USART1, "2. CWMODE=1\r\n");
    while(ESP8266_SendCmd("AT+CWMODE=1\r\n", "OK", 2000))
        DelayXms(500);

    UsartPrintf(USART1, "3. CWDHCP\r\n");
    while(ESP8266_SendCmd("AT+CWDHCP=1,1\r\n", "OK", 2000))
        DelayXms(500);

    UsartPrintf(USART1, "4. CWJAP\r\n");
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PWD);
    for(retry = 0; retry < 3; retry++)
    {
        ESP8266_Clear();
        UsartPrintf(USART1, "Send: %s", cmd);
        Usart_SendString(USART3, (unsigned char *)cmd, strlen(cmd));

        /* 等待更长时间，WiFi连接可能需要20秒 */
        t = 2000;
        while(t--)
        {
            if(esp8266_cnt > 0)
            {
                UsartPrintf(USART1, "R[%d]:%s\r\n", esp8266_cnt, esp8266_buf);
                /* 接受GOT IP或OK作为成功 */
                if(strstr((char *)esp8266_buf, "GOT IP") != NULL ||
                   strstr((char *)esp8266_buf, "OK") != NULL)
                {
                    ESP8266_Clear();
                    goto wifi_ok;
                }
                if(strstr((char *)esp8266_buf, "FAIL") != NULL ||
                   strstr((char *)esp8266_buf, "ERROR") != NULL)
                {
                    UsartPrintf(USART1, "WiFi connect failed!\r\n");
                    break;
                }
                ESP8266_Clear();
            }
            DelayXms(10);
        }
        UsartPrintf(USART1, "WiFi timeout, retry...\r\n");
        DelayXms(2000);
    }

wifi_ok:
    /* 验证WiFi连接 */
    UsartPrintf(USART1, "Check IP...\r\n");
    ESP8266_Clear();
    Usart_SendString(USART3, (unsigned char *)"AT+CIFSR\r\n", strlen("AT+CIFSR\r\n"));
    DelayXms(2000);
    UsartPrintf(USART1, "IP: %s\r\n", esp8266_buf);
    ESP8266_Clear();

    /* 确保关闭透传模式 */
    UsartPrintf(USART1, "4.5 CIPMODE=0\r\n");
    ESP8266_SendCmd("AT+CIPMODE=0\r\n", "OK", 2000);

    UsartPrintf(USART1, "4.6 CIPMUX=0\r\n");
    ESP8266_SendCmd("AT+CIPMUX=0\r\n", "OK", 2000);

    UsartPrintf(USART1, "4.7 CIPCLOSE\r\n");
    ESP8266_SendCmd("AT+CIPCLOSE\r\n", "OK", 2000);

    UsartPrintf(USART1, "5. Connect MQTT\r\n");
    while(ESP8266_SendCmd("AT+CIPSTART=\"TCP\",\"bemfa.com\",9501\r\n", "CONNECT", 10000))
        DelayXms(1000);

    UsartPrintf(USART1, "=== ESP8266 Init OK ===\r\n");
}

void USART3_IRQHandler(void)
{
    uint32_t sr = USART3->SR;

    /* 检查是否有错误标志，清除它们 */
    if(sr & (USART_FLAG_ORE | USART_FLAG_NE | USART_FLAG_FE | USART_FLAG_PE))
    {
        USART3->DR;  // 读DR清除错误标志
        return;
    }

    /* 正常接收数据 */
    if(sr & USART_FLAG_RXNE)
    {
        if(esp8266_cnt >= sizeof(esp8266_buf))
            esp8266_cnt = 0;
        esp8266_buf[esp8266_cnt++] = USART3->DR;
    }
}
