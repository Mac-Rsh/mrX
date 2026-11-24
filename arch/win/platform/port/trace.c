/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/port/mr_port_trace.h>
#if defined(MR_USE_TRACE)
#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_MODULE_XTRACE)
#include <mrX/kernel/mr_initcall.h>
#include <mrX/libc/mr_errno.h>
#include <mrX/modules/xtrace/mr_xtrace.h>
#if defined(MR_USE_MODULE_XTRACE_PROTO_CTF)
#include <mrX/modules/xtrace/proto/mr_xtrace_ctf.h>
#endif /* defined(MR_USE_MODULE_XTRACE_PROTO_CTF) */
#include <stdio.h>

/* Windows file output poll interval (ms) */
#define WIN_XTRACE_POLL_MS              (20U)
#define WIN_XTRACE_CHUNK_MAX            (4096U)

/* Windows file output context type */
typedef struct mr_win_xtrace_file {
    mr_xtrace_output_t output;
    const char *path;
    FILE *fp;
    HANDLE event;
    HANDLE thread;
    volatile LONG running;
} mr_win_xtrace_file_t;

static mr_err_t win_file_init(void *priv);
static void win_file_deinit(void *priv);
static void win_file_notify(void *priv);

/* Windows file output operations */
static const mr_xtrace_output_ops_t __win_file_ops = {
    .notify = win_file_notify,
    .init = win_file_init,
    .deinit = win_file_deinit,
    .read = MR_NULL,
};

/* Windows file output default path */
#if defined(MR_USE_MODULE_XTRACE_PROTO_SYSVIEW)
#define WIN_XTRACE_DEFAULT_PATH "Xtrace.SVDat"
#elif defined(MR_USE_MODULE_XTRACE_PROTO_CTF)
#define WIN_XTRACE_DEFAULT_PATH "Xtrace_ctf"
#else
#define WIN_XTRACE_DEFAULT_PATH "Xtrace.bin"
#endif /* defined(MR_USE_MODULE_XTRACE_PROTO_SYSVIEW) */

/* Windows host core name */
#if defined(__x86_64__) || defined(_M_X64)
#define WIN_XTRACE_CORE_NAME "x86_64"
#elif defined(__i386__) || defined(_M_IX86)
#define WIN_XTRACE_CORE_NAME "x86"
#elif defined(__aarch64__) || defined(_M_ARM64)
#define WIN_XTRACE_CORE_NAME "ARM64"
#else
#define WIN_XTRACE_CORE_NAME "Host"
#endif /* defined(__x86_64__) || defined(_M_X64) */

/* Windows file output context definition */
static mr_win_xtrace_file_t __win_xtrace_file = {
    .path = WIN_XTRACE_DEFAULT_PATH,
    .fp = MR_NULL,
    .event = MR_NULL,
    .thread = MR_NULL,
    .running = 0,
};

#if defined(MR_USE_MODULE_XTRACE_PROTO_CTF)
static mr_err_t win_ctf_write_metadata(const char *dir) {
    char path[260], meta[4096];
    mr_ssize_t len;
    size_t written;
    int path_len;
    FILE *fp;

    /* Format CTF 1.8 metadata */
    len = mr_xtrace_ctf_metadata(meta, sizeof(meta));
    if (len <= 0) {
        return -MR_ENOMEM;
    }

    /* Write metadata file */
    path_len = snprintf(path, sizeof(path), "%s/metadata", dir);
    if ((path_len < 0) || ((size_t)path_len >= sizeof(path))) {
        return -MR_EINVAL;
    }
    fp = fopen(path, "wb");
    if (!fp) {
        return -MR_EIO;
    }
    written = fwrite(meta, 1U, (size_t)len, fp);
    if (fclose(fp) != 0) {
        return -MR_EIO;
    }
    if (written != (size_t)len) {
        return -MR_EIO;
    }
    return 0;
}
#endif /* defined(MR_USE_MODULE_XTRACE_PROTO_CTF) */

static void win_file_drain(mr_win_xtrace_file_t *ctx) {
    mr_size_t claimed;
    size_t written;
    void *ptr;

    if ((!ctx) || (!ctx->fp)) {
        return;
    }

    /* Drain FIFO via zero-copy read claim/finish */
    while ((claimed = mr_xtrace_read_claim(&ptr, WIN_XTRACE_CHUNK_MAX)) > 0U) {
        written = fwrite(ptr, 1U, (size_t)claimed, ctx->fp);
        mr_xtrace_read_finish((mr_size_t)written);
        if (written < (size_t)claimed) {
            break;
        }
    }
}

static DWORD WINAPI win_file_worker(LPVOID param) {
    mr_win_xtrace_file_t *ctx;

    /* Get context */
    ctx = (mr_win_xtrace_file_t *)param;

    /* Consume FIFO asynchronously until stopped */
    while (InterlockedCompareExchange(&ctx->running, 1, 1) != 0) {
        WaitForSingleObject(ctx->event, WIN_XTRACE_POLL_MS);
        win_file_drain(ctx);
    }

    /* Final flush and drain before closing */
    mr_xtrace_flush();
    win_file_drain(ctx);
    if (ctx->fp) {
        fflush(ctx->fp);
    }
    return 0;
}

static void win_file_notify(void *priv) {
    mr_win_xtrace_file_t *ctx;

    /* Get context */
    ctx = (mr_win_xtrace_file_t *)priv;

    /* Wake worker thread without blocking */
    if (ctx && ctx->event) {
        SetEvent(ctx->event);
    }
}

static mr_err_t win_file_init(void *priv) {
    mr_win_xtrace_file_t *ctx;
#if defined(MR_USE_MODULE_XTRACE_PROTO_CTF)
    char stream_path[260];
    int path_len;
#endif /* defined(MR_USE_MODULE_XTRACE_PROTO_CTF) */

    /* Check parameter */
    ctx = (mr_win_xtrace_file_t *)priv;
    if ((!ctx) || (!ctx->path)) {
        return -MR_EINVAL;
    }

#if defined(MR_USE_MODULE_XTRACE_PROTO_CTF)
    /* Create standard CTF directory and metadata */
    if (!CreateDirectoryA(ctx->path, MR_NULL)
        && (GetLastError() != ERROR_ALREADY_EXISTS)) {
        return -MR_EIO;
    }
    if (win_ctf_write_metadata(ctx->path) != 0) {
        return -MR_EIO;
    }

    /* Open CTF channel_0 stream */
    path_len
        = snprintf(stream_path, sizeof(stream_path), "%s/channel_0", ctx->path);
    if ((path_len < 0) || ((size_t)path_len >= sizeof(stream_path))) {
        return -MR_EINVAL;
    }
    ctx->fp = fopen(stream_path, "wb");
#else
    /* Open trace output file */
    ctx->fp = fopen(ctx->path, "wb");
#endif /* defined(MR_USE_MODULE_XTRACE_PROTO_CTF) */
    if (!ctx->fp) {
        return -MR_EIO;
    }

    /* Create wakeup event */
    ctx->event = CreateEventA(MR_NULL, FALSE, FALSE, MR_NULL);
    if (!ctx->event) {
        fclose(ctx->fp);
        ctx->fp = MR_NULL;
        return -MR_EIO;
    }

    /* Start asynchronous consumer thread */
    InterlockedExchange(&ctx->running, 1);
    ctx->thread = CreateThread(MR_NULL, 0, win_file_worker, ctx, 0, MR_NULL);
    if (!ctx->thread) {
        InterlockedExchange(&ctx->running, 0);
        CloseHandle(ctx->event);
        ctx->event = MR_NULL;
        fclose(ctx->fp);
        ctx->fp = MR_NULL;
        return -MR_EIO;
    }
    return 0;
}

static void win_file_deinit(void *priv) {
    mr_win_xtrace_file_t *ctx;

    /* Check parameter */
    ctx = (mr_win_xtrace_file_t *)priv;
    if (!ctx) {
        return;
    }

    /* Stop worker thread and wait for final FIFO drain */
    InterlockedExchange(&ctx->running, 0);
    if (ctx->event) {
        SetEvent(ctx->event);
    }
    if (ctx->thread) {
        WaitForSingleObject(ctx->thread, INFINITE);
        CloseHandle(ctx->thread);
        ctx->thread = MR_NULL;
    }
    if (ctx->event) {
        CloseHandle(ctx->event);
        ctx->event = MR_NULL;
    }

    /* Close trace output file */
    if (ctx->fp) {
        fflush(ctx->fp);
        fclose(ctx->fp);
        ctx->fp = MR_NULL;
    }
}

mr_err_t mr_win_xtrace_file_register(const char *path) {
    const char *old_path;
    mr_err_t ret;

    /* Unregister previous output so its worker drains and releases */
    mr_xtrace_output_unregister();
    if (MR_OBJECT_IS_INITED(&__win_xtrace_file.output)) {
        return -MR_EBUSY;
    }

    /* Set custom output file path if provided */
    old_path = __win_xtrace_file.path;
    if (path && (path[0] != '\0')) {
        __win_xtrace_file.path = path;
    }

    /* Initialize output object */
    ret = mr_xtrace_output_init(&__win_xtrace_file.output, &__win_file_ops,
                                &__win_xtrace_file);
    if (ret != 0) {
        __win_xtrace_file.path = old_path;
        return ret;
    }

    /* Register output and hand ownership reference to xtrace */
    ret = mr_xtrace_output_register(&__win_xtrace_file.output);
    mr_xtrace_output_del(&__win_xtrace_file.output);
    if (ret != 0) {
        __win_xtrace_file.path = old_path;
    }
    return ret;
}

static void win_xtrace_sysinfo_init(void) {
    mr_xtrace_sysinfo_t info = {
        .device_name = "Windows",
        .core_name = WIN_XTRACE_CORE_NAME,
    };

    /* Register Windows platform trace metadata */
    mr_xtrace_sysinfo_set(&info);
}
MR_INIT_EXPORT(win_xtrace_sysinfo_init, MR_INIT_LEVEL_DRIVER);

static void win_xtrace_file_auto_init(void) {
    /* Register default Windows file output */
    mr_win_xtrace_file_register(MR_NULL);
}
MR_INIT_EXPORT(win_xtrace_file_auto_init, MR_INIT_LEVEL_MODULE);
#endif /* defined(MR_USE_MODULE_XTRACE) */

void mr_port_trace(mr_uint64_t timestamp, mr_uint32_t type, const char *tag,
                   const char *name, const mr_uintptr_t *args, mr_size_t argc) {
#if defined(MR_USE_MODULE_XTRACE)
    mr_trace_event_t event;

    /* Build trace event */
    event.timestamp = timestamp;
    event.type = type;
    event.tag = tag;
    event.name = name;
    event.args = args;
    event.argc = argc;

    /* Emit trace event to xtrace */
    mr_xtrace_emit(&event);
#else
    MR_IGNORE(timestamp);
    MR_IGNORE(type);
    MR_IGNORE(tag);
    MR_IGNORE(name);
    MR_IGNORE(args);
    MR_IGNORE(argc);
#endif /* defined(MR_USE_MODULE_XTRACE) */
}
#endif /* defined(MR_USE_TRACE) */
