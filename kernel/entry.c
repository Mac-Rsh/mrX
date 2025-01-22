/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_initcall.h>
#ifdef MR_USE_THREAD
#include <kernel/thread/mr_thread.h>

MR_INLINE void thread_main_entry(void *args) {
    extern int main(void);
    MR_UNUSED(args);
    /* Run main */
    main();
}

MR_INLINE void thread_main(void) {
#if !defined(MR_CFG_MAIN_THREAD_STACK_SIZE)
#define MR_CFG_MAIN_THREAD_STACK_SIZE   (1024)
#endif /* !defined(MR_CFG_MAIN_THREAD_STACK_SIZE) */
    static mr_uint8_t main_stack[MR_CFG_MAIN_THREAD_STACK_SIZE];
    static mr_thread_t main_thread;

    /* Init main thread */
#if !defined(MR_CFG_MAIN_THREAD_PRIORITY)
#define MR_CFG_MAIN_THREAD_PRIORITY     (MR_CFG_THREAD_PRIORITY_MAX / 2)
#endif /* !defined(MR_CFG_MAIN_THREAD_PRIORITY) */
#if MR_CFG_MAIN_THREAD_PRIORITY >= MR_CFG_THREAD_PRIORITY_MAX
#error "Main thread priority must be less than thread priority max."
#endif /* MR_CFG_MAIN_THREAD_PRIORITY >= MR_CFG_THREAD_PRIORITY_MAX */
    mr_thread_init(&main_thread, "main", thread_main_entry, MR_NULL, main_stack,
                   MR_CFG_MAIN_THREAD_STACK_SIZE, MR_CFG_MAIN_THREAD_PRIORITY,
                   10);

    /* Startup main thread */
    mr_thread_startup(&main_thread);
}
#endif /* MR_USE_THREAD */

MR_INLINE void entry(void) {
    /* Run autoinit(kernel) */
    mr_autoinit_kernel();

    /* Run main */
#ifdef MR_USE_THREAD
    thread_main();
#else
    extern int main(void);
    main();
#endif /* MR_USE_THREAD */
}

/* MR-X entry point */
#if defined(__GNUC__) || defined(__clang__)
int mr_entry(void) {
    entry();
    return 0;
}
#elif defined(__CC_ARM)
extern int $Super$$main(void);
int $Sub$$main(void) {
    entry();
    return 0;
}
#endif /* defined(__GNUC__) || defined(__clang__) */
