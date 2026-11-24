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

/* Sprintf stream type */
typedef struct sprintf_stream {
    mr_sprintf_out_t out;
    void *ctx;
    int count;
} sprintf_stream_t;

/* Sprintf buffer type */
typedef struct sprintf_buf {
    char *buf;
    mr_size_t size;
    mr_size_t pos;
} sprintf_buf_t;

static void sprintf_emit(sprintf_stream_t *s, char c) {
    /* Emit character */
    if (s->out) {
        s->out(c, s->ctx);
    }

    /* Count formatted characters */
    s->count++;
}

static void sprintf_buf_out(char c, void *ctx) {
    sprintf_buf_t *b;

    /* Get buffer */
    b = (sprintf_buf_t *)ctx;

    /* Reserve space for the terminating null character */
    if (b->buf && (b->size > 1U) && (b->pos < (b->size - 1U))) {
        b->buf[b->pos] = c;
    }
    b->pos++;
}

static void sprintf_format_number(sprintf_stream_t *s, mr_uint32_t mod,
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
        sprintf_emit(s, sign);
        sign = 0;
    }
    if (!(mod & SPRINTF_MOD_LEFT)
        && ((fill == ' ') || !(mod & SPRINTF_MOD_SPECIAL))) {
        for (; width > 0; width--) {
            sprintf_emit(s, fill);
        }
    }

    /* Sign fill */
    if (sign) {
        sprintf_emit(s, sign);
    }

    /* Prefix fill */
    if (mod & SPRINTF_MOD_SPECIAL) {
        switch (base) {
            case 2: {
                sprintf_emit(s, '0');
                sprintf_emit(s, 'b');
                break;
            }
            case 8: {
                sprintf_emit(s, '0');
                break;
            }
            case 16: {
                sprintf_emit(s, '0');
                sprintf_emit(s, (mod & SPRINTF_MOD_LARGE) ? 'X' : 'x');
                break;
            }
            default: {
                break;
            }
        }

        /* Zero fill after prefix */
        if (!(mod & SPRINTF_MOD_LEFT) && (fill == '0')) {
            for (; width > 0; width--) {
                sprintf_emit(s, fill);
            }
        }
    }

    /* Precision fill */
    for (; i < prec; prec--) {
        sprintf_emit(s, '0');
    }

    /* Number fill */
    for (; i > 0; i--) {
        sprintf_emit(s, tmp[i - 1]);
    }

    /* Right fill */
    for (; width > 0; width--) {
        sprintf_emit(s, ' ');
    }
}

int mr_vcbprintf(mr_sprintf_out_t out, void *ctx, const char *fmt,
                 mr_va_list args) {
    int width, prec, len, i, base;
    sprintf_stream_t stream;
    mr_uint32_t mod;
    mr_uint64_t num;
    mr_int64_t snum;
    const char *s;
    char eff;

    /* Check parameter */
    if (!fmt) {
        return 0;
    }

    /* Init output stream */
    stream.out = out;
    stream.ctx = ctx;
    stream.count = 0;

    /* Format the input string */
    for (; *fmt; fmt++) {
        /* Common character */
        if (*fmt != '%') {
            sprintf_emit(&stream, *fmt);
            continue;
        }

        /* Literal percent */
        if (fmt[1] == '%') {
            sprintf_emit(&stream, '%');
            fmt++;
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
        if ((*fmt == 'l') || (*fmt == 'L') || (*fmt == 'z') || (*fmt == 'h')
            || (*fmt == 'j') || (*fmt == 't')) {
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
                        sprintf_emit(&stream, ' ');
                    }
                }

                /* Character */
                sprintf_emit(&stream, (char)mr_va_arg(args, int));

                /* Flush left */
                while ((width--) > 1) {
                    sprintf_emit(&stream, ' ');
                }
                continue;
            }
            /* String */
            case 's': {
                /* String */
                s = mr_va_arg(args, const char *);
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
                        sprintf_emit(&stream, ' ');
                    }
                }

                /* String */
                for (i = 0; i < len; i++) {
                    sprintf_emit(&stream, s[i]);
                }

                /* Flush left */
                while (len < (width--)) {
                    sprintf_emit(&stream, ' ');
                }
                continue;
            }
            /* Pointer */
            case 'p': {
                if (width == -1) {
                    width = (sizeof(void *) << 1) + 2;
                    mod |= (SPRINTF_MOD_SPECIAL | SPRINTF_MOD_ZEROPAD);
                }
                sprintf_format_number(&stream, mod, width, prec, 16,
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
                /* Consume the argument to keep subsequent formats aligned */
                if (eff == 'L') {
                    (void)mr_va_arg(args, long double);
                } else {
                    (void)mr_va_arg(args, mr_f64_t);
                }
                goto _default;
            }
            default: {
_default:
                /* Not supported symbol */
                sprintf_emit(&stream, '%');
                if (*fmt != '\0') {
                    sprintf_emit(&stream, *fmt);
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
                case 'L':
                case 'j': {
                    snum = (mr_int64_t)mr_va_arg(args, long long);
                    break;
                }
                case 'z': {
                    snum = (mr_int64_t)mr_va_arg(args, mr_ssize_t);
                    break;
                }
                case 't': {
                    snum = (mr_int64_t)mr_va_arg(args, mr_intptr_t);
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
                case 'L':
                case 'j': {
                    num = (mr_uint64_t)mr_va_arg(args, unsigned long long);
                    break;
                }
                case 'z': {
                    num = (mr_uint64_t)mr_va_arg(args, mr_size_t);
                    break;
                }
                case 't': {
                    num = (mr_uint64_t)mr_va_arg(args, mr_uintptr_t);
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
        sprintf_format_number(&stream, mod, width, prec, base, num);
    }
    return stream.count;
}

int mr_vsnprintf(char *buf, mr_size_t size, const char *fmt, mr_va_list args) {
    sprintf_buf_t buffer;
    int ret;

    /* Check parameter */
    if (!fmt) {
        return 0;
    }

    /* Init buffer */
    buffer.buf = buf;
    buffer.size = buf ? size : 0U;
    buffer.pos = 0U;

    /* Format the output */
    ret = mr_vcbprintf(sprintf_buf_out, &buffer, fmt, args);

    /* Null-terminate output buffer */
    if (buffer.buf && (buffer.size > 0U)) {
        if (buffer.pos < buffer.size) {
            buffer.buf[buffer.pos] = '\0';
        } else {
            buffer.buf[buffer.size - 1U] = '\0';
        }
    }
    return ret;
}

int mr_vsprintf(char *buf, const char *fmt, mr_va_list args) {
    /* Format string no limit */
    return mr_vsnprintf(buf, MR_SIZE_MAX, fmt, args);
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
