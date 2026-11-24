/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/kernel/mr_printf.h>
#if defined(MR_USE_PRINTF)
#include <mrX/port/mr_port_output.h>
#include <mrX/kernel/mr_service.h>
#include <mrX/libc/mr_sprintf.h>
#if !defined(MR_USE_SPRINTF) && defined(MR_USE_PRINTF_SAFE)
#include <mrX/libc/mr_malloc.h>
#endif /* !defined(MR_USE_SPRINTF) && defined(MR_USE_PRINTF_SAFE) */

/* Printf buffer size configuration */
#if !defined(MR_CFG_PRINTF_BUF_SIZE)
#define MR_CFG_PRINTF_BUF_SIZE          (64)
#endif /* !defined(MR_CFG_PRINTF_BUF_SIZE) */

#if defined(MR_USE_SPRINTF)
/* Printf stream type */
typedef struct printf_stream {
    char buf[MR_CFG_PRINTF_BUF_SIZE];
    mr_size_t len;
    int total;
    int err;
} printf_stream_t;

static void printf_stream_flush(printf_stream_t *stream) {
    int ret;

    /* Check buffer and output status */
    if ((stream->len == 0U) || (stream->err < 0)) {
        return;
    }

    /* Output buffer */
    ret = mr_port_output(stream->buf, stream->len);
    if (ret < 0) {
        stream->err = ret;
    } else {
        stream->total += ret;
    }

    /* Reset buffer */
    stream->len = 0U;
}

static void printf_stream_out(char c, void *arg) {
    printf_stream_t *stream;

    /* Get stream */
    stream = (printf_stream_t *)arg;

    /* Check output status */
    if (stream->err < 0) {
        return;
    }

    /* Buffer character */
    stream->buf[stream->len++] = c;

    /* Flush full buffer */
    if (stream->len >= sizeof(stream->buf)) {
        printf_stream_flush(stream);
    }
}
#endif /* defined(MR_USE_SPRINTF) */

int mr_vprintf(const char *fmt, mr_va_list args) {
#if defined(MR_USE_SPRINTF)
    printf_stream_t stream;
#elif !defined(MR_USE_PRINTF_SAFE)
    char buf[MR_CFG_PRINTF_BUF_SIZE];
    int outsz, ret;
#else
    int outsz, ret;
    char *buf;
#endif /* defined(MR_USE_SPRINTF) */

    /* Check parameter */
    if ((!fmt) || (fmt[0] == '\0')) {
        return -MR_EINVAL;
    }

#if defined(MR_USE_SPRINTF)
    /* Init stream */
    stream.len = 0U;
    stream.total = 0;
    stream.err = 0;

    /* Format and flush output */
    mr_vcbprintf(printf_stream_out, &stream, fmt, args);
    printf_stream_flush(&stream);
    return (stream.err < 0) ? stream.err : stream.total;
#else
#if defined(MR_USE_PRINTF_SAFE)
    /* Allocate buffer */
    buf = mr_malloc(MR_CFG_PRINTF_BUF_SIZE);
    if (!buf) {
        return -MR_ENOMEM;
    }
#endif /* defined(MR_USE_PRINTF_SAFE) */

    /* Format string */
    ret = mr_vsnprintf(buf, MR_CFG_PRINTF_BUF_SIZE, fmt, args);
    if (ret >= 0) {
        outsz = MR_MIN(ret, (int)(MR_CFG_PRINTF_BUF_SIZE - 1U));
    } else {
        ret = -MR_EINVAL;
        goto _exit;
    }

    /* Output string */
    ret = mr_port_output(buf, (mr_size_t)outsz);
_exit:
#if defined(MR_USE_PRINTF_SAFE)
    /* Free buffer */
    mr_free(buf);
#endif /* defined(MR_USE_PRINTF_SAFE) */
    return ret;
#endif /* defined(MR_USE_SPRINTF) */
}

int mr_printf(const char *fmt, ...) {
    mr_va_list args;
    int ret;

    /* Check parameter */
    if ((!fmt) || (fmt[0] == '\0')) {
        return -MR_EINVAL;
    }

    /* Print */
    mr_va_start(args, fmt);
    ret = mr_vprintf(fmt, args);
    mr_va_end(args);
    return ret;
}
#endif /* defined(MR_USE_PRINTF) */
