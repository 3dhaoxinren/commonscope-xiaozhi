#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
static inline unsigned pcrc(const char* s, unsigned n) {
    unsigned c = 65535;
    while (n--) {
        c ^= (unsigned char)*s++ << 8;
        for (unsigned i = 0; i < 8; i++)
            c = ((c << 1) ^ ((c & 32768) ? 0x1021 : 0)) & 65535;
    }
    return c;
}
static inline void phx(char* s, uint32_t v, unsigned n) {
    const char h[] = "0123456789ABCDEF";
    while (n) {
        s[--n] = h[v & 15];
        v >>= 4;
    }
}
static inline int phread(const char* s, unsigned n, uint32_t* v) {
    uint32_t x = 0;
    while (n--) {
        unsigned a = (unsigned char)*s++;
        if (a >= '0' && a <= '9')
            a -= '0';
        else if (a >= 'A' && a <= 'F')
            a = a - 'A' + 10;
        else
            return 0;
        x = (x << 4) | a;
    }
    *v = x;
    return 1;
}
static inline void pseal(char* s, unsigned n) {
    phx(s + n, pcrc(s, n), 4);
    s[n + 4] = 0;
}
static inline int pcheck(const char* s, unsigned n, const char* tag, unsigned k) {
    uint32_t v;
    if (n != k + 4 || memcmp(s, tag, 4) || !phread(s + k, 4, &v) || v != pcrc(s, k))
        return 0;
    for (unsigned i = 4; i < k; i++)
        if (!phread(s + i, 1, &v))
            return 0;
    return 1;
}
static inline int pweather(int t, int lo, int hi, unsigned rh, unsigned code, uint32_t obs) {
    return obs >= 1704067200u && obs < 4102444800u && t >= -900 && t <= 650 && lo >= -900 &&
           lo <= 650 && hi >= lo && hi <= 650 && rh <= 100 && code <= 99;
}
static inline int psigned(uint32_t v) { return v < 32768 ? (int)v : (int)v - 65536; }
