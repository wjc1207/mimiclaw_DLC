#pragma once

#include "buddy.h"
#include <stdint.h>
#include <stddef.h>

/**
 * Derive per-session AES-128 key + IV from PSK and nonce.
 * SHA-256(PSK || nonce) → aes_key[0:16] || aes_iv[0:12].
 * Both sides call this with the same nonce (initiator's).
 */
esp_err_t buddy_crypto_derive_session_keys(const uint8_t nonce[16],
                                           uint8_t aes_key[16], uint8_t aes_iv[12]);

/**
 * Encrypt profile JSON with AES-128-GCM.
 * ciphertext and auth_tag are outputs; caller provides buffers.
 * Returns ciphertext length, or -1 on error.
 */
int buddy_crypto_encrypt(const uint8_t *plaintext, size_t plain_len,
                         const uint8_t key[16], const uint8_t iv[12],
                         uint8_t *ciphertext, uint8_t auth_tag[16]);

/**
 * Decrypt profile payload with AES-128-GCM.
 * plaintext_out is caller-provided; returns plaintext length, or -1 on error.
 */
int buddy_crypto_decrypt(const uint8_t *ciphertext, size_t cipher_len,
                         const uint8_t key[16], const uint8_t iv[12],
                         const uint8_t auth_tag[16],
                         uint8_t *plaintext_out, size_t plain_cap);

/**
 * ECDSA-P256 sign a 32-byte hash. Signature output: 64 bytes (r||s).
 */
esp_err_t buddy_crypto_sign(const uint8_t *hash, const uint8_t private_key[32],
                            uint8_t sig[64]);

/**
 * Verify ECDSA-P256 signature.
 */
bool buddy_crypto_verify(const uint8_t *hash, const uint8_t public_key[32],
                         const uint8_t sig[64]);
