#pragma once
// ===========================================================================
//  core/Format.h  —  human-readable byte sizes (seam)
// ===========================================================================
//  Extracted from main.cpp::humanSize. Returns std::string so it is testable on
//  the host; main.cpp wraps it back into an Arduino String for display. Uses
//  snprintf("%.1f") which matches the on-device String(float, 1) formatting.
//  Boundaries: 1024 -> "1.0 KB", 1048576 -> "1.0 MB".
// ===========================================================================
#include <stdint.h>
#include <stdio.h>
#include <string>

namespace core {

inline std::string humanSize(uint64_t bytes) {
    char buf[32];
    if (bytes < 1024ULL) {
        snprintf(buf, sizeof(buf), "%u B", (unsigned)bytes);
    } else if (bytes < 1024ULL * 1024) {
        snprintf(buf, sizeof(buf), "%.1f KB", (double)bytes / 1024.0);
    } else {
        snprintf(buf, sizeof(buf), "%.1f MB", (double)bytes / 1024.0 / 1024.0);
    }
    return std::string(buf);
}

} // namespace core
