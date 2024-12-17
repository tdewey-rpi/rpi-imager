/*
 * Use CommonCrypto on macOS for SHA256
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2024 Raspberry Pi Ltd
 */

#include <array>
#include <vector>

#include "acceleratedcryptographichash.h"

#include <CommonCrypto/CommonDigest.h>

struct AcceleratedCryptographicHash::impl {
    explicit impl(Algorithm algo) {
        if (algo != Algorithm::SHA256)
            throw std::runtime_error("Only sha256 implemented");

        CC_SHA256_Init(&_sha256);
    }

    void addData(const void *data, int length)
    {
        CC_SHA256_Update(&_sha256, data, length);
    }

    void addData(const std::vector<uint8_t> &data)
    {
        addData(data.data(), data.size());
    }

    std::array<uint8_t, CC_SHA256_DIGEST_LENGTH> result() {
        CC_SHA256_Final(resultHash.data(), &_sha256);
        return resultHash;
    }

private:
    CC_SHA256_CTX _sha256;
    std::array<uint8_t, CC_SHA256_DIGEST_LENGTH> resultHash;
};

AcceleratedCryptographicHash::AcceleratedCryptographicHash(Algorithm method)
    : p_Impl(std::make_unique<impl>(method)) {}

AcceleratedCryptographicHash::~AcceleratedCryptographicHash() = default;

void AcceleratedCryptographicHash::addData(const char *data, int length) {
    p_Impl->addData(data, length);
}
void AcceleratedCryptographicHash::addData(const std::vector<uint8_t> &data) {
    p_Impl->addData(data);
}
const std::array<uint8_t, CC_SHA256_DIGEST_LENGTH> AcceleratedCryptographicHash::result() {
    return p_Impl->result();
}
