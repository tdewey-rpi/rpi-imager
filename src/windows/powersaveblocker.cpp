/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "powersaveblocker.h"
#include <string>

#include <windows.h>

class PowerSaveBlocker::impl
{
public:
    impl()
        : _stayingAwake(false)
    {
        _powerRequest = INVALID_HANDLE_VALUE
    };

    ~impl() {
        removeBlock();
    }
    void applyBlock(const std::string &reason) {
        if (_stayingAwake)
            return;

        REASON_CONTEXT rc;
        std::wstring wreason = reason;
        rc.Version = POWER_REQUEST_CONTEXT_VERSION;
        rc.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
        rc.Reason.SimpleReasonString = (wchar_t *) wreason.c_str();
        _powerRequest = PowerCreateRequest(&rc);

        if (_powerRequest == INVALID_HANDLE_VALUE)
        {
            std::cerr << "Error creating power request:" << GetLastError();
            return;
        }

        _stayingAwake = PowerSetRequest(_powerRequest, PowerRequestDisplayRequired);
        if (!_stayingAwake)
        {
            std::cerr << "Error running PowerSetRequest():" << GetLastError();
        }
    }
    void removeBlock() {
        if (!_stayingAwake)
            return;

        _stayingAwake = PowerClearRequest(_powerRequest, PowerRequestDisplayRequired);
        CloseHandle(_powerRequest);
    }

protected:
    bool _stayingAwake;
    HANDLE _powerRequest;
};

PowerSaveBlocker::PowerSaveBlocker() {
    p_Impl = std::make_unique<PowerSaveBlocker::impl>();
}

PowerSaveBlocker::~PowerSaveBlocker() {}

void PowerSaveBlocker::applyBlock(const std::string &reason)
{
    p_Impl->applyBlock(reason);
}

void PowerSaveBlocker::removeBlock() {
    p_Impl->removeBlock();
}

