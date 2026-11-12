/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        Deep test version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_hook.h>

/* Helpers */
typedef void(deep_hook_fn_t)(void);

typedef struct {
    int id;
} deep_hook_data_t;

static int deep_hook_count;
static int deep_hook_seq[8];
static int deep_hook_seq_n;
static mr_hook_t *deep_hook_self;
static int deep_hook_expanded;

static void deep_hook_mark1(void) {
    deep_hook_count += 1;
    if (deep_hook_seq_n < 8) {
        deep_hook_seq[deep_hook_seq_n++] = 1;
    }
}

static void deep_hook_mark2(void) {
    deep_hook_count += 2;
    if (deep_hook_seq_n < 8) {
        deep_hook_seq[deep_hook_seq_n++] = 2;
    }
}

static void deep_hook_mark3(void) {
    deep_hook_count += 3;
    if (deep_hook_seq_n < 8) {
        deep_hook_seq[deep_hook_seq_n++] = 3;
    }
}

static void deep_hook_expand(void) {
    deep_hook_count += 1;

    /* On the first call add another entry to the hook being iterated */
    if (!deep_hook_expanded) {
        deep_hook_expanded = 1;
        mr_hook_add(deep_hook_self, (mr_ptr_t)deep_hook_expand);
    }
}

MR_TEST(hook, deep_fill_to_capacity) {
    mr_ptr_t buf[3];
    mr_hook_t hook;
    deep_hook_fn_t *entry;
    mr_uint32_t i;

    deep_hook_count = 0;
    deep_hook_seq_n = 0;
    mr_hook_init(&hook, buf, 3);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark1), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark2), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark3), 0);
    MR_TEST_ASSERT_EQ_INT(hook.next, 3);

    MR_HOOK_EACH(i, entry, deep_hook_fn_t, &hook) {
        entry();
    }
    MR_TEST_ASSERT_EQ_INT(deep_hook_count, 6);
    MR_TEST_ASSERT_EQ_INT(deep_hook_seq[0], 1);
    MR_TEST_ASSERT_EQ_INT(deep_hook_seq[1], 2);
    MR_TEST_ASSERT_EQ_INT(deep_hook_seq[2], 3);
}

MR_TEST(hook, deep_overflow_keeps_state) {
    mr_ptr_t buf[2];
    mr_hook_t hook;
    deep_hook_fn_t *entry;
    mr_uint32_t i;

    deep_hook_count = 0;
    deep_hook_seq_n = 0;
    mr_hook_init(&hook, buf, 2);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark1), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark2), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark3),
                          -MR_ENOMEM);
    MR_TEST_ASSERT_EQ_INT(hook.next, 2);

    /* Iteration must still run exactly the stored entries in order */
    MR_HOOK_EACH(i, entry, deep_hook_fn_t, &hook) {
        entry();
    }
    MR_TEST_ASSERT_EQ_INT(deep_hook_count, 3);
    MR_TEST_ASSERT_EQ_INT(deep_hook_seq[0], 1);
    MR_TEST_ASSERT_EQ_INT(deep_hook_seq[1], 2);
    MR_TEST_ASSERT_EQ_INT(deep_hook_seq_n, 2);

    /* Repeated overflow must keep failing without side effects */
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark3),
                          -MR_ENOMEM);
    MR_TEST_ASSERT_EQ_INT(hook.next, 2);
}

MR_TEST(hook, deep_each_empty) {
    mr_ptr_t buf[4];
    mr_hook_t hook;
    deep_hook_fn_t *entry;
    mr_uint32_t i;

    deep_hook_count = 0;
    mr_hook_init(&hook, buf, 4);
    i = 123;
    MR_HOOK_EACH(i, entry, deep_hook_fn_t, &hook) {
        entry();
        deep_hook_count++;
    }
    MR_TEST_ASSERT_EQ_INT(deep_hook_count, 0);
    MR_TEST_ASSERT_EQ_INT(i, 0);
}

MR_TEST(hook, deep_each_full_boundary) {
    mr_ptr_t buf[3];
    mr_hook_t hook;
    deep_hook_fn_t *entry;
    mr_uint32_t i;

    deep_hook_count = 0;
    mr_hook_init(&hook, buf, 3);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark1), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark2), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark3), 0);

    MR_HOOK_EACH(i, entry, deep_hook_fn_t, &hook) {
        entry();
    }
    MR_TEST_ASSERT_EQ_INT(deep_hook_count, 6);
    /* The loop index must end exactly at next after a full sweep */
    MR_TEST_EXPECT_EQ_INT(i, 3);
}

MR_TEST(hook, deep_duplicate_entries) {
    mr_ptr_t buf[2];
    mr_hook_t hook;
    deep_hook_fn_t *entry;
    mr_uint32_t i;

    deep_hook_count = 0;
    mr_hook_init(&hook, buf, 2);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark1), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark1), 0);

    MR_HOOK_EACH(i, entry, deep_hook_fn_t, &hook) {
        entry();
    }
    MR_TEST_ASSERT_EQ_INT(deep_hook_count, 2);
    MR_TEST_ASSERT_EQ_PTR(hook.entry[0], hook.entry[1]);
}

MR_TEST(hook, deep_data_pointers) {
    mr_ptr_t buf[2];
    mr_hook_t hook;
    deep_hook_data_t a, b;
    deep_hook_data_t *entry;
    mr_uint32_t i;
    int seen;

    a.id = 11;
    b.id = 22;
    mr_hook_init(&hook, buf, 2);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)&a), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)&b), 0);

    seen = 0;
    MR_HOOK_EACH(i, entry, deep_hook_data_t, &hook) {
        seen += entry->id;
    }
    MR_TEST_ASSERT_EQ_INT(seen, 33);
    MR_TEST_ASSERT_EQ_PTR(hook.entry[0], &a);
    MR_TEST_ASSERT_EQ_PTR(hook.entry[1], &b);
}

MR_TEST(hook, deep_each_rereads_next) {
    mr_ptr_t buf[4];
    mr_hook_t hook;
    deep_hook_fn_t *entry;
    mr_uint32_t i;

    deep_hook_count = 0;
    deep_hook_expanded = 0;
    deep_hook_self = &hook;
    mr_hook_init(&hook, buf, 4);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_expand), 0);

    /* MR_HOOK_EACH re-evaluates next every step so the new entry runs too */
    MR_HOOK_EACH(i, entry, deep_hook_fn_t, &hook) {
        entry();
    }
    MR_TEST_EXPECT_EQ_INT(deep_hook_count, 2);
    MR_TEST_EXPECT_EQ_INT(hook.next, 2);
}

MR_TEST(hook, deep_add_after_each_keeps_order) {
    mr_ptr_t buf[4];
    mr_hook_t hook;
    deep_hook_fn_t *entry;
    mr_uint32_t i;

    deep_hook_count = 0;
    deep_hook_seq_n = 0;
    mr_hook_init(&hook, buf, 4);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark1), 0);

    MR_HOOK_EACH(i, entry, deep_hook_fn_t, &hook) {
        entry();
    }

    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark2), 0);
    MR_HOOK_EACH(i, entry, deep_hook_fn_t, &hook) {
        entry();
    }
    MR_TEST_ASSERT_EQ_INT(deep_hook_seq[0], 1);
    MR_TEST_ASSERT_EQ_INT(deep_hook_seq[1], 1);
    MR_TEST_ASSERT_EQ_INT(deep_hook_seq[2], 2);
    MR_TEST_ASSERT_EQ_INT(deep_hook_seq_n, 3);
}

MR_TEST(hook, deep_zero_size_each) {
    mr_hook_t hook;
    deep_hook_fn_t *entry;
    mr_uint32_t i;

    deep_hook_count = 0;
    mr_hook_init(&hook, MR_NULL, 0);
    i = 7;
    MR_HOOK_EACH(i, entry, deep_hook_fn_t, &hook) {
        entry();
        deep_hook_count++;
    }
    MR_TEST_ASSERT_EQ_INT(deep_hook_count, 0);
    MR_TEST_EXPECT_EQ_INT(i, 0);
}

MR_TEST(hook, deep_next_monotonic) {
    mr_ptr_t buf[5];
    mr_hook_t hook;

    mr_hook_init(&hook, buf, 5);
    MR_TEST_ASSERT_EQ_INT(hook.next, 0);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark1), 0);
    MR_TEST_ASSERT_EQ_INT(hook.next, 1);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark2), 0);
    MR_TEST_ASSERT_EQ_INT(hook.next, 2);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark3), 0);
    MR_TEST_ASSERT_EQ_INT(hook.next, 3);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark1), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark2), 0);
    MR_TEST_ASSERT_EQ_INT(hook.next, 5);
    MR_TEST_ASSERT_EQ_INT(mr_hook_add(&hook, (mr_ptr_t)deep_hook_mark3),
                          -MR_ENOMEM);
    MR_TEST_ASSERT_EQ_INT(hook.next, 5);
}

#endif /* defined(MR_USE_TEST) */
