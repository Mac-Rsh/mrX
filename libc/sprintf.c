/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/libc/mr_sprintf.h>
#if defined(MR_USE_SPRINTF)

/* Modifier definition */
#define SPRINTF_MOD_ZEROPAD             (1U << 0)
#define SPRINTF_MOD_SIGN                (1U << 1)
#define SPRINTF_MOD_PLUS                (1U << 2)
#define SPRINTF_MOD_SPACE               (1U << 3)
#define SPRINTF_MOD_LEFT                (1U << 4)
#define SPRINTF_MOD_SPECIAL             (1U << 5)
#define SPRINTF_MOD_LARGE               (1U << 6)
#define SPRINTF_MOD_NEGATIVE            (1U << 7)

static char *sprintf_format_number(char *str, char *end, mr_uint32_t mod,
                                   int width, int prec, int base,
                                   mr_uint64_t num) {
    const char sdigits[] = "0123456789abcdef";
    const char ldigits[] = "0123456789ABCDEF";
    char fill, sign, tmp[64];
    const char *digits;
    int i;

    /* Sign */
    sign = 0;
    if (mod & SPRINTF_MOD_SIGN) {
        if (mod & SPRINTF_MOD_NEGATIVE) {
            sign = '-';
        }
        if (sign != '-') {
            if (mod & SPRINTF_MOD_PLUS) {
                sign = '+';
            } else if (mod & SPRINTF_MOD_SPACE) {
                sign = ' ';
            }
        }
        if (sign) {
            width--;
        }
    }

    /* Prefix */
    if (mod & SPRINTF_MOD_SPECIAL) {
        switch (base) {
            case 2:
            case 16: {
                width -= 2;
                break;
            }
            case 8: {
                break;
            }
            default: {
                break;
            }
        }
    }

    /* Number */
    digits = (mod & SPRINTF_MOD_LARGE) ? ldigits : sdigits;
    for (i = 0; (num != 0) || (i == 0); i++) {
        tmp[i] = digits[num % (mr_uint64_t)base];
        num /= (mr_uint64_t)base;
    }

    /* An octal prefix is needed only when precision does not supply it */
    if ((mod & SPRINTF_MOD_SPECIAL) && (base == 8)) {
        if (prec <= i) {
            width--;
        } else {
            mod &= ~SPRINTF_MOD_SPECIAL;
        }
    }
    if (i > prec) {
        prec = i;
    }
    width -= prec;

    /* Left fill */
    if (mod & SPRINTF_MOD_LEFT) {
        mod &= ~SPRINTF_MOD_ZEROPAD;
    }
    fill = (mod & SPRINTF_MOD_ZEROPAD) ? '0' : ' ';
    if ((fill == '0') && sign) {
        if (str < end) {
            *str = sign;
        }
        str++;
        sign = 0;
    }
    if (!(mod & SPRINTF_MOD_LEFT)
        && ((fill == ' ') || !(mod & SPRINTF_MOD_SPECIAL))) {
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
    if (mod & SPRINTF_MOD_SPECIAL) {
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
                    *str = (mod & SPRINTF_MOD_LARGE) ? 'X' : 'x';
                }
                str++;
                break;
            }
            default: {
                break;
            }
        }

        /* Zero fill after prefix */
        if (!(mod & SPRINTF_MOD_LEFT) && (fill == '0')) {
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
    mr_int64_t snum;
    mr_uint32_t mod;
    mr_uint64_t num;
    char eff, dummy;

    /* Check parameter */
    if (!fmt) {
        return 0;
    }
    if (!buf) {
        buf = &dummy;
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
                mod |= SPRINTF_MOD_LEFT;
            } else if (*fmt == '+') {
                mod |= SPRINTF_MOD_PLUS;
            } else if (*fmt == ' ') {
                mod |= SPRINTF_MOD_SPACE;
            } else if (*fmt == '0') {
                mod |= SPRINTF_MOD_ZEROPAD;
            } else if (*fmt == '#') {
                mod |= SPRINTF_MOD_SPECIAL;
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
                mod |= SPRINTF_MOD_LEFT;
            }
            fmt++;
        }

        /* Get precision */
        prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if ((*fmt >= '0') && (*fmt <= '9')) {
                for (; (*fmt >= '0') && (*fmt <= '9'); fmt++) {
                    prec = (prec * 10) + (*fmt - '0');
                }
            } else if (*fmt == '*') {
                prec = mr_va_arg(args, int);
                if (prec < 0) {
                    prec = -1;
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
                eff = 'H';
                fmt++;
            }
        }

        /* Handle format */
        switch (*fmt) {
            /* Character */
            case 'c': {
                if (width < 1) {
                    width = 1;
                }

                /* Flush right */
                if (!(mod & SPRINTF_MOD_LEFT)) {
                    while ((width--) > 1) {
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
                while ((width--) > 1) {
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
                if (!(mod & SPRINTF_MOD_LEFT)) {
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
                    mod |= (SPRINTF_MOD_SPECIAL | SPRINTF_MOD_ZEROPAD);
                }
                str = sprintf_format_number(
                    str, end, mod, width, prec, 16,
                    (mr_uintptr_t)mr_va_arg(args, void *));
                continue;
            }
            /* Decimal */
            case 'd':
            case 'i': {
                mod |= SPRINTF_MOD_SIGN;
                base = 10;
                break;
            }
            case 'u': {
                base = 10;
                break;
            }
            /* Hexadecimal */
            case 'X': {
                mod |= SPRINTF_MOD_LARGE;
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

        /* Get number argument */
        if (mod & SPRINTF_MOD_SIGN) {
            switch (eff) {
                case 'l': {
                    snum = (mr_int64_t)mr_va_arg(args, long);
                    break;
                }
                case 'L': {
                    snum = (mr_int64_t)mr_va_arg(args, long long);
                    break;
                }
                case 'z': {
                    snum = (mr_int64_t)mr_va_arg(args, mr_ssize_t);
                    break;
                }
                case 'h': {
                    snum = (mr_int16_t)mr_va_arg(args, int);
                    break;
                }
                case 'H': {
                    snum = (mr_int8_t)mr_va_arg(args, int);
                    break;
                }
                default: {
                    snum = (mr_int64_t)mr_va_arg(args, int);
                    break;
                }
            }

            /* Convert signed magnitude without signed overflow */
            if (snum < 0) {
                mod |= SPRINTF_MOD_NEGATIVE;
                num = 0ULL - (mr_uint64_t)snum;
            } else {
                num = (mr_uint64_t)snum;
            }
        } else {
            switch (eff) {
                case 'l': {
                    num = (mr_uint64_t)mr_va_arg(args, unsigned long);
                    break;
                }
                case 'L': {
                    num = (mr_uint64_t)mr_va_arg(args, unsigned long long);
                    break;
                }
                case 'z': {
                    num = (mr_uint64_t)mr_va_arg(args, mr_size_t);
                    break;
                }
                case 'h': {
                    num = (mr_uint16_t)mr_va_arg(args, unsigned int);
                    break;
                }
                case 'H': {
                    num = (mr_uint8_t)mr_va_arg(args, unsigned int);
                    break;
                }
                default: {
                    num = (mr_uint64_t)mr_va_arg(args, unsigned int);
                    break;
                }
            }
        }
        if (num == 0) {
            mod &= ~SPRINTF_MOD_SPECIAL;
        }
        str = sprintf_format_number(str, end, mod, width, prec, base, num);
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
    return mr_vsnprintf(buf, MR_SIZE_MAX - (mr_size_t)buf, fmt, args);
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
