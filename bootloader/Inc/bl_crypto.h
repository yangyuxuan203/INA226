#ifndef BL_CRYPTO_H
#define BL_CRYPTO_H

#include <stddef.h>
#include <stdint.h>
#include "monocypher.h"

#define BL_OTA_HASH_SIZE       32U
#define BL_OTA_SIGNATURE_SIZE  64U

typedef struct
{
    crypto_blake2b_ctx state;
    uint8_t active;
} BL_OTA_HashContext_t;

uint8_t BL_OTA_HashInit(BL_OTA_HashContext_t *context);
uint8_t BL_OTA_HashUpdate(BL_OTA_HashContext_t *context,
                          const uint8_t *data,
                          size_t length);
uint8_t BL_OTA_HashFinish(BL_OTA_HashContext_t *context,
                          uint8_t hash[BL_OTA_HASH_SIZE]);
uint8_t BL_OTA_HashMatches(const uint8_t left[BL_OTA_HASH_SIZE],
                           const uint8_t right[BL_OTA_HASH_SIZE]);
uint8_t BL_OTA_VerifySignature(uint32_t board_id,
                               uint32_t image_version,
                               uint32_t image_size,
                               uint32_t image_crc32,
                               const uint8_t image_hash[BL_OTA_HASH_SIZE],
                               const char *url,
                               uint32_t url_length,
                               const uint8_t signature[BL_OTA_SIGNATURE_SIZE]);

#endif /* BL_CRYPTO_H */
