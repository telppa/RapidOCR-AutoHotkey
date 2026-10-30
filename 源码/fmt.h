// 极简数字格式化 / 解析
// ---------------------------------------------------------------
// 目的：整个 DLL 不引用 printf/strtod 家族。静态 libc 里 pformat 一坨
// （__mingw_pformat + __gdtoa + __strtodg + __gethex_D2A + pow）约 19 KB，
// 只要有一处 sprintf 就全被拉进来；换成这几十行，DLL 直接瘦一圈。
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>

namespace fmt {

// 无符号整数转十进制，返回写到的位置
inline char* uint_dec(char* p, unsigned long long v) {
    char tmp[24];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    } while (v);
    while (n) *p++ = tmp[--n];
    return p;
}

// 有符号整数
inline char* int_dec(char* p, long long v) {
    if (v < 0) {
        *p++ = '-';
        return uint_dec(p, (unsigned long long)(-(v + 1)) + 1ULL);
    }
    return uint_dec(p, (unsigned long long)v);
}

// 固定小数的十六进制（\uXXXX 用），宽度固定 4
inline char* hex4(char* p, unsigned v) {
    for (int i = 3; i >= 0; --i) {
        unsigned d = (v >> (i * 4)) & 0xF;
        *p++ = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
    }
    return p;
}

// 解析 4 位十六进制（\uXXXX），遇到非 hex 字符即停
inline unsigned parse_hex4(const char* s) {
    unsigned v = 0;
    for (int i = 0; i < 4; ++i) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = v * 16 + (unsigned)d;
    }
    return v;
}

// 定点小数：等价 sprintf("%.*f", dec, v)，dec <= 6，用于 2 位/5 位
inline char* fixed(char* p, double v, int dec) {
    static const long long kPow10[7] = {1, 10, 100, 1000, 10000, 100000, 1000000};
    if (dec < 0) dec = 0;
    if (dec > 6) dec = 6;
    const long long m = kPow10[dec];
    if (v < 0) {
        *p++ = '-';
        v = -v;
    }
    // long double（x87 80 位）算乘法，误差比 double 小一个量级；
    // 舍入用 nearbyintl（就近取偶），跟 libc 的 printf 一致
    long long scaled = (long long)nearbyintl((long double)v * (long double)m);
    p = int_dec(p, scaled / m);
    if (dec) {
        *p++ = '.';
        long long frac = scaled % m;
        for (int i = dec - 1; i >= 0; --i) {
            p[i] = (char)('0' + (int)(frac % 10));
            frac /= 10;
        }
        p += dec;
    }
    return p;
}

// 解析十进制数（支持 +- 号、小数、e 指数）。失败时返回 0 且 *end == s
inline double parse(const char* s, const char** end) {
    const char* p = s;
    while (*p == ' ' || *p == '\t') ++p;
    int sign = 1;
    if (*p == '-') { sign = -1; ++p; }
    else if (*p == '+') ++p;

    double v = 0;
    bool any = false;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        ++p;
        any = true;
    }
    if (*p == '.') {
        ++p;
        double f = 0.1;
        while (*p >= '0' && *p <= '9') {
            v += (*p - '0') * f;
            f *= 0.1;
            ++p;
            any = true;
        }
    }
    if (any && (*p == 'e' || *p == 'E')) {
        const char* q = p + 1;
        int es = 1;
        if (*q == '-') { es = -1; ++q; }
        else if (*q == '+') ++q;
        int e = 0;
        bool eany = false;
        while (*q >= '0' && *q <= '9') {
            if (e < 100000) e = e * 10 + (*q - '0');
            ++q;
            eany = true;
        }
        if (eany) {
            while (e-- > 0) v = (es > 0) ? v * 10.0 : v / 10.0;
            p = q;
        }
    }
    if (end) *end = any ? p : s;
    return any ? v * sign : 0.0;
}

}  // namespace fmt
