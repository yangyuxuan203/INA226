#include "delay.h"

static uint32_t fac_us = 0;

void delay_init(void)
{
    fac_us = SystemCoreClock / 1000000;
}

void delay_us(uint32_t us)
{
    uint32_t ticks = us * fac_us;
    uint32_t told, tnow, tcnt = 0;
    uint32_t reload = SysTick->LOAD;

    told = SysTick->VAL;
    while (1)
    {
        tnow = SysTick->VAL;
        if (tnow != told)
        {
            if (tnow < told)
                tcnt += told - tnow;
            else
                tcnt += reload - tnow + told;
            told = tnow;
            if (tcnt >= ticks)
                break;
        }
    }
}

void delay_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++)
    {
        delay_us(1000);
    }
}
