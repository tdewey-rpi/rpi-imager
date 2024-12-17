#ifndef DEVICEWRAPPERBLOCKCACHEENTRY_H
#define DEVICEWRAPPERBLOCKCACHEENTRY_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

#include <cstddef>

struct DeviceWrapperBlockCacheEntry {
    DeviceWrapperBlockCacheEntry(std::size_t blocksize = 4096);
    ~DeviceWrapperBlockCacheEntry();
    char *block;
    bool dirty;
};

#endif // DEVICEWRAPPERBLOCKCACHEENTRY_H
