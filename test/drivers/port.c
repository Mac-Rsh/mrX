/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-11        MacRsh        Split from drivers_deep.c (port cases)
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/port/mr_port_irq.h>
#include <mrX/port/mr_port_output.h>
#if defined(MR_USE_CLOCK_HIRES)
#include <mrX/port/mr_port_clock.h>
#endif /* defined(MR_USE_CLOCK_HIRES) */

MR_TEST(port, irq_disable_enable_nesting) {
    int m0, m1, m2, m3;

    /* Each disable returns the previous nesting level */
    m0 = mr_port_irq_disable();
    m1 = mr_port_irq_disable();
    m2 = mr_port_irq_disable();
    MR_TEST_EXPECT_EQ_INT(m1, m0 + 1);
    MR_TEST_EXPECT_EQ_INT(m2, m0 + 2);

    /* Enables must restore the level so a later disable matches again */
    mr_port_irq_enable(m2);
    mr_port_irq_enable(m1);
    m3 = mr_port_irq_disable();
    MR_TEST_EXPECT_EQ_INT(m3, m0 + 1);

    mr_port_irq_enable(m3);
    mr_port_irq_enable(m0);
}

MR_TEST(port, output_size_semantics) {
    /* The return value counts the characters actually printed */
    MR_TEST_EXPECT_EQ_INT(mr_port_output("mrX", 3), 3);
    MR_TEST_EXPECT_EQ_INT(mr_port_output("abcdef", 4), 4);

    /* A zero size must print nothing and return zero */
    MR_TEST_EXPECT_EQ_INT(mr_port_output("mrX", 0), 0);
}

#if defined(MR_USE_CLOCK_HIRES)
MR_TEST(port, clock_hires_monotonic_and_busy_wait) {
    mr_uint64_t t1, t2, t3, t4, t5;
    mr_int64_t delta;

    /* The host port initializes the hires clock to unix time in microseconds
     * at boot, so the tick must already be far beyond the epoch start */
    t1 = mr_port_clock_hires_tick();
    MR_TEST_EXPECT_GT_UINT(t1, 1000000000000ULL);

    /* A zero length busy wait must return immediately */
    mr_port_clock_busy_wait(0);
    t2 = mr_port_clock_hires_tick();
    MR_TEST_EXPECT_GE_UINT(t2, t1);

    /* The hires tick must advance by at least the busy wait duration */
    mr_port_clock_busy_wait(2000);
    t3 = mr_port_clock_hires_tick();
    delta = (mr_int64_t)(t3 - t2);
    MR_TEST_EXPECT_GE_INT(delta, 1000);

    /* Setting the tick moves the base, the clock must stay monotonic */
    t4 = mr_port_clock_hires_tick();
    mr_port_clock_hires_tick_set(t4);
    t5 = mr_port_clock_hires_tick();
    MR_TEST_EXPECT_GE_UINT(t5, t4);
}
#endif /* defined(MR_USE_CLOCK_HIRES) */

#endif /* defined(MR_USE_TEST) */
