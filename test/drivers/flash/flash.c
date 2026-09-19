/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_DRIVER_FLASH)
#include <mrX/drivers/flash/mr_flash.h>
#include <mrX/libc/mr_malloc.h>

static mr_flash_info_t flash_probe_info;
static mr_err_t flash_probe_ret;
static mr_uint64_t flash_offset;
static mr_uint64_t flash_erase_size;

static mr_err_t flash_probe(const mr_driver_t *drv, mr_flash_info_t *info) {
    MR_IGNORE(drv);
    if (flash_probe_ret == 0) {
        *info = flash_probe_info;
    }
    return flash_probe_ret;
}

static mr_err_t flash_region(const mr_driver_t *drv, mr_uint32_t index,
                             mr_flash_region_t *region) {
    MR_IGNORE(drv);
    region->offset = (mr_uint64_t)index * 128U;
    region->size = 128U;
    region->erase_size = 16U;
    return 0;
}

static mr_ssize_t flash_read(const mr_driver_t *drv, mr_uint64_t offset,
                             mr_uint8_t *buf, mr_size_t size) {
    MR_IGNORE(drv);
    flash_offset = offset;
    if (size != 0U) {
        buf[0] = 0x5aU;
    }
    return (mr_ssize_t)size;
}

static mr_ssize_t flash_program(const mr_driver_t *drv, mr_uint64_t offset,
                                const mr_uint8_t *buf, mr_size_t size) {
    MR_IGNORE(drv);
    MR_IGNORE(buf);
    flash_offset = offset;
    return (mr_ssize_t)size;
}

static mr_err_t flash_erase(const mr_driver_t *drv, mr_uint64_t offset,
                            mr_uint64_t size) {
    MR_IGNORE(drv);
    flash_offset = offset;
    flash_erase_size = size;
    return 0;
}

static const mr_flash_ops_t flash_ops = {
    .probe = flash_probe,
    .region = flash_region,
    .read = flash_read,
    .program = flash_program,
    .erase = flash_erase,
};

static mr_driver_t *flash_create(const mr_flash_ops_t *ops) {
    mr_flash_data_t *data = mr_malloc(sizeof(*data));

    if (data == MR_NULL) {
        return MR_NULL;
    }
    data->info = (mr_flash_info_t){0};
    return mr_flash_create(data, MR_NULL, (mr_flash_ops_t *)ops);
}

static void flash_reset(void) {
    flash_probe_info = (mr_flash_info_t){
        .size = 256U,
        .program_size = 4U,
        .page_size = 32U,
        .regions_num = 2U,
        .erased_value = 0xffU,
    };
    flash_probe_ret = 0;
    flash_offset = 0U;
    flash_erase_size = 0U;
}

MR_TEST(flash, probe_geometry_and_validation) {
    mr_driver_t *drv;
    mr_flash_info_t info;

    flash_reset();
    drv = flash_create(&flash_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_flash_info(drv, &info), 0);
    MR_TEST_ASSERT_EQ_UINT(info.size, 256U);
    MR_TEST_ASSERT_EQ_UINT(info.page_size, 32U);
    mr_flash_del(drv);

    flash_reset();
    flash_probe_info.page_size = 2U;
    drv = flash_create(&flash_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), -MR_EINVAL);
    mr_flash_del(drv);

    flash_reset();
    flash_probe_ret = -MR_EIO;
    drv = flash_create(&flash_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), -MR_EIO);
    mr_flash_del(drv);
}

MR_TEST(flash, region_io_and_optional_operations) {
    mr_driver_t *drv;
    mr_flash_ops_t optional_ops = flash_ops;
    mr_flash_region_t region;
    mr_uint8_t value = 0U;

    flash_reset();
    drv = flash_create(&flash_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_flash_region(drv, 1U, &region), 0);
    MR_TEST_ASSERT_EQ_UINT(region.offset, 128U);
    MR_TEST_ASSERT_EQ_INT(mr_flash_region(drv, 2U, &region), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_flash_read(drv, 255U, &value, 1U), 1);
    MR_TEST_ASSERT_EQ_INT(value, 0x5aU);
    MR_TEST_ASSERT_EQ_INT(mr_flash_program(drv, 4U, &value, 1U), 1);
    MR_TEST_ASSERT_EQ_UINT(flash_offset, 4U);
    MR_TEST_ASSERT_EQ_INT(mr_flash_erase(drv, 32U, 64U), 0);
    MR_TEST_ASSERT_EQ_UINT(flash_offset, 32U);
    MR_TEST_ASSERT_EQ_UINT(flash_erase_size, 64U);
    MR_TEST_ASSERT_EQ_INT(mr_flash_read(drv, 256U, MR_NULL, 0U), 0);
    MR_TEST_ASSERT_EQ_INT(mr_flash_read(drv, 257U, MR_NULL, 0U), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_flash_program(drv, 255U, &value, 2U), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_flash_erase(drv, 200U, 57U), -MR_EINVAL);

    optional_ops.region = MR_NULL;
    optional_ops.read = MR_NULL;
    optional_ops.program = MR_NULL;
    optional_ops.erase = MR_NULL;
    drv->ops = (mr_ptr_t)&optional_ops;
    MR_TEST_ASSERT_EQ_INT(mr_flash_region(drv, 0U, &region), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_INT(mr_flash_read(drv, 0U, &value, 1U), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_INT(mr_flash_program(drv, 0U, &value, 1U), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_INT(mr_flash_erase(drv, 0U, 16U), -MR_ENOSYS);
    drv->ops = (mr_ptr_t)&flash_ops;
    mr_flash_del(drv);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_DRIVER_FLASH) */
