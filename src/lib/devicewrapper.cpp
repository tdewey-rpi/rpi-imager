/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022-2025 Raspberry Pi Ltd
 */

#include <iostream>

#include <cstdint>

#include "devicewrapper.h"
#include "devicewrapperblockcacheentry.h"
#include "devicewrapperstructs.h"
#include "devicewrapperfatpartition.h"

DeviceWrapper::DeviceWrapper(DeviceWrapperFile *file)
    : _dirty(false), _file(file)
{

}

DeviceWrapper::~DeviceWrapper()
{
    sync();
}

void DeviceWrapper::_seekToBlock(uint64_t blockNr)
{
    if (!_file->seek(blockNr*4096))
    {
        throw std::runtime_error("Error seeking device");
    }
}

void DeviceWrapper::sync()
{
    if (!_dirty)
        return;

    for (auto&& block : _blockcache)
    {
        if (block.first == 0)
            continue; /* Save writing first block with MBR for last */

        if (!block.second->dirty)
            continue;

        _seekToBlock(block.first);
        if (_file->write(block.second->block, 4096) != 4096)
        {
            std::string errmsg = "Error writing to device";
            throw std::runtime_error(errmsg);
        }
        block.second->dirty = false;
    }

    if (_blockcache.contains(0))
    {
        /* Write first block with MBR */
        auto block = _blockcache.at(0);

        if (block->dirty)
        {
            _seekToBlock(0);
            if (_file->write(block->block, 4096) != 4096)
            {
                std::string errmsg = "Error writing MBR to device";
                throw std::runtime_error(errmsg);
            }
            block->dirty = false;
        }
    }

    _dirty = false;
}

void DeviceWrapper::_readIntoBlockCacheIfNeeded(uint64_t offset, uint64_t size)
{
    if (!size)
        return;

    uint64_t firstBlock = offset/4096;
    uint64_t lastBlock = (offset+size)/4096;

    for (auto i = firstBlock; i <= lastBlock; i++)
    {
        if (!_blockcache.contains(i))
        {
            _seekToBlock(i);

            auto cacheEntry = new DeviceWrapperBlockCacheEntry();
            int bytesRead = _file->read(cacheEntry->block, 4096);
            if (bytesRead != 4096)
            {
                std::string errmsg = "Error reading from device";
                throw std::runtime_error(errmsg);
            }
            _blockcache[i] = cacheEntry;
        }
    }
}

void DeviceWrapper::pread(char *buf, uint64_t size, uint64_t offset)
{
    if (!size)
        return;

    _readIntoBlockCacheIfNeeded(offset, size);
    uint64_t firstBlock = offset / 4096;
    uint64_t offsetInBlock = offset % 4096;

    for (auto i = firstBlock; size; i++)
    {
        auto block = _blockcache.at(i);
        size_t bytesToCopyFromBlock = std::min(4096-offsetInBlock, size);
        memcpy(buf, block->block + offsetInBlock, bytesToCopyFromBlock);

        buf  += bytesToCopyFromBlock;
        size -= bytesToCopyFromBlock;
        offsetInBlock = 0;
    }
}

void DeviceWrapper::pwrite(const char *buf, uint64_t size, uint64_t offset)
{
    if (!size)
        return;

    uint64_t firstBlock = offset / 4096;
    uint64_t offsetInBlock = offset % 4096;

    if (offsetInBlock || size % 4096)
    {
        /* Need to read existing data from disk
           as we will only be replacing a part of a block. */
        _readIntoBlockCacheIfNeeded(offset, size);
    }

    for (auto i = firstBlock; size; i++)
    {
        auto block = _blockcache.at(i);
        if (!block)
        {
            block = new DeviceWrapperBlockCacheEntry();
            _blockcache[i] = block;
        }

        block->dirty = true;
        size_t bytesToCopyFromBlock = std::min(4096-offsetInBlock, size);
        memcpy(block->block + offsetInBlock, buf, bytesToCopyFromBlock);

        buf  += bytesToCopyFromBlock;
        size -= bytesToCopyFromBlock;
        offsetInBlock = 0;
    }

    _dirty = true;
}

DeviceWrapperFatPartition *DeviceWrapper::fatPartition(int nr)
{
    if (nr > 4 || nr < 1)
        throw std::runtime_error("Only basic partitions 1-4 supported");

    /* GPT table handling */
    struct gpt_header gpt;
    struct gpt_partition gptpart;
    pread((char *) &gpt, sizeof(gpt), 512);

    if (!strncmp("EFI PART", gpt.Signature, 8) && gpt.MyLBA == 1)
    {
        std::cout << "Using GPT partition table";
        if (nr > gpt.NumberOfPartitionEntries)
            throw std::runtime_error("Partition does not exist");

        pread((char *) &gptpart, sizeof(gptpart), gpt.PartitionEntryLBA*512 + gpt.SizeOfPartitionEntry*(nr-1));

        return new DeviceWrapperFatPartition(this, gptpart.StartingLBA*512, (gptpart.EndingLBA-gptpart.StartingLBA+1)*512);
    }

    /* MBR table handling */
    struct mbr_table mbr;
    pread((char *) &mbr, sizeof(mbr), 0);

    if (mbr.signature[0] != 0x55 || mbr.signature[1] != 0xAA)
        throw std::runtime_error("MBR does not have valid signature");

    if (!mbr.part[nr-1].starting_sector || !mbr.part[nr-1].nr_of_sectors)
        throw std::runtime_error("Partition does not exist");

    return new DeviceWrapperFatPartition(this, mbr.part[nr-1].starting_sector*512, mbr.part[nr-1].nr_of_sectors*512);
}

