/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022-2025 Raspberry Pi Ltd
 */

#include "devicewrapperblockcacheentry.h"
#include <cstdlib>

DeviceWrapperBlockCacheEntry::DeviceWrapperBlockCacheEntry(const size_t blocksize)
    : dirty(false)
{
    /* Windows requires buffers to be 4k aligned when reading/writing raw disk devices */
    block = (char *) std::aligned_alloc(4096, blocksize);
}

DeviceWrapperBlockCacheEntry::~DeviceWrapperBlockCacheEntry()
{
    std::free(block);
}
