#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Only the complete text record seen in the B07B firmware is accepted.
// No log line is retained or forwarded. The unit and the sensor are unknown.
struct GreenStats {
    uint64_t bytes = 0;
    uint64_t samples = 0;
    uint32_t faults = 0;
    int32_t raw = 0;
    int64_t last_ms = 0;

    const char* Status(int64_t now) const {
        if (samples) return now - last_ms > 30000 ? "stale" : "recent";
        return bytes ? "no_field" : "no_data";
    }
};

class GreenRx {
public:
    GreenStats stats;
    void Gap() {
        ++stats.faults;
        used_ = 0;
        skip_ = true;  // Cannot tell where the next complete line starts.
    }

    void Feed(const uint8_t* data, size_t n, int64_t now) {
        stats.bytes += n;
        for (size_t i = 0; i < n; ++i) {
            uint8_t c = data[i];
            if (c == '\r' || c == '\n') {
                if (!skip_ && used_) Finish(now);
                used_ = 0;
                skip_ = false;
            } else if (!skip_) {
                if (used_ == sizeof(line_) - 1 || (c < 32 && c != '\t') || c > 126) {
                    skip_ = true;
                } else {
                    line_[used_++] = static_cast<char>(c);
                }
            }
        }
    }

private:
    static constexpr char key_[] = "chip-temperature:";
    char line_[160]{};
    size_t used_ = 0;
    bool skip_ = false;
    static bool Space(char c) { return c == ' ' || c == '\t'; }

    void Finish(int64_t now) {
        line_[used_] = 0;
        const char* p = line_;
        while (Space(*p)) ++p;
        // Optional bracketed log prefixes; never match arbitrary substrings.
        while (*p == '[') {
            const char* end = std::strchr(p + 1, ']');
            if (!end || end == p + 1) return;
            p = end + 1;
            while (Space(*p)) ++p;
        }
        if (std::strncmp(p, key_, sizeof(key_) - 1)) return;
        p += sizeof(key_) - 1;
        while (Space(*p)) ++p;
        bool neg = *p == '-';
        if (*p == '+' || neg) ++p;
        if (*p < '0' || *p > '9') return;
        uint32_t value = 0;
        const uint32_t limit = neg ? 2147483648u : 2147483647u;
        do {
            unsigned digit = static_cast<unsigned>(*p++ - '0');
            if (value > (limit - digit) / 10u) return;
            value = value * 10u + digit;
        } while (*p >= '0' && *p <= '9');
        while (Space(*p)) ++p;
        if (*p) return;
        stats.raw = static_cast<int32_t>(neg ? -static_cast<int64_t>(value) : value);
        stats.last_ms = now;
        ++stats.samples;
    }
};
