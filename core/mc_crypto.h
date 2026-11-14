// SPDX-License-Identifier: MIT
// The four crypto primitives MeshCore needs, behind one tiny interface.
//
// Why an interface: the wire format is portable C++ that both the device and the host test compile, but
// the primitives come from mbedTLS — the ESP-IDF's own copy on the device, the system copy on the host.
// Both are mbedTLS 3.x, so mc_crypto_mbedtls.cpp is the ONE implementation for both, and the host test
// therefore exercises exactly the code the device runs.
//
// What MeshCore uses, read from lib/meshcore/Utils.cpp (S501):
//   · SHA-256 over the channel key (16 bytes) -> the 1-byte channel hash;
//   · AES-128-ECB, no padding in the primitive: MeshCore zero-pads the final partial block itself;
//   · HMAC-SHA256 over the ciphertext, truncated to 2 bytes.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace meshcore {

// AES block size: MeshCore's CIPHER_BLOCK_SIZE. Here rather than in mc_wire.h because it is the
// primitive's own size, and the crypto backend must not depend on the wire format.
constexpr size_t kAesBlockBytes = 16;

bool sha256(const uint8_t* data, size_t len, uint8_t out[32]);

bool hmacSha256(const uint8_t* key, size_t key_len, const uint8_t* data, size_t len, uint8_t out[32]);

// One 16-byte block, ECB. MeshCore encrypts block by block, so the interface is the same shape.
bool aes128EcbEncrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
bool aes128EcbDecrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

}  // namespace meshcore
