#include "bl_board.h"

static UART_HandleTypeDef s_uart2;

static void BL_SystemClock_Config(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clock = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    oscillator.HSIState = RCC_HSI_ON;
    oscillator.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    oscillator.PLL.PLLM = 8U;
    oscillator.PLL.PLLN = 168U;
    oscillator.PLL.PLLP = RCC_PLLP_DIV2;
    oscillator.PLL.PLLQ = 4U;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
    {
        while (1) {}
    }

    clock.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                      RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = RCC_HCLK_DIV4;
    clock.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_5) != HAL_OK)
    {
        while (1) {}
    }
}

static void BL_USART2_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);

    s_uart2.Instance = USART2;
    s_uart2.Init.BaudRate = 115200U;
    s_uart2.Init.WordLength = UART_WORDLENGTH_8B;
    s_uart2.Init.StopBits = UART_STOPBITS_1;
    s_uart2.Init.Parity = UART_PARITY_NONE;
    s_uart2.Init.Mode = UART_MODE_TX_RX;
    s_uart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_uart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&s_uart2) != HAL_OK)
    {
        while (1) {}
    }
}

void BL_Board_Init(void)
{
    HAL_Init();
    BL_SystemClock_Config();
    BL_USART2_Init();
}

void BL_Board_Reset(void)
{
    NVIC_SystemReset();
}

uint8_t BL_UART_Write(const uint8_t *data, uint16_t length, uint32_t timeout_ms)
{
    if (data == NULL || length == 0U)
    {
        return 1U;
    }

    return HAL_UART_Transmit(&s_uart2, (uint8_t *)data, length, timeout_ms) == HAL_OK ? 0U : 1U;
}

uint8_t BL_UART_ReadByte(uint8_t *byte, uint32_t timeout_ms)
{
    if (byte == NULL)
    {
        return 1U;
    }

    return HAL_UART_Receive(&s_uart2, byte, 1U, timeout_ms) == HAL_OK ? 0U : 1U;
}

void BL_UART_Flush(void)
{
#define BL_UART_FLUSH_MAX_MS 200U
    uint8_t byte;
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < BL_UART_FLUSH_MAX_MS &&
           BL_UART_ReadByte(&byte, 2U) == 0U) {}
    __HAL_UART_CLEAR_OREFLAG(&s_uart2);
    s_uart2.ErrorCode = HAL_UART_ERROR_NONE;
#undef BL_UART_FLUSH_MAX_MS
}
