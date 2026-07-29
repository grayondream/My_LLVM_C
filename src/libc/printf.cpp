#include "libc.h"
#include <cstdio>

namespace mylibc {

static int int_to_string(char* buf, int value) {
    char tmp[12];
    int len = 0;
    bool neg = false;

    if (value < 0) {
        neg = true;
        value = -value;
    }

    if (value == 0) {
        tmp[len++] = '0';
    } else {
        while (value > 0) {
            tmp[len++] = '0' + (value % 10);
            value /= 10;
        }
    }

    int pos = 0;
    if (neg) buf[pos++] = '-';
    for (int i = len - 1; i >= 0; --i)
        buf[pos++] = tmp[i];
    buf[pos] = '\0';
    return pos;
}

static int format_output(char* dest, const char* format, va_list args) {
    int written = 0;
    const char* fmt = format;

    while (*fmt) {
        if (*fmt == '%' && *(fmt + 1)) {
            ++fmt;
            switch (*fmt) {
            case 'd':
            case 'i': {
                int val = va_arg(args, int);
                char numbuf[12];
                int_to_string(numbuf, val);
                for (const char* p = numbuf; *p; ++p) {
                    if (dest) dest[written] = *p;
                    ++written;
                }
                break;
            }
            case 'u': {
                unsigned int val = va_arg(args, unsigned int);
                char numbuf[12];
                char tmp[12];
                int len = 0;
                if (val == 0) {
                    tmp[len++] = '0';
                } else {
                    while (val > 0) {
                        tmp[len++] = '0' + (val % 10);
                        val /= 10;
                    }
                }
                for (int i = 0; i < len; ++i)
                    numbuf[i] = tmp[len - 1 - i];
                numbuf[len] = '\0';
                for (const char* p = numbuf; *p; ++p) {
                    if (dest) dest[written] = *p;
                    ++written;
                }
                break;
            }
            case 's': {
                const char* str = va_arg(args, const char*);
                if (!str) str = "(null)";
                for (const char* p = str; *p; ++p) {
                    if (dest) dest[written] = *p;
                    ++written;
                }
                break;
            }
            case 'c': {
                char c = static_cast<char>(va_arg(args, int));
                if (dest) dest[written] = c;
                ++written;
                break;
            }
            case '%': {
                if (dest) dest[written] = '%';
                ++written;
                break;
            }
            default: {
                if (dest) dest[written] = '%';
                ++written;
                if (dest) dest[written] = *fmt;
                ++written;
                break;
            }
            }
        } else {
            if (dest) dest[written] = *fmt;
            ++written;
        }
        ++fmt;
    }

    if (dest) dest[written] = '\0';
    return written;
}

int my_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int len = format_output(nullptr, format, args);
    va_end(args);

    // Use actual stdout write
    va_start(args, format);
    char* buf = new char[len + 1];
    format_output(buf, format, args);
    va_end(args);

    int ret = std::fputs(buf, stdout);
    delete[] buf;
    return (ret >= 0) ? len : -1;
}

int my_sprintf(char* str, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int len = format_output(str, format, args);
    va_end(args);
    return len;
}

} // namespace mylibc
