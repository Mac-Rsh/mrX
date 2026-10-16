/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_DRIVER_FLASH_H__
#define __MR_DRIVER_FLASH_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_DRIVER_FLASH)
#include <mrX/drivers/mr_driver.h>
#endif /* defined(MR_USE_DRIVER_FLASH) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Drivers-FLASH
 * @{
 */

#if defined(MR_USE_DRIVER_FLASH)
/* Flash information type */
typedef struct mr_flash_info {
    mr_uint64_t size;
    mr_uint32_t program_size;
    mr_uint32_t page_size;
    mr_uint32_t regions_num;
    mr_uint8_t erased_value;
} mr_flash_info_t;

/* Flash erase region type */
typedef struct mr_flash_region {
    mr_uint64_t offset;
    mr_uint64_t size;
    mr_uint32_t erase_size;
} mr_flash_region_t;

/* Flash data type */
MR_DRIVER_CLAZZES_EXPORT(flash);
typedef struct mr_flash_data {
    mr_flash_info_t info;
} mr_flash_data_t;

/* Flash operations type */
typedef struct mr_flash_ops {
    mr_err_t (*probe)(const mr_driver_t *drv, mr_flash_info_t *info);
    mr_err_t (*region)(const mr_driver_t *drv, mr_uint32_t index,
                       mr_flash_region_t *region);
    mr_ssize_t (*read)(const mr_driver_t *drv, mr_uint64_t offset,
                       mr_uint8_t *buf, mr_size_t size);
    mr_ssize_t (*program)(const mr_driver_t *drv, mr_uint64_t offset,
                          const mr_uint8_t *buf, mr_size_t size);
    mr_err_t (*erase)(const mr_driver_t *drv, mr_uint64_t offset,
                      mr_uint64_t size);
} mr_flash_ops_t;

/**
 * @brief This macro function defines a FLASH driver.
 *
 * @param _name The FLASH driver name.
 * @param _data The FLASH driver data.
 * @param _priv The FLASH private data.
 * @param _ops The FLASH operations.
 */
#define MR_FLASH_DEFINE(_name, _data, _priv, _ops)                             \
    MR_DRIVER_DEFINE(_name, MR_DRIVER_CLAZZES_STATIK(flash),                   \
                     MR_DRIVER_TYPE_FLASH, _data, _priv, _ops)

/**
 * @brief This macro function installs a FLASH driver.
 *
 * @param _name The FLASH driver name.
 * @param _data The FLASH driver data.
 * @param _priv The FLASH private data.
 * @param _ops The FLASH operations.
 * @param _level The init level.
 */
#define MR_FLASH_INSTALL(_name, _data, _priv, _ops, _level)                    \
    MR_DRIVER_INSTALL(_name, MR_DRIVER_CLAZZES_STATIK(flash),                  \
                      MR_DRIVER_TYPE_FLASH, _data, _priv, _ops, _level)

/**
 * @brief This function initializes a FLASH driver.
 *
 * @param drv The FLASH driver.
 * @param data The FLASH driver data.
 * @param priv The FLASH private data.
 * @param ops The FLASH operations.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_flash_init(mr_driver_t *drv, mr_flash_data_t *data,
                                 void *priv, mr_flash_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(flash);

    /* Check parameter */
    MR_ASSERT(drv != MR_NULL);
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Init FLASH driver */
    return mr_driver_init(drv, dclazz, MR_DRIVER_TYPE_FLASH, data, priv, ops);
}

/**
 * @brief This function creates a FLASH driver.
 *
 * @param data The FLASH driver data.
 * @param priv The FLASH private data.
 * @param ops The FLASH operations.
 * @return The FLASH driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_flash_create(mr_flash_data_t *data, void *priv,
                                       mr_flash_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_DYNAMIC(flash);

    /* Check parameter */
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Create FLASH driver */
    return mr_driver_create(dclazz, MR_DRIVER_TYPE_FLASH, data, priv, ops);
}

/**
 * @brief This function deletes a FLASH driver.
 *
 * @param drv The FLASH driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_flash_del(mr_driver_t *drv) {
    /* Check parameter */
    MR_DRIVER_ASSERT_TYPE(drv, MR_DRIVER_TYPE_FLASH);

    /* Delete driver */
    return mr_driver_del(drv);
}

/**
 * @brief This function gets FLASH information discovered during setup.
 *
 * @param drv The FLASH driver.
 * @param info The FLASH information output.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_flash_info(mr_driver_t *drv, mr_flash_info_t *info) {
    mr_flash_data_t *flash_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_FLASH);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(info != MR_NULL);

    /* Get FLASH data */
    flash_data = (mr_flash_data_t *)MR_DRIVER_DATA(drv);

    /* Get FLASH information */
    *info = flash_data->info;
    return 0;
}

/**
 * @brief This function gets a FLASH erase region.
 *
 * @param drv The FLASH driver.
 * @param index The region index.
 * @param region The region information output.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_flash_region(mr_driver_t *drv, mr_uint32_t index,
                                   mr_flash_region_t *region) {
    const mr_flash_ops_t *flash_ops;
    mr_flash_data_t *flash_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_FLASH);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(region != MR_NULL);

    /* Get FLASH operations and data */
    flash_ops = (const mr_flash_ops_t *)MR_DRIVER_OPS(drv);
    flash_data = (mr_flash_data_t *)MR_DRIVER_DATA(drv);

    /* Check whether operation is supported */
    if (!flash_ops->region) {
        return -MR_ENOSYS;
    }

    /* Check region index */
    if (index >= flash_data->info.regions_num) {
        return -MR_EINVAL;
    }

    /* Get FLASH erase region */
    return flash_ops->region(drv, index, region);
}

/**
 * @brief This function reads FLASH data.
 *
 * @param drv The FLASH driver.
 * @param offset The FLASH offset.
 * @param buf The read buffer.
 * @param size The read size.
 * @return The actual size of read data on success, a negative error code on
 * failure.
 */
MR_INLINE mr_ssize_t mr_flash_read(mr_driver_t *drv, mr_uint64_t offset,
                                   void *buf, mr_size_t size) {
    const mr_flash_ops_t *flash_ops;
    mr_flash_data_t *flash_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_FLASH);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0U));

    /* Get FLASH operations and data */
    flash_ops = (const mr_flash_ops_t *)MR_DRIVER_OPS(drv);
    flash_data = (mr_flash_data_t *)MR_DRIVER_DATA(drv);

    /* Check whether operation is supported */
    if (!flash_ops->read) {
        return -MR_ENOSYS;
    }

    /* Check range */
    if ((offset > flash_data->info.size)
        || ((mr_uint64_t)size > (flash_data->info.size - offset))) {
        return -MR_EINVAL;
    }

    /* Read FLASH data */
    return flash_ops->read(drv, offset, buf, size);
}

/**
 * @brief This function programs FLASH data without erasing it first.
 *
 * @param drv The FLASH driver.
 * @param offset The FLASH offset.
 * @param buf The program buffer.
 * @param size The program size.
 * @return The actual size of programmed data on success, a negative error code
 * on failure.
 */
MR_INLINE mr_ssize_t mr_flash_program(mr_driver_t *drv, mr_uint64_t offset,
                                      const void *buf, mr_size_t size) {
    const mr_flash_ops_t *flash_ops;
    mr_flash_data_t *flash_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_FLASH);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0U));

    /* Get FLASH operations and data */
    flash_ops = (const mr_flash_ops_t *)MR_DRIVER_OPS(drv);
    flash_data = (mr_flash_data_t *)MR_DRIVER_DATA(drv);

    /* Check whether operation is supported */
    if (!flash_ops->program) {
        return -MR_ENOSYS;
    }

    /* Check range */
    if ((offset > flash_data->info.size)
        || ((mr_uint64_t)size > (flash_data->info.size - offset))) {
        return -MR_EINVAL;
    }

    /* Program FLASH data */
    return flash_ops->program(drv, offset, buf, size);
}

/**
 * @brief This function erases a FLASH range.
 *
 * @param drv The FLASH driver.
 * @param offset The FLASH offset.
 * @param size The erase size.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_flash_erase(mr_driver_t *drv, mr_uint64_t offset,
                                  mr_uint64_t size) {
    const mr_flash_ops_t *flash_ops;
    mr_flash_data_t *flash_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_FLASH);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get FLASH operations and data */
    flash_ops = (const mr_flash_ops_t *)MR_DRIVER_OPS(drv);
    flash_data = (mr_flash_data_t *)MR_DRIVER_DATA(drv);

    /* Check whether operation is supported */
    if (!flash_ops->erase) {
        return -MR_ENOSYS;
    }

    /* Check range */
    if ((offset > flash_data->info.size)
        || (size > (flash_data->info.size - offset))) {
        return -MR_EINVAL;
    }

    /* Erase FLASH data */
    return flash_ops->erase(drv, offset, size);
}

/**
 * @brief This function gets a FLASH driver.
 *
 * @param drv The FLASH driver.
 * @return The FLASH driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_flash_get(mr_driver_t *drv) {
    /* Get driver reference */
    return mr_driver_get(drv);
}

/**
 * @brief This function puts a FLASH driver.
 *
 * @param drv The FLASH driver.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_flash_put(mr_driver_t *drv) {
    /* Put driver reference */
    return mr_driver_put(drv);
}

/* ================================ HardTree ================================ */

/**
 * @brief This macro function installs a FLASH instance through hardtree.
 *
 * @param _id The instance id.
 * @param _priv The FLASH private data.
 * @param _ops The FLASH operations.
 */
#define MR_HT_FLASH_INSTALL(_id, _priv, _ops)                                  \
    static mr_flash_data_t MR_CAT(__mr_flash_data_, _id) = {0};                \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(flash),                 \
                         MR_DRIVER_TYPE_FLASH, &MR_CAT(__mr_flash_data_, _id), \
                         _priv, _ops)
#endif /* defined(MR_USE_DRIVER_FLASH) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_FLASH_H__ */
