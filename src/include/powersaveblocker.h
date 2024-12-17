#ifndef POWERSAVEBLOCKER_H
#define POWERSAVEBLOCKER_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

#include <memory>
#include <vector>

#include <cstdint>

struct PowerSaveBlocker
{
    explicit PowerSaveBlocker();
    virtual ~PowerSaveBlocker();
    void applyBlock(const std::string &reason);
    void removeBlock();

private:
    struct impl;
    std::unique_ptr<impl> p_Impl;
};

#endif // POWERSAVEBLOCKER_H
