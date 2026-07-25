#include "bl_crypto.h"
#include "monocypher-ed25519.h"
#include "ota_update.h"
#include "ota_public_key.h"

_Static_assert(BL_OTA_HASH_SIZE == OTA_IMAGE_HASH_SIZE,
               "Bootloader and metadata hash sizes must match");
_Static_assert(BL_OTA_SIGNATURE_SIZE == OTA_SIGNATURE_SIZE,
               "Bootloader and metadata signature sizes must match");

static const uint8_t s_manifest_domain[] = "STM32F407-OTA-V3";

static void BL_WriteLE32(uint8_t output[4], uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

uint8_t BL_OTA_HashInit(BL_OTA_HashContext_t *context)
{
    if (context == NULL)
    {
        return 1U;
    }

    crypto_blake2b_init(&context->state, BL_OTA_HASH_SIZE);
    context->active = 1U;
    return 0U;
}

uint8_t BL_OTA_HashUpdate(BL_OTA_HashContext_t *context,
                          const uint8_t *data,
                          size_t length)
{
    if (context == NULL || !context->active ||
        (data == NULL && length != 0U))
    {
        return 1U;
    }

    crypto_blake2b_update(&context->state, data, length);
    return 0U;
}

uint8_t BL_OTA_HashFinish(BL_OTA_HashContext_t *context,
                          uint8_t hash[BL_OTA_HASH_SIZE])
{
    if (context == NULL || hash == NULL || !context->active)
    {
        return 1U;
    }

    context->active = 0U;
    crypto_blake2b_final(&context->state, hash);
    return 0U;
}

uint8_t BL_OTA_HashMatches(const uint8_t left[BL_OTA_HASH_SIZE],
                           const uint8_t right[BL_OTA_HASH_SIZE])
{
    if (left == NULL || right == NULL)
    {
        return 0U;
    }

    return crypto_verify32(left, right) == 0 ? 1U : 0U;
}

uint8_t BL_OTA_VerifySignature(uint32_t board_id,
                               uint32_t image_version,
                               uint32_t image_size,
                               uint32_t image_crc32,
                               const uint8_t image_hash[BL_OTA_HASH_SIZE],
                               const char *url,
                               uint32_t url_length,
                               const uint8_t signature[BL_OTA_SIGNATURE_SIZE])
{
    uint8_t message[(sizeof(s_manifest_domain) - 1U) + 20U +
                    BL_OTA_HASH_SIZE + OTA_URL_MAX_LENGTH - 1U];
    uint8_t *cursor = message;
    size_t message_length;

    if (board_id != OTA_BOARD_ID || image_size == 0U || image_version == 0U ||
        image_hash == NULL || url == NULL || url_length == 0U ||
        url_length >= OTA_URL_MAX_LENGTH || url[url_length] != '\0' ||
        signature == NULL)
    {
        return 0U;
    }

    for (size_t i = 0U; i < (sizeof(s_manifest_domain) - 1U); i++)
    {
        *cursor++ = s_manifest_domain[i];
    }
    BL_WriteLE32(cursor, board_id);
    cursor += 4U;
    BL_WriteLE32(cursor, image_version);
    cursor += 4U;
    BL_WriteLE32(cursor, image_size);
    cursor += 4U;
    BL_WriteLE32(cursor, image_crc32);
    cursor += 4U;
    BL_WriteLE32(cursor, url_length);
    cursor += 4U;
    for (size_t i = 0U; i < BL_OTA_HASH_SIZE; i++)
    {
        *cursor++ = image_hash[i];
    }
    for (size_t i = 0U; i < url_length; i++)
    {
        *cursor++ = (uint8_t)url[i];
    }

    message_length = (size_t)(cursor - message);

    return crypto_ed25519_check(signature, OTA_ED25519_PUBLIC_KEY,
                                message, message_length) == 0 ? 1U : 0U;
}
