#include "bl_board.h"
#include "bl_crypto.h"
#include "bl_esp8266.h"
#include "bl_flash.h"
#include "ota_update.h"
#include "stm32f4xx_hal.h"

#define BL_PREFLIGHT_MAX_ATTEMPTS  4U
#define BL_RETRY_BASE_DELAY_MS     2000U
#define BL_RETRY_MAX_DELAY_MS      30000U
#define BL_RECOVERY_RETRY_DELAY_MS 30000U

typedef enum
{
    BL_STATE_STARTUP = 0,
    BL_STATE_CHECKING_MANIFEST,
    BL_STATE_WIFI,
    BL_STATE_PREFLIGHT,
    BL_STATE_ERASING,
    BL_STATE_DOWNLOADING,
    BL_STATE_VERIFYING,
    BL_STATE_COMMITTING,
    BL_STATE_TRIAL_BOOT,
    BL_STATE_RECOVERY
} BL_State_t;

/* These values remain visible to a debugger while the bootloader is stopped. */
volatile BL_State_t g_bl_state = BL_STATE_STARTUP;
volatile int32_t g_bl_last_error = OTA_ERROR_NONE;
volatile uint32_t g_bl_install_attempt = 0U;
volatile uint32_t g_bl_preflight_attempt = 0U;
volatile uint32_t g_bl_recovery_cycle = 0U;

__attribute__((naked, noreturn))
static void BL_StartApplication(uint32_t initial_sp __attribute__((unused)),
                                uint32_t reset_handler __attribute__((unused)))
{
    __asm volatile (
        "msr msp, r0\n"
        "cpsie i\n"
        "bx r1\n");
}

static uint32_t BL_RetryDelay(uint32_t attempt)
{
    uint32_t delay = BL_RETRY_BASE_DELAY_MS;

    while (attempt > 0U && delay < BL_RETRY_MAX_DELAY_MS)
    {
        delay <<= 1U;
        attempt--;
    }

    return delay > BL_RETRY_MAX_DELAY_MS ? BL_RETRY_MAX_DELAY_MS : delay;
}

__attribute__((noreturn))
static void BL_EnterRecovery(OTA_Error_t error)
{
    BL_Flash_Abort();
    g_bl_last_error = (int32_t)error;
    g_bl_state = BL_STATE_RECOVERY;

    while (1)
    {
        HAL_Delay(1000U);
    }
}

static OTA_Error_t BL_PreflightWithRetry(const OTA_Metadata_t *metadata)
{
    OTA_Error_t last_error = OTA_ERROR_WIFI;
    uint32_t attempt;

    for (attempt = 0U; attempt < BL_PREFLIGHT_MAX_ATTEMPTS; attempt++)
    {
        g_bl_preflight_attempt = attempt + 1U;
        g_bl_state = BL_STATE_WIFI;
        if (BL_ESP8266_InitWiFi() != 0U)
        {
            last_error = OTA_ERROR_WIFI;
        }
        else
        {
            g_bl_state = BL_STATE_PREFLIGHT;
            if (BL_ESP8266_PreflightImage(metadata) == 0U)
            {
                return OTA_ERROR_NONE;
            }
            last_error = OTA_ERROR_PREFLIGHT;
        }

        if ((attempt + 1U) < BL_PREFLIGHT_MAX_ATTEMPTS)
        {
            HAL_Delay(BL_RetryDelay(attempt));
        }
    }

    return last_error;
}

__attribute__((noreturn))
static void BL_JumpToApplication(void)
{
    uint32_t initial_sp = *(const uint32_t *)OTA_APPLICATION_ADDRESS;
    uint32_t reset_handler = *(const uint32_t *)(OTA_APPLICATION_ADDRESS + 4U);
    uint32_t index;

    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

    (void)HAL_RCC_DeInit();
    (void)HAL_DeInit();

    /* HAL_RCC_DeInit() reconfigures the HAL tick before returning. */
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

    for (index = 0U; index < 8U; index++)
    {
        NVIC->ICER[index] = 0xFFFFFFFFUL;
        NVIC->ICPR[index] = 0xFFFFFFFFUL;
    }

    SCB->VTOR = OTA_APPLICATION_ADDRESS;
    __set_CONTROL(0U);
    __set_PSP(0U);
    __set_BASEPRI(0U);
    __set_FAULTMASK(0U);
    __DSB();
    __ISB();
    BL_StartApplication(initial_sp, reset_handler);
}

__attribute__((noreturn))
static void BL_RecordFailure(OTA_Error_t error, uint8_t application_intact)
{
    OTA_Metadata_t current;
    OTA_Error_t installed_error;

    BL_Flash_Abort();
    if (application_intact &&
        (OTA_Metadata_Read(&current) != 0U ||
         !BL_Flash_ApplicationHeaderIsValid(current.installed_version)))
    {
        application_intact = 0U;
    }
    if (OTA_Metadata_MarkFailure(error, application_intact) != 0U)
    {
        BL_EnterRecovery(OTA_ERROR_METADATA);
    }

    if (application_intact)
    {
        if (OTA_Metadata_Read(&current) != 0U)
        {
            BL_EnterRecovery(OTA_ERROR_METADATA);
        }
        installed_error = BL_Flash_VerifyApplication(&current);
        if (installed_error != OTA_ERROR_NONE)
        {
            if (OTA_Metadata_MarkFailure(installed_error, 0U) != 0U)
            {
                BL_EnterRecovery(OTA_ERROR_METADATA);
            }
            HAL_Delay(BL_RECOVERY_RETRY_DELAY_MS);
            BL_Board_Reset();
            BL_EnterRecovery(installed_error);
        }
        BL_JumpToApplication();
    }

    HAL_Delay(BL_RECOVERY_RETRY_DELAY_MS);
    BL_Board_Reset();
    BL_EnterRecovery(error);
}

static uint8_t BL_ApplicationIsIntact(const OTA_Metadata_t *metadata)
{
    OTA_Metadata_t installed;

    if (metadata == NULL)
    {
        return 0U;
    }

    if (metadata->state == OTA_STATE_REQUESTED ||
        (metadata->state == OTA_STATE_INSTALLING &&
         (metadata->flags & OTA_METADATA_FLAG_FLASH_STARTED) == 0U))
    {
        return 1U;
    }

    if (metadata->state == OTA_STATE_INSTALLING &&
        OTA_Metadata_ReadConfirmedInstalled(metadata->installed_version,
                                            &installed) == 0U &&
        BL_Flash_VerifyApplication(&installed) == OTA_ERROR_NONE)
    {
        return 1U;
    }

    return 0U;
}

static OTA_Error_t BL_ValidateManifest(const OTA_Metadata_t *metadata)
{
    if (metadata == NULL)
    {
        return OTA_ERROR_INVALID_REQUEST;
    }
    if (metadata->board_id != OTA_BOARD_ID)
    {
        return OTA_ERROR_WRONG_BOARD;
    }
    if (metadata->target_version < metadata->installed_version ||
        (metadata->target_version == metadata->installed_version &&
         (metadata->flags & OTA_METADATA_FLAG_REINSTALL) == 0U))
    {
        return OTA_ERROR_DOWNGRADE;
    }
    if (!BL_OTA_VerifySignature(metadata->board_id,
                                metadata->target_version,
                                metadata->image_size,
                                metadata->image_crc32,
                                metadata->image_hash,
                                metadata->url,
                                metadata->url_length,
                                metadata->signature))
    {
        return OTA_ERROR_SIGNATURE;
    }

    return OTA_ERROR_NONE;
}

__attribute__((noreturn))
static void BL_RunInstallation(OTA_Metadata_t *metadata)
{
    OTA_Error_t error;

    while (1)
    {
        g_bl_state = BL_STATE_CHECKING_MANIFEST;
        error = BL_ValidateManifest(metadata);
        if (error != OTA_ERROR_NONE)
        {
            BL_RecordFailure(error, BL_ApplicationIsIntact(metadata));
        }

        if (metadata->state == OTA_STATE_INSTALLING &&
            (metadata->flags & OTA_METADATA_FLAG_FLASH_STARTED) != 0U &&
            BL_Flash_VerifyApplication(metadata) == OTA_ERROR_NONE)
        {
            g_bl_state = BL_STATE_COMMITTING;
            if (OTA_Metadata_MarkTrial() != 0U)
            {
                BL_EnterRecovery(OTA_ERROR_METADATA);
            }
            BL_Board_Reset();
            BL_EnterRecovery(OTA_ERROR_METADATA);
        }

        if (metadata->install_attempts >= OTA_MAX_INSTALL_ATTEMPTS)
        {
            BL_RecordFailure(OTA_ERROR_INSTALL_ATTEMPTS,
                             BL_ApplicationIsIntact(metadata));
        }

        error = BL_PreflightWithRetry(metadata);
        if (error != OTA_ERROR_NONE)
        {
            BL_RecordFailure(error, BL_ApplicationIsIntact(metadata));
        }

        /* No application Flash is touched before this persistent transition. */
        if (OTA_Metadata_BeginInstall() != 0U ||
            OTA_Metadata_Read(metadata) != 0U ||
            metadata->state != OTA_STATE_INSTALLING)
        {
            BL_EnterRecovery(OTA_ERROR_METADATA);
        }
        g_bl_install_attempt = metadata->install_attempts;

        if (OTA_Metadata_MarkFlashStarted() != 0U ||
            OTA_Metadata_Read(metadata) != 0U ||
            (metadata->flags & OTA_METADATA_FLAG_FLASH_STARTED) == 0U)
        {
            BL_EnterRecovery(OTA_ERROR_METADATA);
        }

        g_bl_state = BL_STATE_ERASING;
        if (BL_Flash_Begin(metadata->image_size) != 0U)
        {
            error = OTA_ERROR_FLASH_ERASE;
        }
        else
        {
            g_bl_state = BL_STATE_DOWNLOADING;
            if (BL_ESP8266_DownloadImage(metadata) != 0U)
            {
                error = OTA_ERROR_DOWNLOAD;
            }
            else
            {
                g_bl_state = BL_STATE_VERIFYING;
                error = BL_Flash_Finish(metadata->image_crc32,
                                        metadata->image_hash,
                                        metadata->target_version);
                if (error == OTA_ERROR_NONE)
                {
                    g_bl_state = BL_STATE_COMMITTING;
                    if (OTA_Metadata_MarkTrial() != 0U)
                    {
                        BL_EnterRecovery(OTA_ERROR_METADATA);
                    }

                    g_bl_last_error = OTA_ERROR_NONE;
                    BL_Board_Reset();
                    BL_EnterRecovery(OTA_ERROR_METADATA);
                }
            }
        }

        BL_Flash_Abort();
        g_bl_last_error = (int32_t)error;

        if (metadata->install_attempts >= OTA_MAX_INSTALL_ATTEMPTS)
        {
            BL_RecordFailure(OTA_ERROR_INSTALL_ATTEMPTS, 0U);
        }

        HAL_Delay(BL_RetryDelay(metadata->install_attempts - 1U));
        if (OTA_Metadata_Read(metadata) != 0U ||
            metadata->state != OTA_STATE_INSTALLING)
        {
            BL_EnterRecovery(OTA_ERROR_METADATA);
        }
    }
}

int main(void)
{
    OTA_Metadata_t metadata;
    OTA_Error_t verify_error;
    uint8_t recovery_retry;

    BL_Board_Init();

    if (OTA_Metadata_Read(&metadata) != 0U)
    {
        BL_EnterRecovery(OTA_ERROR_METADATA);
    }

    switch ((OTA_State_t)metadata.state)
    {
        case OTA_STATE_CONFIRMED:
            if (metadata.target_version != metadata.installed_version ||
                metadata.image_size < 8U)
            {
                BL_EnterRecovery(OTA_ERROR_INVALID_REQUEST);
            }
            verify_error = BL_Flash_VerifyApplication(&metadata);
            if (verify_error == OTA_ERROR_NONE)
            {
                BL_JumpToApplication();
            }
            BL_RecordFailure(verify_error, 0U);
            break;

        case OTA_STATE_REQUESTED:
        case OTA_STATE_INSTALLING:
            BL_RunInstallation(&metadata);
            break;

        case OTA_STATE_TRIAL:
            g_bl_state = BL_STATE_TRIAL_BOOT;
            if (metadata.trial_attempts >= OTA_MAX_TRIAL_ATTEMPTS)
            {
                BL_RecordFailure(OTA_ERROR_TRIAL_ATTEMPTS, 0U);
            }
            verify_error = BL_Flash_VerifyApplication(&metadata);
            if (verify_error != OTA_ERROR_NONE)
            {
                BL_RecordFailure(verify_error, 0U);
            }
            if (OTA_Metadata_RecordTrialBoot() != 0U)
            {
                BL_EnterRecovery(OTA_ERROR_METADATA);
            }
            BL_JumpToApplication();
            break;

        case OTA_STATE_RECOVERY:
            g_bl_state = BL_STATE_RECOVERY;
            g_bl_recovery_cycle = metadata.recovery_cycles;
            verify_error = BL_ValidateManifest(&metadata);
            if (verify_error != OTA_ERROR_NONE)
            {
                BL_EnterRecovery(verify_error);
            }
            recovery_retry = OTA_Metadata_RetryRecovery();
            if (recovery_retry == OTA_RECOVERY_RETRY_LIMIT_REACHED)
            {
                BL_EnterRecovery(metadata.error_code > OTA_ERROR_NONE ?
                                 (OTA_Error_t)metadata.error_code :
                                 OTA_ERROR_INSTALL_ATTEMPTS);
            }
            if (recovery_retry != OTA_RECOVERY_RETRY_OK ||
                OTA_Metadata_Read(&metadata) != 0U)
            {
                BL_EnterRecovery(OTA_ERROR_METADATA);
            }
            g_bl_recovery_cycle = metadata.recovery_cycles;
            BL_RunInstallation(&metadata);
            break;

        default:
            BL_EnterRecovery(OTA_ERROR_METADATA);
            break;
    }
}
