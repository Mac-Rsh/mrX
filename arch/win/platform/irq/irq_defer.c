/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-07        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_IRQ_DEFER_HOOK)
#include <mrX/kernel/mr_irq.h>
#include <mrX/libc/mr_compiler.h>

/* Irq defer semaphore */
static HANDLE __irq_defer_sem = NULL;

static DWORD WINAPI irq_defer_thread_entry(LPVOID param) {
    MR_IGNORE(param);

    /* Set thread priority */
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    /* Wait until the first deferred work is posted */
    WaitForSingleObject(__irq_defer_sem, INFINITE);

    while (1) {
        /* Execute irq defer */
        mr_irq_defer_execute();
    }
    return 0;
}

static void irq_defer_hook_suspend(void *param) {
    MR_IGNORE(param);

    /* Wait for the next deferred work */
    WaitForSingleObject(__irq_defer_sem, INFINITE);
}

static void irq_defer_hook_wakeup(void *param) {
    MR_IGNORE(param);

    /* Wake irq defer thread */
    ReleaseSemaphore(__irq_defer_sem, 1, NULL);
}
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */

void mr_win_platform_irq_defer_init(void) {
#if defined(MR_USE_IRQ_DEFER_HOOK)
    static const mr_irq_defer_hook_t __hook
        = {.suspend = irq_defer_hook_suspend, .wakeup = irq_defer_hook_wakeup};
    HANDLE thread;

    /* Init irq defer semaphore */
    __irq_defer_sem = CreateSemaphore(NULL, 0, 1, NULL);
    if (!__irq_defer_sem) {
        return;
    }

    /* Start irq defer thread */
    thread = CreateThread(NULL, 0, irq_defer_thread_entry, NULL, 0, NULL);
    if (!thread) {
        CloseHandle(__irq_defer_sem);
        __irq_defer_sem = NULL;
        return;
    }
    CloseHandle(thread);

    /* Init irq defer hook */
    mr_irq_defer_hook_set(&__hook);
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */
}
