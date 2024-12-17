#ifndef WLANCREDENTIALS_H
#define WLANCREDENTIALS_H

/*
 * Interface for wlan credential detection
 * Use WlanCredentials::instance() to get platform
 * specific implementation
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2023 Raspberry Pi Ltd
 */

#include <vector>
#include <cstdint>

class WlanCredentials
{
public:
    static WlanCredentials *instance();
    virtual std::vector<uint8_t> getSSID() = 0;
    virtual std::vector<uint8_t> getPSK() = 0;

protected:
    static WlanCredentials *_instance;
};

#endif // WLANCREDENTIALS_H
