#ifndef DEVICEWRAPPERPARTITION_H
#define DEVICEWRAPPERPARTITION_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022-2025 Raspberry Pi Ltd
 */

#include <vector>

#include <cstddef>
#include <cstdint>

class DeviceWrapper;

class DeviceWrapperPartition {
public:
    explicit DeviceWrapperPartition(DeviceWrapper *dw, std::size_t partStart, std::size_t partLen);
    virtual ~DeviceWrapperPartition();
    void read(std::vector<uint8_t> &data, std::size_t size);
    void seek(std::size_t pos);
    std::size_t pos() const;
    void write(const std::vector<uint8_t> &data);

protected:
    DeviceWrapper *_dw;
    std::size_t _partStart, _partLen, _partEnd, _offset;
};

#endif // DEVICEWRAPPERPARTITION_H
