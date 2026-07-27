#include "app_clock.h"

#include "app_state.h"
#include "main.h"

#include <stdio.h>
#include <string.h>

static uint8_t AppClock_MonthFromText(const char *month_text)
{
    static const char *names[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    uint8_t index;

    for (index = 0U; index < 12U; index++)
    {
        if (strncmp(month_text, names[index], 3U) == 0)
        {
            return (uint8_t)(index + 1U);
        }
    }
    return 0U;
}

static uint8_t AppClock_IsLeapYear(uint16_t year)
{
    return ((year % 4U == 0U && year % 100U != 0U) ||
            year % 400U == 0U) ? 1U : 0U;
}

static uint8_t AppClock_DaysInMonth(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };

    if (month == 2U && AppClock_IsLeapYear(year) != 0U)
    {
        return 29U;
    }
    if (month >= 1U && month <= 12U)
    {
        return days[month - 1U];
    }
    return 31U;
}

static void AppClock_IncrementOneSecond(BeijingClock_t *clock)
{
    if (++clock->second < 60U)
    {
        return;
    }
    clock->second = 0U;

    if (++clock->minute < 60U)
    {
        return;
    }
    clock->minute = 0U;

    if (++clock->hour < 24U)
    {
        return;
    }
    clock->hour = 0U;

    clock->day++;
    if (clock->day <= AppClock_DaysInMonth(clock->year, clock->month))
    {
        return;
    }
    clock->day = 1U;

    if (++clock->month <= 12U)
    {
        return;
    }
    clock->month = 1U;
    clock->year++;
}

static void AppClock_Format(const BeijingClock_t *clock,
                            char *text,
                            uint32_t text_size)
{
    if (text == NULL || text_size == 0U)
    {
        return;
    }

    if (clock == NULL || clock->valid == 0U)
    {
        (void)snprintf(text, text_size, "--:--:--");
        return;
    }

    (void)snprintf(text, text_size,
                   "%04u-%02u-%02u %02u:%02u:%02u",
                   clock->year, clock->month, clock->day,
                   clock->hour, clock->minute, clock->second);
}

static void AppClock_StoreUnparsedText(const char *raw_time)
{
    BeijingClock_t current_clock = {0};

    (void)AppState_GetBeijingTime(&current_clock, NULL, 0U);
    (void)AppState_SetBeijingTime(&current_clock, raw_time);
}

void AppClock_Service(void)
{
    BeijingClock_t clock;
    char text[APP_STATE_BEIJING_TIME_LENGTH];
    uint8_t changed = 0U;

    if (AppState_GetBeijingTime(&clock, NULL, 0U) != 0U ||
        clock.valid == 0U)
    {
        return;
    }

    while ((HAL_GetTick() - clock.tick_ms) >= 1000U)
    {
        clock.tick_ms += 1000U;
        AppClock_IncrementOneSecond(&clock);
        changed = 1U;
    }

    if (changed != 0U)
    {
        AppClock_Format(&clock, text, sizeof(text));
        (void)AppState_SetBeijingTime(&clock, text);
    }
}

uint8_t AppClock_IsValid(void)
{
    BeijingClock_t clock;

    if (AppState_GetBeijingTime(&clock, NULL, 0U) != 0U)
    {
        return 0U;
    }
    return clock.valid != 0U ? 1U : 0U;
}

uint8_t AppClock_SetFromSntp(const char *raw_time)
{
    BeijingClock_t clock = {0};
    char day_name[4] = {0};
    char month_text[4] = {0};
    char display_text[APP_STATE_BEIJING_TIME_LENGTH];
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    if (raw_time == NULL)
    {
        return 1U;
    }

    if (sscanf(raw_time, "%3s %3s %d %d:%d:%d %d",
               day_name, month_text, &day, &hour, &minute, &second,
               &year) == 7)
    {
        month = (int)AppClock_MonthFromText(month_text);
    }
    else if (sscanf(raw_time, "%d-%d-%d %d:%d:%d",
                    &year, &month, &day, &hour, &minute, &second) != 6)
    {
        AppClock_StoreUnparsedText(raw_time);
        return 1U;
    }

    if (year < 2020 || month < 1 || month > 12 || day < 1 ||
        day > (int)AppClock_DaysInMonth((uint16_t)year, (uint8_t)month) ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59)
    {
        AppClock_StoreUnparsedText(raw_time);
        return 1U;
    }

    clock.year = (uint16_t)year;
    clock.month = (uint8_t)month;
    clock.day = (uint8_t)day;
    clock.hour = (uint8_t)hour;
    clock.minute = (uint8_t)minute;
    clock.second = (uint8_t)second;
    clock.tick_ms = HAL_GetTick();
    clock.valid = 1U;
    AppClock_Format(&clock, display_text, sizeof(display_text));
    return AppState_SetBeijingTime(&clock, display_text);
}
