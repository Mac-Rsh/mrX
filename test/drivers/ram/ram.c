/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_DRIVER_RAM)
#include <mrX/drivers/ram/mr_ram.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

static mr_uint8_t ram_region_mem[64];
static int ram_probe_count;
static int ram_region_count;
static int ram_read_count;
static int ram_write_count;
static mr_err_t ram_probe_ret;
static mr_uint64_t ram_size;
static mr_uint32_t ram_regions_num;
static mr_uint64_t ram_offset_seen;
static mr_size_t ram_size_seen;

static void ram_test_reset(void) {
    ram_probe_count = 0;
    ram_region_count = 0;
    ram_read_count = 0;
    ram_write_count = 0;
    ram_probe_ret = 0;
    ram_size = sizeof(ram_region_mem);
    ram_regions_num = 1U;
    ram_offset_seen = 0U;
    ram_size_seen = 0U;
}

static mr_err_t ram_probe(const mr_driver_t *drv, mr_ram_info_t *info) {
    MR_IGNORE(drv);
    ram_probe_count++;
    if (ram_probe_ret != 0) {
        return ram_probe_ret;
    }
    info->size = ram_size;
    info->regions_num = ram_regions_num;
    return 0;
}

static mr_err_t ram_region(const mr_driver_t *drv, mr_uint32_t index,
                           mr_ram_region_t *region) {
    MR_IGNORE(drv);
    MR_IGNORE(index);
    ram_region_count++;
    region->address = ram_region_mem;
    region->size = sizeof(ram_region_mem);
    return 0;
}

static mr_ssize_t ram_read(const mr_driver_t *drv, mr_uint64_t offset,
                           mr_uint8_t *buf, mr_size_t size) {
    MR_IGNORE(drv);
    ram_read_count++;
    ram_offset_seen = offset;
    ram_size_seen = size;
    if ((buf != MR_NULL) && (size != 0U)) {
        *buf = 0x5a;
    }
    return (mr_ssize_t)size;
}

static mr_ssize_t ram_write(const mr_driver_t *drv, mr_uint64_t offset,
                            const mr_uint8_t *buf, mr_size_t size) {
    MR_IGNORE(drv);
    MR_IGNORE(buf);
    ram_write_count++;
    ram_offset_seen = offset;
    ram_size_seen = size;
    return (mr_ssize_t)size;
}

static const mr_ram_ops_t ram_ops = {
    .probe = ram_probe,
    .region = ram_region,
    .read = ram_read,
    .write = ram_write,
};

static const mr_ram_ops_t ram_probe_only_ops = {
    .probe = ram_probe,
};

static mr_driver_t *ram_create(const mr_ram_ops_t *ops) {
    mr_ram_data_t *data = mr_malloc(sizeof(*data));

    if (data == MR_NULL) {
        return MR_NULL;
    }
    mr_memset(data, 0, sizeof(*data));
    return mr_ram_create(data, MR_NULL, (mr_ram_ops_t *)ops);
}

MR_TEST(ram, probe_lifecycle) {
    mr_driver_t *drv;
    mr_ram_info_t info;

    ram_test_reset();
    drv = ram_create(&ram_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(ram_probe_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_ram_info(drv, &info), 0);
    MR_TEST_ASSERT_EQ_UINT(info.size, sizeof(ram_region_mem));
    MR_TEST_ASSERT_EQ_UINT(info.regions_num, 1U);

    /* Setup is idempotent while the driver remains ready */
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(ram_probe_count, 1);

    /* Shutdown clears cached geometry; a later setup probes again */
    MR_TEST_ASSERT_EQ_INT(mr_driver_shutdown(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(ram_probe_count, 2);
    mr_ram_del(drv);
}

MR_TEST(ram, probe_validation) {
    mr_driver_t *drv;

    ram_test_reset();
    ram_probe_ret = -MR_EIO;
    drv = ram_create(&ram_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), -MR_EIO);
    MR_TEST_ASSERT_EQ_INT(ram_probe_count, 1);
    mr_driver_del(drv);

    ram_test_reset();
    ram_size = 0U;
    drv = ram_create(&ram_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), -MR_EINVAL);
    mr_driver_del(drv);

    ram_test_reset();
    drv = ram_create(&ram_probe_only_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), -MR_EINVAL);
    mr_driver_del(drv);

    /* Indirect RAM is valid when no directly addressable region exists */
    ram_test_reset();
    ram_regions_num = 0U;
    drv = ram_create(&ram_probe_only_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    mr_ram_del(drv);
}

MR_TEST(ram, region_and_io) {
    mr_driver_t *drv;
    mr_ram_region_t region;
    mr_uint8_t input = 0x11;
    mr_uint8_t output = 0U;

    ram_test_reset();
    drv = ram_create(&ram_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    MR_TEST_ASSERT_EQ_INT(mr_ram_region(drv, 0U, &region), 0);
    MR_TEST_ASSERT_EQ_PTR(region.address, ram_region_mem);
    MR_TEST_ASSERT_EQ_UINT(region.size, sizeof(ram_region_mem));
    MR_TEST_ASSERT_EQ_INT(ram_region_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_ram_region(drv, 1U, &region), -MR_EINVAL);

    MR_TEST_ASSERT_EQ_INT(mr_ram_write(drv, 2U, &input, 1U), 1);
    MR_TEST_ASSERT_EQ_UINT(ram_offset_seen, 2U);
    MR_TEST_ASSERT_EQ_UINT(ram_size_seen, 1U);
    MR_TEST_ASSERT_EQ_INT(mr_ram_read(drv, 2U, &output, 1U), 1);
    MR_TEST_ASSERT_EQ_INT(output, 0x5a);

    /* A zero-size access at the end is valid, beyond the end is not */
    MR_TEST_ASSERT_EQ_INT(
        mr_ram_write(drv, sizeof(ram_region_mem), MR_NULL, 0U), 0);
    MR_TEST_ASSERT_EQ_INT(mr_ram_read(drv, sizeof(ram_region_mem), MR_NULL, 0U),
                          0);
    MR_TEST_ASSERT_EQ_INT(
        mr_ram_write(drv, sizeof(ram_region_mem) + 1U, MR_NULL, 0U),
        -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(
        mr_ram_read(drv, sizeof(ram_region_mem) - 1U, &output, 2U), -MR_EINVAL);

    mr_ram_del(drv);
}

MR_TEST(ram, unsupported_io) {
    mr_ram_data_t data = {0};
    mr_ram_ops_t ops = ram_probe_only_ops;
    mr_ram_region_t region;
    mr_driver_t drv;
    mr_uint8_t value = 0U;

    ram_test_reset();
    ram_regions_num = 0U;
    MR_TEST_ASSERT_EQ_INT(mr_ram_init(&drv, &data, MR_NULL, &ops), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_ram_region(&drv, 0U, &region), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_ram_read(&drv, 0U, &value, 1U), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_ram_write(&drv, 0U, &value, 1U), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_PTR(mr_ram_get(&drv), &drv);
    MR_TEST_EXPECT_EQ_INT(mr_ram_del(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_ram_put(&drv), MR_TRUE);
}

#endif
