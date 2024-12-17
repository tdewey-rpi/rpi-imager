#ifndef DEVICEWRAPPERFATPARTITION_H
#define DEVICEWRAPPERFATPARTITION_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

#include <chrono>
#include <string>
#include <vector>
#include <list>
#include <cstdint>

#include "devicewrapperpartition.h"

enum fatType { FAT12, FAT16, FAT32, EXFAT };
struct dir_entry;

class DeviceWrapperFatPartition : public DeviceWrapperPartition
{
public:
    DeviceWrapperFatPartition(DeviceWrapper *dw, uint64_t partStart, uint64_t partLen);

    std::vector<uint8_t> readFile(const std::string &filename);
    void writeFile(const std::string &filename, const std::vector<uint8_t> &contents);
    bool fileExists(const std::string &filename);

protected:
    enum fatType _type;
    uint32_t _firstFatStartOffset, _fatSize, _bytesPerCluster, _clusterOffset;
    uint32_t _fat16_rootDirSectors, _fat16_firstRootDirSector;
    uint32_t _fat32_firstRootDirCluster, _fat32_currentRootDirCluster;
    uint16_t _bytesPerSector, _fat32_fsinfoSector;
    std::list<uint32_t> _fatStartOffset;
    std::list<uint32_t> _currentDirClusters;

    std::list<uint32_t> getClusterChain(uint32_t firstCluster);
    void setFAT16(uint16_t cluster, uint16_t value);
    void setFAT32(uint32_t cluster, uint32_t value);
    void setFAT(uint32_t cluster, uint32_t value);
    uint32_t getFAT(uint32_t cluster);
    void seekCluster(uint32_t cluster);
    uint32_t allocateCluster();
    uint32_t allocateCluster(uint32_t previousCluster);
    bool getDirEntry(const std::string &longFilename, struct dir_entry *entry, bool createIfNotExist = false);
    bool dirNameExists(const std::string &dirname);
    void updateDirEntry(struct dir_entry *dirEntry);
    void writeDirEntryAtCurrentPos(struct dir_entry *dirEntry);
    void openDir();
    bool readDir(struct dir_entry *result);
    void updateFSinfo(int deltaClusters, uint32_t nextFreeClusterHint);
    uint16_t TimeToFATtime(const std::chrono::zoned_time &time);
    uint16_t DateToFATdate(const std::chrono::zoned_time &date);
};

#endif // DEVICEWRAPPERFATPARTITION_H
