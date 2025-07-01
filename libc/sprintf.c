/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <libc/mr_sprintf.h>
#if defined(MR_USE_SPRINTF)

/* Modifier definition */
#define MOD_ZEROPAD                     (1U << 0)
#define MOD_SIGN                        (1U << 1)
#define MOD_PLUS                        (1U << 2)
#define MOD_SPACE                       (1U << 3)
#define MOD_LEFT                        (1U << 4)
#define MOD_SPECIAL                     (1U << 5)
#define MOD_LARGE                       (1U << 6)

static char *format_number(char *str, char *end, mr_uint32_t mod, int width,
                           int prec, char eff, int base, mr_uint64_t num) {
    static const char sdigits[] = "0123456789abcdef";
    static const char ldigits[] = "0123456789ABCDEF";
    char fill, sign, tmp[64];
    const char *digits;
    int i;

    /* Sign */
    sign = 0;
    if (mod & MOD_SIGN) {
        switch (eff) {
            case 'l': {
                if ((mr_int32_t)num < 0) {
                    sign = '-';
                    num = (mr_uint32_t)-num;
                }
                break;
            }
            case 'L': {
                if ((mr_int64_t)num < 0) {
                    sign = '-';
                    num = -num;
                }
                break;
            }
            case 'h': {
                if ((mr_int16_t)num < 0) {
                    sign = '-';
                    num = (mr_uint16_t)-num;
                }
                break;
            }
            case 'H': {
                if ((mr_int8_t)num < 0) {
                    sign = '-';
                    num = (mr_uint8_t)-num;
                }
                break;
            }
            default: {
                if ((mr_int32_t)num < 0) {
                    sign = '-';
                    num = (mr_uint32_t)-num;
                }
                break;
            }
        }
        if (sign != '-') {
            if (mod & MOD_PLUS) {
                sign = '+';
            } else if (mod & MOD_SPACE) {
                sign = ' ';
            }
        }
        if (sign) {
            width--;
        }
    }

    /* Prefix */
    if (mod & MOD_SPECIAL) {
        switch (base) {
            case 2:
            case 16: {
                width -= 2;
                break;
            }
            case 8: {
                width--;
                break;
            }
            default: {
                break;
            }
        }
    }

    /* Number */
    digits = (mod & MOD_LARGE) ? ldigits : sdigits;
    for (i = 0; (num != 0) || (i == 0); i++) {
        tmp[i] = digits[num % (mr_uint64_t)base];
        num /= (mr_uint64_t)base;
    }
    if (i > prec) {
        prec = i;
    }
    width -= prec;

    /* Left fill */
    if (mod & MOD_LEFT) {
        mod &= ~MOD_ZEROPAD;
    }
    fill = (mod & MOD_ZEROPAD) ? '0' : ' ';
    if (!(mod & (MOD_LEFT | MOD_SPECIAL))) {
        for (; width > 0; width--) {
            if (str < end) {
                *str = fill;
            }
            str++;
        }
    }

    /* Sign fill */
    if (sign) {
        if (str < end) {
            *str = sign;
        }
        str++;
    }

    /* Prefix fill */
    if (mod & MOD_SPECIAL) {
        switch (base) {
            case 2: {
                if (str < end) {
                    *str = '0';
                }
                str++;
                if (str < end) {
                    *str = 'b';
                }
                str++;
                break;
            }
            case 8: {
                if (str < end) {
                    *str = '0';
                }
                str++;
                break;
            }
            case 16: {
                if (str < end) {
                    *str = '0';
                }
                str++;
                if (str < end) {
                    *str = (mod & MOD_LARGE) ? 'X' : 'x';
                }
                str++;
                break;
            }
            default: {
                break;
            }
        }

        /* Left fill */
        if (!(mod & MOD_LEFT)) {
            for (; width > 0; width--) {
                if (str < end) {
                    *str = fill;
                }
                str++;
            }
        }
    }

    /* Precision fill */
    for (; i < prec; prec--) {
        if (str < end) {
            *str = '0';
        }
        str++;
    }

    /* Number fill */
    for (; i > 0; i--) {
        if (str < end) {
            *str = tmp[i - 1];
        }
        str++;
    }

    /* Right fill */
    for (; width > 0; width--) {
        if (str < end) {
            *str = ' ';
        }
        str++;
    }
    return str;
}

int mr_vsnprintf(char *buf, mr_size_t size, const char *fmt, mr_va_list args) {
    int width, prec, len, i, base;
    char *str, *end, *s;
    mr_uint32_t mod;
    mr_uint64_t num;
    char eff;

    /* Check parameter */
    if ((!fmt) || (fmt[0] == '\0')) {
        return 0;
    }
    if (!buf) {
        size = 0;
    }

    for (str = buf, end = (str + size); *fmt; fmt++) {
        /* Common character */
        if (*fmt != '%') {
            if (str < end) {
                *str = *fmt;
            }
            str++;
            continue;
        }

        /* Get format modifier */
        for (fmt++, mod = 0; *fmt; fmt++) {
            if (*fmt == '-') {
                mod |= MOD_LEFT;
            } else if (*fmt == '+') {
                mod |= MOD_PLUS;
            } else if (*fmt == ' ') {
                mod |= MOD_SPACE;
            } else if (*fmt == '0') {
                mod |= MOD_ZEROPAD;
            } else if (*fmt == '#') {
                mod |= MOD_SPECIAL;
            } else {
                break;
            }
        }

        /* Get width */
        width = -1;
        if ((*fmt >= '0') && (*fmt <= '9')) {
            for (width = 0; (*fmt >= '0') && (*fmt <= '9'); fmt++) {
                width = (width * 10) + (*fmt - '0');
            }
        } else if (*fmt == '*') {
            width = mr_va_arg(args, int);
            if (width < 0) {
                width = -width;
                mod |= MOD_LEFT;
            }
            fmt++;
        }

        /* Get precision */
        prec = -1;
        if (*fmt == '.') {
            fmt++;
            if ((*fmt >= '0') && (*fmt <= '9')) {
                for (prec = 0; (*fmt >= '0') && (*fmt <= '9'); fmt++) {
                    prec = (prec * 10) + (*fmt - '0');
                }
            } else if (*fmt == '*') {
                prec = mr_va_arg(args, int);
                if (prec < 0) {
                    prec = 0;
                }
                fmt++;
            }
        }

        /* Get effector */
        eff = '\0';
        if ((*fmt == 'l') || (*fmt == 'L') || (*fmt == 'z') || (*fmt == 'h')) {
            eff = *(fmt++);
            if ((*fmt == 'l') && (eff == 'l')) {
                eff = 'L';
                fmt++;
            } else if ((*fmt == 'h') && (eff == 'h')) {
                fmt++;
            }
        }

        /* Handle format */
        switch (*fmt) {
            /* Character */
            case 'c': {
                /* Flush right */
                if (!(mod & MOD_LEFT)) {
                    while ((width--) > 0) {
                        if (str < end) {
                            *str = ' ';
                        }
                        str++;
                    }
                }

                /* Character */
                if (str < end) {
                    *str = (char)mr_va_arg(args, int);
                }
                str++;

                /* Flush left */
                while ((width--) > 0) {
                    if (str < end) {
                        *str = ' ';
                    }
                    str++;
                }
                continue;
            }
            /* String */
            case 's': {
                /* String */
                s = mr_va_arg(args, char *);
                if (!s) {
                    s = "(null)";
                }

                /* Get string length */
                for (len = 0; len != prec; len++) {
                    if (s[len] != '\0') {
                        continue;
                    }
                    break;
                }

                /* Flush right */
                if (!(mod & MOD_LEFT)) {
                    while (len < (width--)) {
                        if (str < end) {
                            *str = ' ';
                        }
                        str++;
                    }
                }

                /* String */
                for (i = 0; i < len; i++) {
                    if (str < end) {
                        *str = s[i];
                    }
                    str++;
                }

                /* Flush left */
                while (len < (width--)) {
                    if (str < end) {
                        *str = ' ';
                    }
                    str++;
                }
                continue;
            }
            /* Pointer */
            case 'p': {
                if (width == -1) {
                    width = (sizeof(void *) << 1) + 2;
                    mod |= (MOD_SPECIAL | MOD_ZEROPAD);
                }
                str = format_number(str, end, mod, width, prec, eff, 16,
                                    (mr_uintptr_t)mr_va_arg(args, void *));
                continue;
            }
            /* Decimal */
            case 'd':
            case 'i': {
                mod |= MOD_SIGN;
                base = 10;
                break;
            }
            case 'u': {
                base = 10;
                break;
            }
            /* Hexadecimal */
            case 'X': {
                mod |= MOD_LARGE;
                base = 16;
                break;
            }
            case 'x': {
                base = 16;
                break;
            }
            /* Binary */
            case 'b': {
                base = 2;
                break;
            }
            /* Octal */
            case 'o': {
                base = 8;
                break;
            }
            /* Not supported */
            case 'g':
            case 'e':
            case 'f':
            case 'G':
            case 'E':
            case 'F': {
                num = (mr_uint64_t)mr_va_arg(args, mr_f64_t);
                (void)num;
                goto _default;
            }
            default: {
_default:
                /* Not supported symbol */
                if (str < end) {
                    *str = '%';
                }
                str++;
                if (*fmt != '\0') {
                    if (str < end) {
                        *str = *fmt;
                    }
                    str++;
                } else {
                    fmt--;
                }
                continue;
            }
        }

        /* Handle number format */
        switch (eff) {
            case 'l': {
                num = mr_va_arg(args, mr_uint32_t);
                break;
            }
            case 'L': {
                num = mr_va_arg(args, mr_uint64_t);
                break;
            }
            case 'z': {
                num = mr_va_arg(args, mr_size_t);
                if (mod & MOD_SIGN) {
                    num = (mr_uint64_t)((mr_ssize_t)num);
                }
                break;
            }
            case 'h':
            case 'H': {
                num = (mr_uint16_t)mr_va_arg(args, mr_int32_t);
                if (mod & MOD_SIGN) {
                    num = (mr_uint64_t)((mr_int16_t)num);
                }
                break;
            }
            default: {
                num = mr_va_arg(args, mr_uint32_t);
                break;
            }
        }
        str = format_number(str, end, mod, width, prec, eff, base, num);
    }

    /* Handle end */
    if (size > 0) {
        if (str < end) {
            *str = '\0';
        } else {
            end[-1] = '\0';
        }
    }
    return (int)(str - buf);
}

int mr_vsprintf(char *buf, const char *fmt, mr_va_list args) {
    /* Format string no limit */
    return mr_vsnprintf(buf, (mr_size_t)-1, fmt, args);
}

int mr_snprintf(char *buf, mr_size_t size, const char *fmt, ...) {
    mr_va_list args;
    int ret;

    /* Format string with limit */
    mr_va_start(args, fmt);
    ret = mr_vsnprintf(buf, size, fmt, args);
    mr_va_end(args);
    return ret;
}

int mr_sprintf(char *buf, const char *fmt, ...) {
    mr_va_list args;
    int ret;

    /* Format string no limit */
    mr_va_start(args, fmt);
    ret = mr_vsprintf(buf, fmt, args);
    mr_va_end(args);
    return ret;
}
#endif /* defined(MR_USE_SPRINTF) */
