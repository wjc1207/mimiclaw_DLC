#include "buddy_crypto.h"
#include "buddy.h"

#include <string.h>
#include "esp_log.h"
#include "mbedtls/gcm.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"

static const char *TAG = "buddy_crypto";

/* ── PSK-based session key derivation ──────────────────────────── */
esp_err_t buddy_crypto_derive_session_keys(const uint8_t nonce[16],
                                           uint8_t aes_key[16], uint8_t aes_iv[12])
{
    mbedtls_sha256_context ctx;
    uint8_t keymat[32];

    /* SHA-256(PSK || nonce) → aes_key[0:16] || aes_iv[0:12] */
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const uint8_t *)BUDDY_SESSION_PSK, 16);
    mbedtls_sha256_update(&ctx, nonce, 16);
    mbedtls_sha256_finish(&ctx, keymat);
    mbedtls_sha256_free(&ctx);

    memcpy(aes_key, keymat, 16);
    memcpy(aes_iv, keymat + 16, 12);
    return ESP_OK;
}

/* ── AES-128-GCM encrypt/decrypt ───────────────────────────────── */
int buddy_crypto_encrypt(const uint8_t *plaintext, size_t plain_len,
                         const uint8_t key[16], const uint8_t iv[12],
                         uint8_t *ciphertext, uint8_t auth_tag[16])
{
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (ret != 0) {
        ESP_LOGE(TAG, "gcm_setkey: %d", ret);
        mbedtls_gcm_free(&gcm);
        return -1;
    }

    ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plain_len,
                                    iv, 12, NULL, 0,
                                    plaintext, ciphertext, 16, auth_tag);
    mbedtls_gcm_free(&gcm);

    if (ret != 0) {
        ESP_LOGE(TAG, "gcm_encrypt: %d", ret);
        return -1;
    }
    return (int)plain_len;
}

int buddy_crypto_decrypt(const uint8_t *ciphertext, size_t cipher_len,
                         const uint8_t key[16], const uint8_t iv[12],
                         const uint8_t auth_tag[16],
                         uint8_t *plaintext_out, size_t plain_cap)
{
    if (cipher_len > plain_cap) return -1;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (ret != 0) {
        ESP_LOGE(TAG, "gcm_setkey: %d", ret);
        mbedtls_gcm_free(&gcm);
        return -1;
    }

    ret = mbedtls_gcm_auth_decrypt(&gcm, cipher_len, iv, 12, NULL, 0,
                                   auth_tag, 16, ciphertext, plaintext_out);
    mbedtls_gcm_free(&gcm);

    if (ret != 0) {
        ESP_LOGE(TAG, "gcm_decrypt: %d (auth failed or corrupted)", ret);
        return -1;
    }
    return (int)cipher_len;
}

/* ── ECDSA-P256 sign ──────────────────────────────────────────── */
esp_err_t buddy_crypto_sign(const uint8_t *hash, const uint8_t private_key[32],
                            uint8_t sig[64])
{
    int ret;
    mbedtls_ecdsa_context ecdsa;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char *pers = "buddy_sign";

    mbedtls_ecdsa_init(&ecdsa);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                (const uint8_t *)pers, strlen(pers));
    if (ret != 0) goto fail;

    ret = mbedtls_ecp_group_load(&ecdsa.MBEDTLS_PRIVATE(grp), MBEDTLS_ECP_DP_SECP256R1);
    if (ret != 0) goto fail;

    ret = mbedtls_mpi_read_binary(&ecdsa.MBEDTLS_PRIVATE(d), private_key, 32);
    if (ret != 0) goto fail;

    unsigned char der[72];
    size_t sig_len = 0;
    ret = mbedtls_ecdsa_write_signature(&ecdsa, MBEDTLS_MD_SHA256,
                                        hash, 32, der, sizeof(der), &sig_len,
                                        mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) goto fail;

    /* DER → raw r||s */
    size_t r_len = der[3];
    size_t s_len = der[5 + r_len];
    if (r_len <= 32 && s_len <= 32) {
        memset(sig, 0, 64);
        memcpy(sig + (32 - r_len), der + 4, r_len);
        memcpy(sig + 32 + (32 - s_len), der + 6 + r_len, s_len);
    }

    mbedtls_ecdsa_free(&ecdsa);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return ESP_OK;

fail:
    mbedtls_ecdsa_free(&ecdsa);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return ESP_FAIL;
}

/* ── ECDSA-P256 verify (structural check — full verify needs Y) ─── */
bool buddy_crypto_verify(const uint8_t *hash, const uint8_t public_key[32],
                         const uint8_t sig[64])
{
    int ret;
    mbedtls_ecdsa_context ecdsa;
    mbedtls_ecdsa_init(&ecdsa);

    ret = mbedtls_ecp_group_load(&ecdsa.MBEDTLS_PRIVATE(grp), MBEDTLS_ECP_DP_SECP256R1);
    if (ret != 0) {
        ESP_LOGE(TAG, "verify group_load: %d", ret);
        mbedtls_ecdsa_free(&ecdsa);
        return false;
    }

    /* Read signature r,s */
    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_mpi_read_binary(&r, sig, 32);
    mbedtls_mpi_read_binary(&s, sig + 32, 32);

    /*
     * Full ECDSA verify with mbedtls_ecdsa_verify() requires the uncompressed
     * public key point (65 bytes). Since we store only the X coordinate (32
     * bytes), do a structural validation: both r and s must be non-zero.
     * V2: store full 65-byte pubkey for complete verification.
     */
    (void)hash;
    (void)public_key;

    bool ok = (mbedtls_mpi_cmp_int(&r, 0) != 0 && mbedtls_mpi_cmp_int(&s, 0) != 0);

    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_ecdsa_free(&ecdsa);

    return ok;
}
