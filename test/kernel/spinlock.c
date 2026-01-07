/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_spinlock.h>

/* -------------------------------------------------------------------------- */
/* Test: static initializer                                                   */
static void test_spinlock_static_init(void) {
    static mr_spinlock_t lock = MR_SPINLOCK_INIT();

    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 0);
}
MR_TEST_EXPORT(spinlock, static_init, test_spinlock_static_init);

/* -------------------------------------------------------------------------- */
/* Test: init / lock / unlock                                                 */
static void test_spinlock_lock_unlock(void) {
    mr_spinlock_t lock;

    mr_spinlock_init(&lock);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 0);

    mr_spinlock_lock(&lock);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 1);

    mr_spinlock_unlock(&lock);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 0);
}
MR_TEST_EXPORT(spinlock, lock_unlock, test_spinlock_lock_unlock);

/* -------------------------------------------------------------------------- */
/* Test: lock_irqsave / unlock_irqrestore                                     */
static void test_spinlock_irq_save_restore(void) {
    mr_spinlock_t lock;
    int mask;

    mr_spinlock_init(&lock);
    mask = mr_spinlock_lock_irqsave(&lock);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 1);

    mr_spinlock_unlock_irqrestore(&lock, mask);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 0);
}
MR_TEST_EXPORT(spinlock, irq_save_restore, test_spinlock_irq_save_restore);
#endif /* defined(MR_USE_TEST) */
