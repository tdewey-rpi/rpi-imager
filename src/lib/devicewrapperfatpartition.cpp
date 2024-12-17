#include "devicewrapperfatpartition.h"
#include "devicewrapperstructs.h"
#include <QDebug>

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

DeviceWrapperFatPartition::DeviceWrapperFatPartition(DeviceWrapper *dw, uint64_t partStart, uint64_t partLen)
    : DeviceWrapperPartition(dw, partStart, partLen)
{
    union fat_bpb bpb;

    read((char *) &bpb, sizeof(bpb));

    if (bpb.fat16.Signature[0] != 0x55 || bpb.fat16.Signature[1] != 0xAA)
        throw std::runtime_error("Partition does not have a FAT file system");

    /* Determine FAT type as per p. 14 https://academy.cba.mit.edu/classes/networking_communications/SD/FAT.pdf */
    _bytesPerSector = bpb.fat16.BPB_BytsPerSec;
    uint32_t totalSectors, dataSectors, countOfClusters;
    _fat16_rootDirSectors = ((bpb.fat16.BPB_RootEntCnt * 32) + (_bytesPerSector - 1)) / _bytesPerSector;

    if (bpb.fat16.BPB_FATSz16)
        _fatSize = bpb.fat16.BPB_FATSz16;
    else
        _fatSize = bpb.fat32.BPB_FATSz32;

    if (bpb.fat16.BPB_TotSec16)
        totalSectors = bpb.fat16.BPB_TotSec16;
    else
        totalSectors = bpb.fat32.BPB_TotSec32;

    dataSectors = totalSectors - (bpb.fat16.BPB_RsvdSecCnt + (bpb.fat16.BPB_NumFATs * _fatSize) + _fat16_rootDirSectors);
    countOfClusters = dataSectors / bpb.fat16.BPB_SecPerClus;
    _bytesPerCluster = bpb.fat16.BPB_SecPerClus * _bytesPerSector;
    _fat16_firstRootDirSector = bpb.fat16.BPB_RsvdSecCnt + (bpb.fat16.BPB_NumFATs * bpb.fat16.BPB_FATSz16);
    _fat32_firstRootDirCluster = bpb.fat32.BPB_RootClus;

    if (!_bytesPerSector)
        _type = EXFAT;
    else if (countOfClusters < 4085)
        _type = FAT12;
    else if (countOfClusters < 65525)
        _type = FAT16;
    else
        _type = FAT32;

    if (_type == FAT12)
        throw std::runtime_error("FAT12 file system not supported");
    if (_type == EXFAT)
        throw std::runtime_error("exFAT file system not supported");
    if (_bytesPerSector % 4)
        throw std::runtime_error("FAT file system: invalid bytes per sector");

    _firstFatStartOffset = bpb.fat16.BPB_RsvdSecCnt * _bytesPerSector;
    for (int i = 0; i < bpb.fat16.BPB_NumFATs; i++)
    {
        _fatStartOffset.push_back(_firstFatStartOffset + (i * _fatSize * _bytesPerSector));
    }

    if (_type == FAT16)
    {
        _fat32_fsinfoSector = 0;
        _clusterOffset = (_fat16_firstRootDirSector+_fat16_rootDirSectors) * _bytesPerSector;
    }
    else
    {
        _fat32_fsinfoSector = bpb.fat32.BPB_FSInfo;
        _clusterOffset = _firstFatStartOffset + (bpb.fat16.BPB_NumFATs * _fatSize * _bytesPerSector);
    }
}

uint32_t DeviceWrapperFatPartition::allocateCluster()
{
    //char sector[_bytesPerSector];
    std::vector<uint8_t> sector(_bytesPerSector);
    const int entriesPerSector = _bytesPerSector/(_type == FAT16 ? 2 : 4);
    uint32_t cluster;
    uint16_t *f16 = reinterpret_cast<uint16_t *>(sector.data());
    uint32_t *f32 = reinterpret_cast<uint32_t *>(sector.data());

    seek(_firstFatStartOffset);

    for (int i = 0; i < _fatSize; i++)
    {
        read(sector, sizeof(sector));

        for (int j=0; j < entriesPerSector; j++)
        {
            if (_type == FAT16)
            {
                if (f16[j] == 0)
                {
                    /* Found available FAT16 cluster, mark it used/EOF */
                    cluster = j+i*entriesPerSector;
                    setFAT16(cluster, 0xFFFF);
                    return cluster;
                }
            }
            else
            {
                if ( (f32[j] & 0x0FFFFFFF) == 0)
                {
                    /* Found available FAT32 cluster, mark it used/EOF */
                    cluster = j+i*entriesPerSector;
                    setFAT32(cluster, 0xFFFFFFF);
                    updateFSinfo(-1, cluster);
                    return cluster;
                }
            }
        }
    }

    throw std::runtime_error("Out of disk space on FAT partition");
}

uint32_t DeviceWrapperFatPartition::allocateCluster(uint32_t previousCluster)
{
    uint32_t newCluster = allocateCluster();

    if (previousCluster)
    {
        if (_type == FAT16)
            setFAT16(previousCluster, newCluster);
        else
            setFAT32(previousCluster, newCluster);
    }

    return newCluster;
}

void DeviceWrapperFatPartition::setFAT16(uint16_t cluster, uint16_t value)
{
    /* Modify all FATs (usually 2) */
    for (auto fatStart : std::as_const(_fatStartOffset))
    {
        seek(fatStart + cluster * 2);
        write((char *) &value, 2);
    }
}

void DeviceWrapperFatPartition::setFAT32(uint32_t cluster, uint32_t value)
{
    uint32_t prev_value;

    /* Modify all FATs (usually 2) */
    for (auto fatStart : std::as_const(_fatStartOffset))
    {
        /* Spec (p. 16) mentions we must preserve high 4 bits of FAT32 FAT entry when modifiying */
        std::vector<uint8_t> readBytes(4);
        seek(fatStart + cluster * 4);
        read(readBytes, 4);
        uint32_t readValue = readBytes[0] << 24 | readBytes[1] << 16 | readBytes[2] << 8 | readBytes [3];
        auto reserved_bits = readValue & 0xF0000000;
        value |= reserved_bits;
        readBytes[0] = (readValue & 0xFF000000) >> 24;
        readBytes[1] = (readValue & 0x00FF0000) >> 16;
        readBytes[2] = (readValue & 0x0000FF00) >> 8;
        readBytes[3] = (readValue & 0x000000FF);

        seek(fatStart + cluster * 4);
        write(readBytes);
    }
}

void DeviceWrapperFatPartition::setFAT(uint32_t cluster, uint32_t value)
{
    if (_type == FAT16)
        setFAT16(cluster, value);
    else
        setFAT32(cluster, value);
}

uint32_t DeviceWrapperFatPartition::getFAT(uint32_t cluster)
{
    if (_type == FAT16)
    {
        uint16_t result;
        seek(_firstFatStartOffset + cluster * 2);
        read((char *) &result, 2);
        return result;
    }
    else
    {
        uint32_t result;
        seek(_firstFatStartOffset + cluster * 4);
        read((char *) &result, 4);
        return result & 0x0FFFFFFF;
    }
}

std::list<uint32_t> DeviceWrapperFatPartition::getClusterChain(uint32_t firstCluster)
{
    std::list<uint32_t> list;
    uint32_t cluster = firstCluster;

    while (true)
    {
        if ( (_type == FAT16 && cluster > 0xFFF7)
             || (_type == FAT32 && cluster > 0xFFFFFF7))
        {
            /* Reached EOF */
            break;
        }

        if (list.contains(cluster))
            throw std::runtime_error("Corrupt file system. Circular references in FAT table");

        list.push_back(cluster);
        cluster = getFAT(cluster);
    }

    return list;
}

void DeviceWrapperFatPartition::seekCluster(uint32_t cluster)
{
    seek(_clusterOffset + (cluster-2)*_bytesPerCluster);
}

bool DeviceWrapperFatPartition::fileExists(const std::string &filename)
{
    struct dir_entry entry;
    return getDirEntry(filename, &entry);
}

std::vector<uint8_t> DeviceWrapperFatPartition::readFile(const std::string &filename)
{
    struct dir_entry entry;

    if (!getDirEntry(filename, &entry))
        return {}; /* File not found */

    uint32_t firstCluster = entry.DIR_FstClusLO;
    if (_type == FAT32)
        firstCluster |= (entry.DIR_FstClusHI << 16);
    auto clusterList = getClusterChain(firstCluster);
    uint32_t len = entry.DIR_FileSize, pos = 0;
    std::vector<uint8_t> result(len);

    for (auto cluster : std::as_const(clusterList))
    {
        seekCluster(cluster);
        read(result.data()+pos, std::min(_bytesPerCluster, len-pos));

        pos += _bytesPerCluster;
        if (pos >= len)
            break;
    }

    return result;
}

void DeviceWrapperFatPartition::writeFile(const std::string &filename, const std::vector<uint8_t> &contents)
{
    std::list<uint32_t> clusterList;
    uint32_t pos = 0, firstCluster = 0;
    int clustersNeeded = (contents.size() + _bytesPerCluster - 1) / _bytesPerCluster;
    struct dir_entry entry;

    getDirEntry(filename, &entry, true);
    firstCluster = entry.DIR_FstClusLO;
    if (_type == FAT32)
        firstCluster |= (entry.DIR_FstClusHI << 16);

    if (firstCluster)
        clusterList = getClusterChain(firstCluster);

    if (clusterList.size() < clustersNeeded)
    {
        /* We need to allocate more clusters */
        uint32_t lastCluster = 0;
        int extraClustersNeeded = clustersNeeded - clusterList.size();

        if (!clusterList.empty())
            lastCluster = clusterList.back();

        for (int i = 0; i < extraClustersNeeded; i++)
        {
            lastCluster = allocateCluster(lastCluster);
            clusterList.push_back(lastCluster);
        }
    }
    else if (clusterList.size() > clustersNeeded)
    {
        /* We need to remove excess clusters */
        int clustersToRemove = clusterList.size() - clustersNeeded;
        uint32_t clusterToRemove = 0;
        std::vector<uint8_t> zeroes(_bytesPerCluster, 0);

        for (int i=0; i < clustersToRemove; i++)
        {
            clusterToRemove = clusterList.back();

            /* Zero out previous data in excess clusters,
               just in case someone wants to take a disk image later */
            seekCluster(clusterToRemove);
            write(zeroes.data(), zeroes.size());

            /* Mark cluster available again in FAT */
            setFAT(clusterToRemove, 0);
            clusterList.pop_back();
        }
        updateFSinfo(clustersToRemove, clusterToRemove);

        if (!clusterList.empty())
        {
            if (_type == FAT16)
                setFAT16(clusterList.back(), 0xFFFF);
            else
                setFAT32(clusterList.back(), 0xFFFFFFF);
        }
    }

    //std::cout << "First cluster:" << firstCluster << "Clusters:" << clusterList << std::endl;

    /* Write file data */
    for (uint32_t cluster : std::as_const(clusterList))
    {
        seekCluster(cluster);
        write(contents.data()+pos, std::min(_bytesPerCluster, (contents.size()-pos)));

        pos += _bytesPerCluster;
        if (pos >= contents.size())
            break;
    }

    if (clustersNeeded && contents.size() % _bytesPerCluster)
    {
        /* Zero out last cluster tip */
        uint32_t extraBytesAtEndOfCluster = _bytesPerCluster - (contents.size() % _bytesPerCluster);
        if (extraBytesAtEndOfCluster)
        {
            std::vector<uint8_t> zeroes(extraBytesAtEndOfCluster, 0);
            write(zeroes.data(), zeroes.size());
        }
    }

    /* Update directory entry */
    if (clusterList.empty())
        firstCluster = (_type == FAT16 ? 0xFFFF : 0xFFFFFFF);
    else
        firstCluster = clusterList.front();

    entry.DIR_FstClusLO = (firstCluster & 0xFFFF);
    entry.DIR_FstClusHI = (firstCluster >> 16);
    entry.DIR_WrtDate = DateToFATdate( QDate::currentDate() );
    entry.DIR_WrtTime = TimeToFATtime( QTime::currentTime() );
    entry.DIR_LstAccDate = entry.DIR_WrtDate;
    entry.DIR_FileSize = contents.size();
    updateDirEntry(&entry);
}

inline QByteArray _dirEntryToShortName(struct dir_entry *entry)
{
    QByteArray base = QByteArray((char *) entry->DIR_Name, 8).trimmed().toLower();
    QByteArray ext = QByteArray((char *) entry->DIR_Name+8, 3).trimmed().toLower();

    if (ext.isEmpty())
        return base;
    else
        return base+"."+ext;
}

bool DeviceWrapperFatPartition::getDirEntry(const std::string &longFilename, struct dir_entry *entry, bool createIfNotExist)
{
    std::string filenameRead, longFilenameLower = longFilename;

    std::transform(longFilenameLower.begin(), longFilenameLower.end(), longFilename.begin(),
        [](unsigned char c) {return std::tolower(c); });

    if (longFilename.empty())
        throw std::runtime_error("Filename cannot not be empty");

    openDir();
    while (readDir(entry))
    {
        if (entry->DIR_Attr & ATTR_LONG_NAME)
        {
            struct longfn_entry *l = (struct longfn_entry *) entry;
            /* A part can have 13 UTF-16 characters */
            char lnamePartStr[26] = {0};
             /* Using memcpy() because it has no problems accessing unaligned struct members */
            memcpy(lnamePartStr, l->LDIR_Name1, 10);
            memcpy(lnamePartStr+10, l->LDIR_Name2, 12);
            memcpy(lnamePartStr+22, l->LDIR_Name3, 4);
            std::string lnamePart( lnamePartStr, 13);
            filenameRead = lnamePart + filenameRead;
        }
        else
        {
            if (entry->DIR_Name[0] != 0xE5)
            {
                if (filenameRead.indexOf(QChar::Null))
                    filenameRead.truncate(filenameRead.indexOf(QChar::Null));

                //qDebug() << "Long filename:" << filenameRead << "DIR_Name:" << QByteArray((char *) entry->DIR_Name, sizeof(entry->DIR_Name)) << "Short:" << _dirEntryToShortName(entry);

                if (filenameRead.toLower() == longFilenameLower || (filenameRead.isEmpty() && _dirEntryToShortName(entry) == longFilenameLower))
                {
                    return true;
                }
            }

            filenameRead.clear();
        }
    }

    if (createIfNotExist)
    {
        std::vector<uint8_t> shortFilename;
        uint8_t shortFileNameChecksum = 0;
        struct longfn_entry longEntry;

        if (longFilename.count(".") == 1)
        {
            std::list<std::vector<uint8_t>> fnParts = longFilename.toLatin1().toUpper().split('.');
            shortFilename = fnParts[0].leftJustified(8, ' ', true)+fnParts[1].leftJustified(3, ' ', true);
        }
        else
        {
            shortFilename = longFilename.toLatin1().leftJustified(11, ' ', true);
        }

        /* Verify short file name has not been taken yet, and if not try inserting numbers into the name */
        if (dirNameExists(shortFilename))
        {
            for (int i=0; i<100; i++)
            {
                shortFilename = shortFilename.left( (i < 10 ? 7 : 6) )+QByteArray::number(i)+shortFilename.right(3);

                if (!dirNameExists(shortFilename))
                {
                    break;
                }
                else if (i == 99)
                {
                    throw std::runtime_error("Error finding available short filename");
                }
            }
        }

        for(int i = 0; i < shortFilename.length(); i++)
        {
            shortFileNameChecksum = ((shortFileNameChecksum & 1) ? 0x80 : 0) + (shortFileNameChecksum >> 1) + shortFilename[i];
        }

        std::vector<uint8_t> longFilenameWithNull;
        longFilenameWithNull.reserve(longFilename.size() + 1);
        char *longFilenameStr = (char *) longFilenameWithNull.data();
        int lfnFragments = (longFilenameWithNull.size()+12)/13;
        int lenBytes = longFilenameWithNull.size()*2;

        /* long file name directory entries are added in reverse order before the 8.3 entry */
        for (int i = lfnFragments; i > 0; i--)
        {
            memset(&longEntry, 0xff, sizeof(longEntry));
            longEntry.LDIR_Attr = ATTR_LONG_NAME;
            longEntry.LDIR_Chksum = shortFileNameChecksum;
            longEntry.LDIR_Ord = (i == lfnFragments) ? lfnFragments | LAST_LONG_ENTRY : lfnFragments;
            longEntry.LDIR_FstClusLO = 0;
            longEntry.LDIR_Type = 0;

            size_t start = (i-1) * 26;
            memcpy(longEntry.LDIR_Name1, longFilenameStr+start, std::min(lenBytes-start, sizeof(longEntry.LDIR_Name1)));
            start += sizeof(longEntry.LDIR_Name1);
            if (start < lenBytes)
            {
                memcpy(longEntry.LDIR_Name2, longFilenameStr+start, std::min(lenBytes-start, sizeof(longEntry.LDIR_Name2)));
                start += sizeof(longEntry.LDIR_Name2);
                if (start < lenBytes)
                {
                    memcpy(longEntry.LDIR_Name3, longFilenameStr+start, std::min(lenBytes-start, sizeof(longEntry.LDIR_Name3)));
                }
            }

            writeDirEntryAtCurrentPos((struct dir_entry *) &longEntry);
        }

        const std::chrono::zoned_time cur_time{ std::chrono::current_zone(),
                                            std::chrono::system_clock::now() };

        memset(entry, 0, sizeof(*entry));
        memcpy(entry->DIR_Name, shortFilename.data(), sizeof(entry->DIR_Name));
        entry->DIR_Attr = ATTR_ARCHIVE;
        entry->DIR_CrtDate = DateToFATdate(cur_time);
        entry->DIR_CrtTime = TimeToFATtime(cur_time);

        writeDirEntryAtCurrentPos(entry);

        /* Add an end-of-directory marker after our newly appended file */
        struct dir_entry endOfDir = {0};
        writeDirEntryAtCurrentPos(&endOfDir);
    }

    return false;
}

bool DeviceWrapperFatPartition::dirNameExists(const std::string &dirname)
{
    struct dir_entry entry;

    openDir();
    while (readDir(&entry))
    {
        if (!(entry.DIR_Attr & ATTR_LONG_NAME)
                && dirname == std::string((char *) entry.DIR_Name, sizeof(entry.DIR_Name)))
        {
            return true;
        }
    }

    return false;
}

void DeviceWrapperFatPartition::updateDirEntry(struct dir_entry *dirEntry)
{
    struct dir_entry iterEntry;

    openDir();
    quint64 oldOffset = _offset;

    while (readDir(&iterEntry))
    {
        /* Look for existing entry with same short filename */
        if (!(iterEntry.DIR_Attr & ATTR_LONG_NAME)
                && memcmp(dirEntry->DIR_Name, iterEntry.DIR_Name, sizeof(iterEntry.DIR_Name)) == 0)
        {
            /* seek() back and write out new entry */
            _offset = oldOffset;
            write((char *) dirEntry, sizeof(*dirEntry));
            return;
        }

        oldOffset = _offset;
    }

    throw std::runtime_error("Error locating existing directory entry");
}

void DeviceWrapperFatPartition::writeDirEntryAtCurrentPos(struct dir_entry *dirEntry)
{
    //qDebug() << "Write new entry" << QByteArray((char *) dirEntry->DIR_Name, 11);
    write((char *) dirEntry, sizeof(*dirEntry));

    if (_type == FAT32)
    {
        if ((pos()-_clusterOffset) % _bytesPerCluster == 0)
        {
            /* We reached the end of the cluster, allocate/seek to next cluster */
            uint32_t nextCluster = getFAT(_fat32_currentRootDirCluster);

            if (nextCluster > 0xFFFFFF7)
            {
                nextCluster = allocateCluster(_fat32_currentRootDirCluster);
            }

            if (_currentDirClusters.contains(nextCluster))
                throw std::runtime_error("Circular cluster references in FAT32 directory detected");
            _currentDirClusters.push_back(nextCluster);

            _fat32_currentRootDirCluster = nextCluster;
            seekCluster(_fat32_currentRootDirCluster);

            /* Zero out entire new cluster, as fsck.fat does not stop reading entries at end-of-directory marker */
            std::vector<uint8_t> zeroes(_bytesPerCluster, 0);
            write(zeroes.data(), zeroes.size() );
            seekCluster(_fat32_currentRootDirCluster);
        }
    }
    else if (pos() > (_fat16_firstRootDirSector+_fat16_rootDirSectors)*_bytesPerSector)
    {
        throw std::runtime_error("FAT16: ran out of root directory entry space");
    }
}

void DeviceWrapperFatPartition::openDir()
{
    /* Seek to start of root directory */
    if (_type == FAT16)
    {
        seek(_fat16_firstRootDirSector * _bytesPerSector);
    }
    else
    {
        _fat32_currentRootDirCluster = _fat32_firstRootDirCluster;
        seekCluster(_fat32_currentRootDirCluster);
        /* Keep track of directory clusters we seeked to, to be able
           to detect circular references */
        _currentDirClusters.clear();
        _currentDirClusters.push_back(_fat32_currentRootDirCluster);
    }
}

bool DeviceWrapperFatPartition::readDir(struct dir_entry *result)
{
    quint64 oldOffset = _offset;
    read((char *) result, sizeof(*result));

    if (result->DIR_Name[0] == 0)
    {
        /* seek() back to start of the entry marking end of directory */
        _offset = oldOffset;
        return false;
    }

    if (_type == FAT32)
    {
        if ((pos()-_clusterOffset) % _bytesPerCluster == 0)
        {
            /* We reached the end of the cluster, seek to next cluster */
            uint32_t nextCluster = getFAT(_fat32_currentRootDirCluster);

            if (nextCluster > 0xFFFFFF7)
            {
                qDebug() << "Reached end of FAT32 root directory, but no end-of-directory marker found. Adding one in new cluster.";
                nextCluster = allocateCluster(_fat32_currentRootDirCluster);
                seekCluster(nextCluster);
                QByteArray zeroes(_bytesPerCluster, 0);
                write(zeroes.data(), zeroes.length() );
            }

            if (_currentDirClusters.contains(nextCluster))
                throw std::runtime_error("Circular cluster references in FAT32 directory detected");
            _currentDirClusters.push_back(nextCluster);
            _fat32_currentRootDirCluster = nextCluster;
            seekCluster(_fat32_currentRootDirCluster);
        }
    }
    else if (pos() > (_fat16_firstRootDirSector+_fat16_rootDirSectors)*_bytesPerSector)
    {
        throw std::runtime_error("Reached end of FAT16 root directory section, but no end-of-directory marker found");
    }

    return true;
}

void DeviceWrapperFatPartition::updateFSinfo(int deltaClusters, uint32_t nextFreeClusterHint)
{
    struct FSInfo fsinfo;

    if (!_fat32_fsinfoSector)
        return;

    seek(_fat32_fsinfoSector * _bytesPerSector);
    read((char *) &fsinfo, sizeof(fsinfo));

    if (fsinfo.FSI_LeadSig[0] != 0x52 || fsinfo.FSI_LeadSig[1] != 0x52
            || fsinfo.FSI_LeadSig[2] != 0x61 || fsinfo.FSI_LeadSig[3] != 0x41
            || fsinfo.FSI_StrucSig[0] != 0x72 || fsinfo.FSI_StrucSig[1] != 0x72
            || fsinfo.FSI_StrucSig[2] != 0x41 || fsinfo.FSI_StrucSig[3] != 0x61
            || fsinfo.FSI_TrailSig[0] != 0x00 || fsinfo.FSI_TrailSig[1] != 0x00
            || fsinfo.FSI_TrailSig[2] != 0x55 || fsinfo.FSI_TrailSig[3] != 0xAA)
    {
        throw std::runtime_error("FAT32 FSinfo structure corrupt. Signature does not match.");
    }

    if (deltaClusters != 0 && fsinfo.FSI_Free_Count != 0xFFFFFFFF)
    {
        fsinfo.FSI_Free_Count += deltaClusters;
    }

    if (nextFreeClusterHint)
    {
        fsinfo.FSI_Nxt_Free = nextFreeClusterHint;
    }

    seek(_fat32_fsinfoSector * _bytesPerSector);
    write((char *) &fsinfo, sizeof(fsinfo));
}

uint16_t DeviceWrapperFatPartition::TimeToFATtime(const std::chrono::zoned_time &time)
{
    return (time.hour() << 11) | (time.minute() << 5) | (time.second() >> 1) ;
}

uint16_t DeviceWrapperFatPartition::DateToFATdate(const std::chrono::zoned_time &date)
{
    return ((date.year() - 1980) << 9) | (date.month() << 5) | date.day();
}
