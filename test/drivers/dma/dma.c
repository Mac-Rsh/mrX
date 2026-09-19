/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_DRIVER_DMA)
#include <mrX/drivers/dma/mr_dma.h>
#include <mrX/libc/mr_malloc.h>

static mr_uint32_t dma_channel;
static mr_dma_config_t dma_config;
static const mr_dma_transfer_t *dma_transfer;
static mr_bool_t dma_busy;
static mr_size_t dma_transfer_offset;
static mr_uint32_t dma_events;
static mr_bool_t dma_event_enable_state;
static int dma_event_attach_count;

static mr_err_t dma_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_dma_config_t *config) {
    MR_IGNORE(drv);
    dma_channel = channel;
    dma_config = *config;
    return 0;
}

static mr_err_t dma_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_dma_config_t *config) {
    MR_IGNORE(drv);
    dma_channel = channel;
    *config = dma_config;
    return 0;
}

static mr_err_t dma_start(const mr_driver_t *drv, mr_uint32_t channel,
                          const mr_dma_transfer_t *transfer) {
    MR_IGNORE(drv);
    dma_channel = channel;
    dma_transfer = transfer;
    return 0;
}

static mr_err_t dma_stop(const mr_driver_t *drv, mr_uint32_t channel) {
    MR_IGNORE(drv);
    dma_channel = channel;
    return 0;
}

static mr_err_t dma_is_busy(const mr_driver_t *drv, mr_uint32_t channel,
                            mr_bool_t *busy) {
    MR_IGNORE(drv);
    dma_channel = channel;
    *busy = dma_busy;
    return 0;
}

static mr_err_t dma_offset(const mr_driver_t *drv, mr_uint32_t channel,
                           mr_size_t *offset) {
    MR_IGNORE(drv);
    dma_channel = channel;
    *offset = dma_transfer_offset;
    return 0;
}

static mr_err_t dma_event_attach(const mr_driver_t *drv, mr_uint32_t channel,
                                 mr_waiter_t *entry) {
    MR_IGNORE(drv);
    MR_IGNORE(entry);
    dma_channel = channel;
    dma_event_attach_count++;
    return 0;
}

static mr_err_t dma_event_control(const mr_driver_t *drv, mr_uint32_t channel,
                                  mr_uint32_t events, mr_bool_t enable) {
    MR_IGNORE(drv);
    dma_channel = channel;
    dma_events = events;
    dma_event_enable_state = enable;
    return 0;
}

static const mr_dma_ops_t dma_ops = {
    .channel_configure = dma_channel_configure,
    .channel_configuration = dma_channel_configuration,
    .start = dma_start,
    .stop = dma_stop,
    .is_busy = dma_is_busy,
    .offset = dma_offset,
    .event_attach = dma_event_attach,
    .event_control = dma_event_control,
};

static mr_driver_t *dma_create(mr_uint64_t exist_mask,
                               const mr_dma_ops_t *ops) {
    mr_dma_data_t *data = mr_malloc(sizeof(*data));

    if (data == MR_NULL) {
        return MR_NULL;
    }
    *data = (mr_dma_data_t)MR_DMA_DATA_INIT(exist_mask);
    return mr_dma_create(data, MR_NULL, (mr_dma_ops_t *)ops);
}

MR_TEST(dma, channel_operations_and_bounds) {
    mr_driver_t *drv;
    mr_dma_config_t config = {
        .src_width = MR_DMA_WIDTH_16,
        .dst_width = MR_DMA_WIDTH_32,
        .src_adj = MR_DMA_ADJ_INC,
        .dst_adj = MR_DMA_ADJ_CONST,
        .src_burst = MR_DMA_BURST_4,
        .dst_burst = MR_DMA_BURST_8,
        .direction = MR_DMA_DIR_M2P,
    };
    mr_dma_config_t saved = {0};
    mr_uint8_t src[4] = {0};
    mr_uint8_t dst[4] = {0};
    mr_dma_transfer_t transfer = {
        .count = sizeof(src),
        .src = src,
        .dst = dst,
    };
    mr_bool_t busy;
    mr_size_t offset;

    dma_busy = MR_TRUE;
    dma_transfer_offset = 3U;
    dma_transfer = MR_NULL;
    drv = dma_create((1ULL << 2) | (1ULL << 63), &dma_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    MR_TEST_ASSERT_EQ_INT(mr_dma_configure(drv, 1U, &config), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_dma_configure(drv, 64U, &config), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_dma_configure(drv, 2U, &config), 0);
    MR_TEST_ASSERT_EQ_INT(mr_dma_configuration(drv, 2U, &saved), 0);
    MR_TEST_ASSERT_EQ_INT(saved.dst_width, MR_DMA_WIDTH_32);
    MR_TEST_ASSERT_EQ_INT(saved.direction, MR_DMA_DIR_M2P);
    MR_TEST_ASSERT_EQ_INT(mr_dma_start(drv, 63U, &transfer), 0);
    MR_TEST_ASSERT_EQ_PTR(dma_transfer, &transfer);
    MR_TEST_ASSERT_EQ_INT(mr_dma_is_busy(drv, 2U, &busy), 0);
    MR_TEST_ASSERT_EQ_INT(busy, MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_dma_offset(drv, 2U, &offset), 0);
    MR_TEST_ASSERT_EQ_UINT(offset, 3U);
    MR_TEST_ASSERT_EQ_INT(mr_dma_stop(drv, 2U), 0);
    MR_TEST_ASSERT_EQ_UINT(dma_channel, 2U);
    MR_TEST_ASSERT_EQ_INT(mr_dma_del(drv), 0);
}

MR_TEST(dma, event_paths) {
    mr_driver_t *drv;
    mr_dma_ops_t optional_ops = dma_ops;
    mr_waiter_t waiter;

    dma_event_attach_count = 0;
    drv = dma_create(1ULL << 3, &dma_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_dma_event_enable(drv, 3U, MR_DMA_WAIT_DONE), 0);
    MR_TEST_ASSERT_EQ_UINT(dma_events, MR_DMA_WAIT_DONE);
    MR_TEST_ASSERT_EQ_INT(dma_event_enable_state, MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_dma_event_disable(drv, 3U, MR_DMA_WAIT_ERROR), 0);
    MR_TEST_ASSERT_EQ_INT(dma_event_enable_state, MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_dma_event_attach(drv, 3U, &waiter), 0);
    MR_TEST_ASSERT_EQ_INT(dma_event_attach_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_dma_event_enable(drv, 4U, MR_DMA_WAIT_DONE),
                          -MR_EINVAL);

    optional_ops.event_attach = MR_NULL;
    optional_ops.event_control = MR_NULL;
    drv->ops = (mr_ptr_t)&optional_ops;
    MR_TEST_ASSERT_EQ_INT(mr_dma_event_attach(drv, 3U, &waiter), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_INT(mr_dma_event_enable(drv, 3U, MR_DMA_WAIT_DONE),
                          -MR_ENOSYS);
    drv->ops = (mr_ptr_t)&dma_ops;
    mr_dma_del(drv);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_DRIVER_DMA) */
