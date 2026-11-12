#pragma once

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

// Write beside the destination so rename is atomic and readers never see a
// truncated file. mkstemp also keeps newly written moderation data private.
inline bool WriteFileAtomically(const std::string& path, const std::string& data,
                                std::string& error) {
    std::string pattern = path + ".tmp.XXXXXX";
    std::vector<char> temp(pattern.begin(), pattern.end());
    temp.push_back('\0');
    int fd = mkstemp(temp.data());
    if (fd < 0) { error = strerror(errno); return false; }
    bool ok = true;
    size_t offset = 0;
    while (offset < data.size()) {
        ssize_t n = write(fd, data.data() + offset, data.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { error = strerror(errno); ok = false; break; }
        offset += (size_t)n;
    }
    if (ok && fsync(fd) != 0) { error = strerror(errno); ok = false; }
    if (close(fd) != 0 && ok) { error = strerror(errno); ok = false; }
    if (ok && rename(temp.data(), path.c_str()) != 0) {
        error = strerror(errno);
        ok = false;
    }
    if (!ok) unlink(temp.data());
    return ok;
}
