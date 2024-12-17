/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

#include "devicewrapperpartition.h"
#include "devicewrapper.h"

DeviceWrapperPartition::DeviceWrapperPartition(DeviceWrapper *dw, std::size_t partStart, std::size_t partLen)
    : _dw(dw), _partStart(partStart), _partLen(partLen), _offset(partStart)
{
    _partEnd = _partStart + _partLen;
}

DeviceWrapperPartition::~DeviceWrapperPartition()
{

}

void DeviceWrapperPartition::read(std::vector<uint8_t> &data, const std::size_t size)
{
    if (_offset+size > _partEnd)
    {
        throw std::runtime_error("Error: trying to read beyond partition");
    }

    if (data.size() < size) {
        data.resize(size);
    }

    _dw->pread(reinterpret_cast<char *>(data.data()), size, _offset);
    _offset += size;
}

void DeviceWrapperPartition::seek(const std::size_t pos)
{
    if (pos > _partLen)
    {
        throw std::runtime_error("Error: trying to seek beyond partition");
    }
    _offset = pos+_partStart;
}

std::size_t DeviceWrapperPartition::pos() const
{
    return _offset-_partStart;
}

void DeviceWrapperPartition::write(const std::vector<uint8_t> &data)
{
    if ((_offset + data.size()) > _partEnd)
    {
        // Thow an exception, because this absolutely must be a logic problem.
        throw std::runtime_error("Error: trying to write beyond partition");
    }

    _dw->pwrite(reinterpret_cast<const char *>(data.data()), data.size(), _offset);
    _offset += data.size();
}
