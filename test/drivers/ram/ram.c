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

static int ram_mount_count;
static int ram_unmount_count;
static mr_err_t ram_mount_ret;
static mr_err_t ram_unmount_ret;
static mr_ssize_t ram_write_ret;
static mr_ssize_t ram_read_ret;

static void ram_test_reset(void) {
    ram_mount_count = 0;
    ram_unmount_count = 0;
    ram_mount_ret = 0;
    ram_unmount_ret = 0;
    ram_write_ret = 3;
    ram_read_ret = 4;
}

static mr_err_t ram_mount(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    ram_mount_count++;
    return ram_mount_ret;
}

static mr_err_t ram_unmount(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    ram_unmount_count++;
    return ram_unmount_ret;
}

static mr_ssize_t ram_write(const mr_driver_t *drv, mr_ssize_t offset,
                            const mr_uint8_t *buf, mr_size_t size) {
    MR_IGNORE(drv);
    MR_IGNORE(offset);
    MR_IGNORE(buf);
    MR_IGNORE(size);
    return ram_write_ret;
}

static mr_ssize_t ram_read(const mr_driver_t *drv, mr_ssize_t offset,
                           mr_uint8_t *buf, mr_size_t size) {
    MR_IGNORE(drv);
    MR_IGNORE(offset);
    MR_IGNORE(size);
    if (buf) {
        *buf = 0x5a;
    }
    return ram_read_ret;
}

static const mr_ram_ops_t ram_ops = {
    .mount = ram_mount,
    .unmount = ram_unmount,
    .write = ram_write,
    .read = ram_read,
};

static mr_driver_t *ram_create(const mr_ram_ops_t *ops) {
    mr_ram_data_t *data = mr_malloc(sizeof(*data));

    if (!data) {
        return MR_NULL;
    }
    data->regions = MR_NULL;
    data->regions_num = 0;
    return mr_ram_create(data, MR_NULL, (mr_ram_ops_t *)ops);
}

static void test_ram_lifecycle(void) {
    mr_driver_t *drv;

    ram_test_reset();
    drv = ram_create(&ram_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(ram_mount_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(ram_mount_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_driver_shutdown(drv), 0);
    MR_TEST_ASSERT_EQ_INT(ram_unmount_count, 1);
    mr_driver_del(drv);
}
MR_TEST_EXPORT(ram, lifecycle, test_ram_lifecycle);

static void test_ram_mount_unmount_errors(void) {
    mr_driver_t *drv;

    ram_test_reset();
    ram_mount_ret = -MR_EIO;
    drv = ram_create(&ram_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), -MR_EIO);
    MR_TEST_ASSERT_EQ_INT(ram_mount_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_driver_is_ready(drv), MR_FALSE);
    mr_driver_del(drv);

    ram_test_reset();
    ram_unmount_ret = -MR_EIO;
    drv = ram_create(&ram_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_shutdown(drv), -MR_EIO);
    MR_TEST_ASSERT_EQ_INT(ram_unmount_count, 1);
    mr_ram_del(drv);
}
MR_TEST_EXPORT(ram, mount_unmount_errors, test_ram_mount_unmount_errors);

static void test_ram_io_and_unsupported(void) {
    mr_driver_t *drv;
    mr_driver_t *empty;
    mr_uint8_t input = 0x11;
    mr_uint8_t output = 0;
    mr_ram_ops_t empty_ops = {0};

    ram_test_reset();
    drv = ram_create(&ram_ops);
    empty = ram_create(&empty_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(empty, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(empty), 0);
    MR_TEST_ASSERT_EQ_INT(mr_ram_write(drv, -2, &input, 1), 3);
    MR_TEST_ASSERT_EQ_INT(mr_ram_read(drv, 2, &output, 1), 4);
    MR_TEST_ASSERT_EQ_INT(output, 0x5a);
    MR_TEST_ASSERT_EQ_INT(mr_ram_write(drv, 0, MR_NULL, 0), 3);
    MR_TEST_ASSERT_EQ_INT(mr_ram_read(drv, 0, MR_NULL, 0), 4);
    ram_write_ret = -MR_EIO;
    ram_read_ret = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(mr_ram_write(drv, 0, &input, 1), -MR_EIO);
    MR_TEST_ASSERT_EQ_INT(mr_ram_read(drv, 0, &output, 1), -MR_EIO);
    MR_TEST_ASSERT_EQ_INT(mr_ram_write(empty, 0, &input, 1), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_INT(mr_ram_read(empty, 0, &output, 1), -MR_ENOSYS);
    mr_driver_del(drv);
    mr_driver_del(empty);
}
MR_TEST_EXPORT(ram, io_and_unsupported, test_ram_io_and_unsupported);
#endif
