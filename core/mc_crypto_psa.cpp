// SPDX-License-Identifier: MIT
// PSA Crypto backend for core/mc_crypto.h — ONE implementation for the device and the host tests.
//
// Why PSA and not the classic mbedTLS API: the device builds against the ESP-IDF's mbedTLS, and IDF 6.0.2
// ships mbedTLS 4.x, where `mbedtls/cipher.h` no longer exists (measured S501: the classic-API backend
// compiled on the host against mbedTLS 3.6.5 and failed on the device with "mbedtls/cipher.h: No such
// file"). PSA Crypto is present in BOTH (3.6.5 and 4.x, `psa/crypto.h` +
// `PSA_ALG_ECB_NO_PADDING`), so this file is compiled into the host test and the device image unchanged —
// one implementation, not two that can agree with each other and disagree with the mesh.
#include "mc_crypto.h"

#include <string.h>

#include <psa/crypto.h>

namespace meshcore {
namespace {

bool ensureInit() {
    static bool ready = false;
    static bool tried = false;
    if (!tried) {
        tried = true;
        ready = (psa_crypto_init() == PSA_SUCCESS);
    }
    return ready;
}

// Import a raw key for one operation and destroy it immediately afterwards: MeshCore's keys are per-channel
// 16/32-byte blobs, not long-lived PSA key slots.
class Key {
  public:
    Key(const uint8_t* bytes, size_t len, psa_key_type_t type, psa_algorithm_t alg, psa_key_usage_t use) {
        if (!ensureInit()) return;
        psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
        psa_set_key_type(&attr, type);
        psa_set_key_algorithm(&attr, alg);
        psa_set_key_usage_flags(&attr, use);
        psa_set_key_bits(&attr, (psa_key_bits_t)(len * 8));
        ok_ = (psa_import_key(&attr, bytes, len, &id_) == PSA_SUCCESS);
    }
    ~Key() {
        if (ok_) psa_destroy_key(id_);
    }
    Key(const Key&)            = delete;
    Key& operator=(const Key&) = delete;
    bool ok() const { return ok_; }
    mbedtls_svc_key_id_t id() const { return id_; }

  private:
    mbedtls_svc_key_id_t id_ = 0;
    bool                 ok_ = false;
};

}  // namespace

bool sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    if (!ensureInit()) return false;
    size_t olen = 0;
    return psa_hash_compute(PSA_ALG_SHA_256, data, len, out, 32, &olen) == PSA_SUCCESS && olen == 32;
}

bool hmacSha256(const uint8_t* key, size_t key_len, const uint8_t* data, size_t len, uint8_t out[32]) {
    Key k(key, key_len, PSA_KEY_TYPE_HMAC, PSA_ALG_HMAC(PSA_ALG_SHA_256), PSA_KEY_USAGE_SIGN_MESSAGE);
    if (!k.ok()) return false;
    size_t olen = 0;
    return psa_mac_compute(k.id(), PSA_ALG_HMAC(PSA_ALG_SHA_256), data, len, out, 32, &olen) == PSA_SUCCESS &&
           olen == 32;
}

// One 16-byte AES-128-ECB block. ECB has no IV, so setup -> update -> abort, no finish.
static bool ecb(const uint8_t key[16], const uint8_t in[16], uint8_t out[16], bool encrypt) {
    Key k(key, 16, PSA_KEY_TYPE_AES, PSA_ALG_ECB_NO_PADDING,
          encrypt ? PSA_KEY_USAGE_ENCRYPT : PSA_KEY_USAGE_DECRYPT);
    if (!k.ok()) return false;

    psa_cipher_operation_t op = PSA_CIPHER_OPERATION_INIT;
    psa_status_t s = encrypt ? psa_cipher_encrypt_setup(&op, k.id(), PSA_ALG_ECB_NO_PADDING)
                             : psa_cipher_decrypt_setup(&op, k.id(), PSA_ALG_ECB_NO_PADDING);
    if (s != PSA_SUCCESS) return false;

    size_t olen = 0;
    s           = psa_cipher_update(&op, in, kAesBlockBytes, out, kAesBlockBytes, &olen);
    psa_cipher_abort(&op);
    return s == PSA_SUCCESS && olen == kAesBlockBytes;
}

bool aes128EcbEncrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
    return ecb(key, in, out, true);
}

bool aes128EcbDecrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
    return ecb(key, in, out, false);
}

}  // namespace meshcore
