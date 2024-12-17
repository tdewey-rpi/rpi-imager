#ifndef MACFILE_H
#define MACFILE_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

#include <string>

struct MacFile
{
    enum class AuthOpenResult { AuthOpenCancelled, AuthOpenSuccess, AuthOpenError };

    MacFile();
    ~MacFile();
    virtual bool isSequential() const;
    AuthOpenResult authOpen(const std::string &filename);

private:
    int fileDescriptor;
};

#endif // MACFILE_H
