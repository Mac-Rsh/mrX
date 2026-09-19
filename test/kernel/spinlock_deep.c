/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_spinlock.h>

/* Tests */
MR_TEST(spinlock, unlock_without_lock) {
    mr_spinlock_t lock;

    mr_spinlock_init(&lock);
    mr_spinlock_unlock(&lock);
    mr_spinlock_unlock(&lock);
    MR_TEST_EXPECT_EQ_INT(mr_spinlock_is_locked(&lock), 0);

    /* The lock must still work after stray unlocks */
    mr_spinlock_lock(&lock);
    MR_TEST_EXPECT_EQ_INT(mr_spinlock_is_locked(&lock), 1);
    mr_spinlock_unlock(&lock);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 0);
}

MR_TEST(spinlock, locked_value_is_one) {
    mr_spinlock_t lock;

    mr_spinlock_init(&lock);
    mr_spinlock_lock(&lock);
    MR_TEST_EXPECT_EQ_INT((int)mr_atomic_load(&lock), 1);
    mr_spinlock_unlock(&lock);
    MR_TEST_EXPECT_EQ_INT((int)mr_atomic_load(&lock), 0);
}

MR_TEST(spinlock, macro_and_function_init) {
    static mr_spinlock_t static_lock = MR_SPINLOCK_INIT();
    mr_spinlock_t lock;

    MR_TEST_EXPECT_EQ_INT((int)mr_atomic_load(&static_lock), 0);
    mr_spinlock_init(&lock);
    MR_TEST_EXPECT_EQ_INT((int)mr_atomic_load(&lock), 0);

    /* Both initialization forms must produce a working lock */
    mr_spinlock_lock(&static_lock);
    MR_TEST_EXPECT_EQ_INT(mr_spinlock_is_locked(&static_lock), 1);
    mr_spinlock_unlock(&static_lock);
    mr_spinlock_lock(&lock);
    MR_TEST_EXPECT_EQ_INT(mr_spinlock_is_locked(&lock), 1);
    mr_spinlock_unlock(&lock);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 0);
}

MR_TEST(spinlock, reinit_while_locked) {
    mr_spinlock_t lock;

    mr_spinlock_init(&lock);
    mr_spinlock_lock(&lock);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 1);

    /* Re-initialization must reset the lock to the unlocked state */
    mr_spinlock_init(&lock);
    MR_TEST_EXPECT_EQ_INT(mr_spinlock_is_locked(&lock), 0);
    mr_spinlock_lock(&lock);
    MR_TEST_EXPECT_EQ_INT(mr_spinlock_is_locked(&lock), 1);
    mr_spinlock_unlock(&lock);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock), 0);
}

MR_TEST(spinlock, irq_nested_distinct_locks) {
    mr_spinlock_t lock_a;
    mr_spinlock_t lock_b;
    int mask_a;
    int mask_b;

    mr_spinlock_init(&lock_a);
    mr_spinlock_init(&lock_b);

    mask_a = mr_spinlock_lock_irqsave(&lock_a);
    mask_b = mr_spinlock_lock_irqsave(&lock_b);
    MR_TEST_EXPECT_EQ_INT(mr_spinlock_is_locked(&lock_a), 1);
    MR_TEST_EXPECT_EQ_INT(mr_spinlock_is_locked(&lock_b), 1);
    /* Nested irqsave calls must return distinct nesting masks */
    MR_TEST_EXPECT_NE_INT(mask_a, mask_b);

    mr_spinlock_unlock_irqrestore(&lock_b, mask_b);
    MR_TEST_EXPECT_EQ_INT(mr_spinlock_is_locked(&lock_b), 0);
    MR_TEST_EXPECT_EQ_INT(mr_spinlock_is_locked(&lock_a), 1);
    mr_spinlock_unlock_irqrestore(&lock_a, mask_a);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock_a), 0);
}

MR_TEST(spinlock, alternate_two_locks) {
    mr_spinlock_t lock_a;
    mr_spinlock_t lock_b;
    int i;

    mr_spinlock_init(&lock_a);
    mr_spinlock_init(&lock_b);
    for (i = 0; i < 50; i++) {
        mr_spinlock_lock(&lock_a);
        MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock_a), 1);
        MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock_b), 0);
        mr_spinlock_lock(&lock_b);
        MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock_a), 1);
        MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock_b), 1);
        mr_spinlock_unlock(&lock_a);
        MR_TEST_EXPECT_EQ_INT(mr_spinlock_is_locked(&lock_a), 0);
        MR_TEST_EXPECT_EQ_INT(mr_spinlock_is_locked(&lock_b), 1);
        mr_spinlock_unlock(&lock_b);
    }
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock_a), 0);
    MR_TEST_ASSERT_EQ_INT(mr_spinlock_is_locked(&lock_b), 0);
}

MR_TEST(spinlock, many_independent_locks) {
    mr_spinlock_t locks[8];
    int i;

    for (i = 0; i < 8; i++) {
        mr_spinlock_init(&locks[i]);
    }
    for (i = 0; i < 8; i++) {
        mr_spinlock_lock(&locks[i]);
        MR_TEST_EXPECT_EQ_INT(mr_spinlock_is_locked(&locks[i]), 1);
    }
    for (i = 0; i < 8; i++) {
        /* Unlocking one lock must not affect its siblings */
        mr_spinlock_unlock(&locks[i]);
    }
    for (i = 0; i < 8; i++) {
        MR_TEST_EXPECT_EQ_INT(mr_spinlock_is_locked(&locks[i]), 0);
    }
}

#endif /* defined(MR_USE_TEST) */
