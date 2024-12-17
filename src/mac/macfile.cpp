/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <iostream>
#include <string>

#include "macfile.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <security/Authorization.h>

MacFile::MacFile() { }

MacFile::~MacFile() {
    if (fileDescriptor) {
        close(fileDescriptor);
    }
}

/* Prevent that Qt thinks /dev/rdisk does not permit seeks because it does not report size */
bool MacFile::isSequential() const
{
    return false;
}

MacFile::AuthOpenResult MacFile::authOpen(const std::string &filename)
{
    int fd = -1;

    std::string right = "sys.openfile.readwrite."+filename;
    AuthorizationItem item = {right.c_str(), 0, nullptr, 0};
    AuthorizationRights rights = {1, &item};
    AuthorizationFlags flags = kAuthorizationFlagInteractionAllowed |
            kAuthorizationFlagExtendRights |
            kAuthorizationFlagPreAuthorize;
    AuthorizationRef authRef;
    if (AuthorizationCreate(&rights, nullptr, flags, &authRef) != 0)
        return AuthOpenResult::AuthOpenCancelled;

    AuthorizationExternalForm externalForm;
    if (AuthorizationMakeExternalForm(authRef, &externalForm) != 0)
    {
        AuthorizationFree(authRef, 0);
        return AuthOpenResult::AuthOpenError;
    }

    const char *cmd = "/usr/libexec/authopen";
    int pipe[2];
    int stdinpipe[2];
    ::socketpair(AF_UNIX, SOCK_STREAM, 0, pipe);
    ::pipe(stdinpipe);
    pid_t pid = ::fork();
    if (pid == 0)
    {
        // child
        ::close(pipe[0]);
        ::close(stdinpipe[1]);
        ::dup2(pipe[1], STDOUT_FILENO);
        ::dup2(stdinpipe[0], STDIN_FILENO);
        ::execl(cmd, cmd, "-stdoutpipe", "-extauth", "-o", O_RDWR, filename.c_str(), NULL);
        ::exit(-1);
    }
    else
    {
        ::close(pipe[1]);
        ::close(stdinpipe[0]);
        ::write(stdinpipe[1], externalForm.bytes, sizeof(externalForm.bytes));
        ::close(stdinpipe[1]);

        const size_t bufSize = CMSG_SPACE(sizeof(int));
        char buf[bufSize];
        struct iovec io_vec[1];
        io_vec[0].iov_base = buf;
        io_vec[0].iov_len = bufSize;
        const size_t cmsgSize = CMSG_SPACE(sizeof(int));
        char cmsg[cmsgSize];

        struct msghdr msg = {};
        msg.msg_iov = io_vec;
        msg.msg_iovlen = 1;
        msg.msg_control = cmsg;
        msg.msg_controllen = cmsgSize;

        ssize_t size;
        do {
            size = recvmsg(pipe[0], &msg, 0);
        } while (size == -1 && errno == EINTR);

        std::cout << "RECEIVED SIZE:" << size;

        if (size > 0) {
            struct cmsghdr *chdr = CMSG_FIRSTHDR(&msg);
            if (chdr && chdr->cmsg_type == SCM_RIGHTS) {
                std::cout << "SCMRIGHTS";
                fd = *( (int*) (CMSG_DATA(chdr)) );
            }
            else
            {
                std::cout << "NOT SCMRIGHTS";
            }
        }

        pid_t wpid;
        int status;

        do {
            wpid = ::waitpid(pid, &status, 0);
        } while (wpid == -1 && errno == EINTR);

        if (wpid == -1)
        {
            std::cout << "waitpid() failed executing authopen";
            return AuthOpenResult::AuthOpenError;
        }
        if (WEXITSTATUS(status))
        {
            std::cout << "authopen returned failure code" << WEXITSTATUS(status);
            return AuthOpenResult::AuthOpenError;
        }

        std::cout << "fd received:" << fd;
    }
    AuthorizationFree(authRef, 0);

    fd = open(filename.c_str(), O_RDWR | O_NOCTTY);

    return fd ? AuthOpenResult::AuthOpenSuccess : AuthOpenResult::AuthOpenError;
}
