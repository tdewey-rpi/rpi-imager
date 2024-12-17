#ifndef ACCELERATEDCRYPTOGRAPHICHASH_H
#define ACCELERATEDCRYPTOGRAPHICHASH_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <memory>
#include <vector>

#include <cstdint>

struct AcceleratedCryptographicHash
{
    enum class Algorithm {
        SHA256,
    };
    explicit AcceleratedCryptographicHash(Algorithm method);
    ~AcceleratedCryptographicHash();
    void addData(const char *data, int length);
    void addData(const std::vector<uint8_t> &data);
    const std::array<uint8_t, 32> result();

private:
    struct impl;
    std::unique_ptr<impl> p_Impl;
};

#endif // ACCELERATEDCRYPTOGRAPHICHASH_H
