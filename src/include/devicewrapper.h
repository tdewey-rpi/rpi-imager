#ifndef DEVICEWRAPPER_H
#define DEVICEWRAPPER_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022-2025 Raspberry Pi Ltd
 */

#include <map>
#include <cstdint>

class DeviceWrapperBlockCacheEntry;
class DeviceWrapperFatPartition;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#include "windows/winfile.h"
typedef WinFile DeviceWrapperFile;
#elif defined(__APPLE__)
#include "mac/macfile.h"
typedef MacFile DeviceWrapperFile;
#else
typedef QFile DeviceWrapperFile;
#endif


class DeviceWrapper {
public:
    explicit DeviceWrapper(DeviceWrapperFile *file);
    virtual ~DeviceWrapper();
    void sync();
    void pwrite(const char *buf, uint64_t size, uint64_t offset);
    void pread(char *buf, uint64_t size, uint64_t offset);
    DeviceWrapperFatPartition *fatPartition(int nr);

protected:
    bool _dirty;
    std::map<uint64_t,DeviceWrapperBlockCacheEntry *> _blockcache;
    DeviceWrapperFile *_file;

    void _readIntoBlockCacheIfNeeded(uint64_t offset, uint64_t size);
    void _seekToBlock(uint64_t blockNr);
};

#endif // DEVICEWRAPPER_H
