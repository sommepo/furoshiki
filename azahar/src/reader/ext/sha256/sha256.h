// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// The reader layer's SHA-256 (sha256_starts, sha256_update, sha256_finish), as the other
// reader forks carry it in this folder: here OpenSSL's, which the emulator has.

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <openssl/evp.h>

struct sha256_context {
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> md{nullptr, &EVP_MD_CTX_free};
};

inline void sha256_starts(sha256_context* context) {
    context->md.reset(EVP_MD_CTX_new());
    if (!context->md || EVP_DigestInit_ex(context->md.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("SHA-256 is not available");
    }
}

inline void sha256_update(sha256_context* context, const std::uint8_t* data, std::uint32_t length) {
    EVP_DigestUpdate(context->md.get(), data, length);
}

inline void sha256_finish(sha256_context* context, std::uint8_t digest[32]) {
    unsigned int length = 0;
    EVP_DigestFinal_ex(context->md.get(), digest, &length);
}
