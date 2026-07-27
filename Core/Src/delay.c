#include "delay.h"

#include "FreeRTOS.h"
#include "task.h"

#define DELAY_DWT_MAX_CHUNK_US 1000000U

static uint32_t s_cycles_per_us;

static void delay_wait_cycles(uint32_t cycles)
{
    uint32_t start = DWT->CYCCNT;

    while ((uint32_t)(DWT->CYCCNT - start) < cycles)
    {
        __NOP();
    }
}

void delay_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();

    s_cycles_per_us = SystemCoreClock / 1000000U;
}

void delay_us(uint32_t us)
{
    if (s_cycles_per_us == 0U)
    {
        delay_init();
    }

    while (us != 0U)
    {
        uint32_t chunk_us = us > DELAY_DWT_MAX_CHUNK_US ?
                            DELAY_DWT_MAX_CHUNK_US : us;

        delay_wait_cycles(chunk_us * s_cycles_per_us);
        us -= chunk_us;
    }
}

void delay_ms(uint32_t ms)
{
    if (ms == 0U)
    {
        return;
    }

    if (__get_IPSR() == 0U &&
        xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
        return;
    }

    for (uint32_t i = 0; i < ms; i++)
    {
        delay_us(1000);
    }
}
