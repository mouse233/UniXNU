// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace ram_system {
enum class Result { Success, BadArgument, WriteProtected, ShortRead };

// Direction is deliberately explicit: bidirectional descriptors are writes too.
// A rejected request neither touches memory nor invokes a completion callback.
template <class Buffer>
Result read(const uint8_t *memory, uint64_t bytes, uint64_t block, uint64_t count,
            bool inputOnly, Buffer &buffer, uint64_t &actual) {
    actual = 0;
    if (!memory || !bytes || (bytes & 511) || !count || block >= bytes / 512 ||
        count > bytes / 512 - block || buffer.length() < count * 512)
        return Result::BadArgument;
    if (!inputOnly) return Result::WriteProtected;
    const uint64_t requested = count * 512;
    actual = buffer.copy(memory + block * 512, requested);
    return actual == requested ? Result::Success : Result::ShortRead;
}

inline bool volumeName(const char *name) {
    if (!name) return false;
    for (unsigned i=0;i<4;i++) if (name[i]!="disk"[i]) return false;
    unsigned at=4;
    if (name[at]<'0' || name[at]>'9') return false;
    while (name[at]>='0' && name[at]<='9') {
        if (++at > 13) return false;
    }
    // These two immutable containers each have only nx_fs_oid[0].
    return name[at]=='s' && name[at+1]=='1' && name[at+2]==0;
}
inline bool uuid(const char *text, size_t length) {
    if (!text || length != 36) return false;
    for (unsigned i=0;i<36;i++) {
        const char c=text[i];
        if (i==8 || i==13 || i==18 || i==23) { if(c!='-') return false; }
        else if (!((c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F'))) return false;
    }
    return text[36]==0;
}
}
