/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_DRIVER_RAM_H__
#define __MR_DRIVER_RAM_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_DRIVER_RAM)
#include <mrX/drivers/mr_driver.h>
#endif /* defined(MR_USE_DRIVER_RAM) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup RAM
 * @{
 */

#if defined(MR_USE_DRIVER_RAM)
/* RAM information type */
typedef struct mr_ram_info {
    mr_uint64_t size;
    mr_uint32_t regions_num;
} mr_ram_info_t;

/* RAM region type */
typedef struct mr_ram_region {
    void *address;
    mr_size_t size;
} mr_ram_region_t;

/* RAM data type */
MR_DRIVER_CLAZZES_EXPORT(ram);
typedef struct mr_ram_data {
    mr_ram_info_t info;
} mr_ram_data_t;

/* RAM operations type */
typedef struct mr_ram_ops {
    mr_err_t (*probe)(const mr_driver_t *drv, mr_ram_info_t *info);
    mr_err_t (*region)(const mr_driver_t *drv, mr_uint32_t index,
                       mr_ram_region_t *region);
    mr_ssize_t (*read)(const mr_driver_t *drv, mr_uint64_t offset,
                       mr_uint8_t *buf, mr_size_t size);
    mr_ssize_t (*write)(const mr_driver_t *drv, mr_uint64_t offset,
                        const mr_uint8_t *buf, mr_size_t size);
} mr_ram_ops_t;

/**
 * @brief This macro function defines a RAM driver.
 *
 * @param _name The RAM driver name.
 * @param _data The RAM driver data.
 * @param _priv The RAM private data.
 * @param _ops The RAM operations.
 */
#define MR_RAM_DEFINE(_name, _data, _priv, _ops)                               \
    MR_DRIVER_DEFINE(_name, MR_DRIVER_CLAZZES_STATIK(ram), MR_DRIVER_TYPE_RAM, \
                     _data, _priv, _ops)

/**
 * @brief This macro function installs a RAM driver.
 *
 * @param _name The RAM driver name.
 * @param _data The RAM driver data.
 * @param _priv The RAM private data.
 * @param _ops The RAM operations.
 * @param _level The init level.
 */
#define MR_RAM_INSTALL(_name, _data, _priv, _ops, _level)                      \
    MR_DRIVER_INSTALL(_name, MR_DRIVER_CLAZZES_STATIK(ram),                    \
                      MR_DRIVER_TYPE_RAM, _data, _priv, _ops, _level)

/**
 * @brief This function initializes a RAM driver.
 *
 * @param drv The RAM driver.
 * @param data The RAM driver data.
 * @param priv The RAM private data.
 * @param ops The RAM operations.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ram_init(mr_driver_t *drv, mr_ram_data_t *data,
                               void *priv, mr_ram_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(ram);

    /* Check parameter */
    MR_ASSERT(drv != MR_NULL);
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Init RAM driver */
    return mr_driver_init(drv, dclazz, MR_DRIVER_TYPE_RAM, data, priv, ops);
}

/**
 * @brief This function creates a RAM driver.
 *
 * @param data The RAM driver data.
 * @param priv The RAM private data.
 * @param ops The RAM operations.
 * @return The RAM driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_ram_create(mr_ram_data_t *data, void *priv,
                                     mr_ram_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_DYNAMIC(ram);

    /* Check parameter */
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Create RAM driver */
    return mr_driver_create(dclazz, MR_DRIVER_TYPE_RAM, data, priv, ops);
}

/**
 * @brief This function deletes a RAM driver.
 *
 * @param drv The RAM driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ram_del(mr_driver_t *drv) {
    /* Check parameter */
    MR_DRIVER_ASSERT_TYPE(drv, MR_DRIVER_TYPE_RAM);

    /* Delete driver */
    return mr_driver_del(drv);
}

/**
 * @brief This function gets RAM information discovered during setup.
 *
 * @param drv The RAM driver.
 * @param info The RAM information output.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ram_info(mr_driver_t *drv, mr_ram_info_t *info) {
    mr_ram_data_t *ram_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_RAM);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(info != MR_NULL);

    /* Get RAM data */
    ram_data = (mr_ram_data_t *)MR_DRIVER_DATA(drv);

    /* Get RAM information */
    *info = ram_data->info;
    return 0;
}

/**
 * @brief This function gets a directly addressable RAM region.
 *
 * @param drv The RAM driver.
 * @param index The region index.
 * @param region The region information output.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ram_region(mr_driver_t *drv, mr_uint32_t index,
                                 mr_ram_region_t *region) {
    const mr_ram_ops_t *rm_ops;
    mr_ram_data_t *ram_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_RAM);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(region != MR_NULL);

    /* Get RAM operations and data */
    rm_ops = (const mr_ram_ops_t *)MR_DRIVER_OPS(drv);
    ram_data = (mr_ram_data_t *)MR_DRIVER_DATA(drv);

    /* Check whether operation is supported */
    if (!rm_ops->region) {
        return -MR_ENOSYS;
    }

    /* Check region index */
    if (index >= ram_data->info.regions_num) {
        return -MR_EINVAL;
    }

    /* Get RAM region */
    return rm_ops->region(drv, index, region);
}

/**
 * @brief This function reads RAM data.
 *
 * @param drv The RAM driver.
 * @param offset The offset address.
 * @param buf The buffer.
 * @param size The size.
 * @return The actual size of read data on success, a negative error code on
 * failure.
 */
MR_INLINE mr_ssize_t mr_ram_read(mr_driver_t *drv, mr_uint64_t offset,
                                 void *buf, mr_size_t size) {
    const mr_ram_ops_t *rm_ops;
    mr_ram_data_t *ram_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_RAM);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0U));

    /* Get RAM operations and data */
    rm_ops = (const mr_ram_ops_t *)MR_DRIVER_OPS(drv);
    ram_data = (mr_ram_data_t *)MR_DRIVER_DATA(drv);

    /* Check whether operation is supported */
    if (!rm_ops->read) {
        return -MR_ENOSYS;
    }

    /* Check range */
    if ((offset > ram_data->info.size)
        || ((mr_uint64_t)size > (ram_data->info.size - offset))) {
        return -MR_EINVAL;
    }

    /* Read RAM data */
    return rm_ops->read(drv, offset, buf, size);
}

/**
 * @brief This function writes RAM data.
 *
 * @param drv The RAM driver.
 * @param offset The offset address.
 * @param buf The buffer.
 * @param size The size.
 * @return The actual size of written data on success, a negative error code on
 * failure.
 */
MR_INLINE mr_ssize_t mr_ram_write(mr_driver_t *drv, mr_uint64_t offset,
                                  const void *buf, mr_size_t size) {
    const mr_ram_ops_t *rm_ops;
    mr_ram_data_t *ram_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_RAM);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0U));

    /* Get RAM operations and data */
    rm_ops = (const mr_ram_ops_t *)MR_DRIVER_OPS(drv);
    ram_data = (mr_ram_data_t *)MR_DRIVER_DATA(drv);

    /* Check whether operation is supported */
    if (!rm_ops->write) {
        return -MR_ENOSYS;
    }

    /* Check range */
    if ((offset > ram_data->info.size)
        || ((mr_uint64_t)size > (ram_data->info.size - offset))) {
        return -MR_EINVAL;
    }

    /* Write RAM data */
    return rm_ops->write(drv, offset, buf, size);
}

/**
 * @brief This function gets a RAM driver.
 *
 * @param drv The RAM driver.
 * @return The RAM driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_ram_get(mr_driver_t *drv) {
    /* Get driver reference */
    return mr_driver_get(drv);
}

/**
 * @brief This function puts a RAM driver.
 *
 * @param drv The RAM driver.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_ram_put(mr_driver_t *drv) {
    /* Put driver reference */
    return mr_driver_put(drv);
}

/* ================================ HardTree ================================ */

/**
 * @brief This macro function installs a RAM instance through hardtree.
 *
 * @param _id The instance id.
 * @param _priv The RAM private data.
 * @param _ops The RAM operations.
 */
#define MR_HT_RAM_INSTALL(_id, _priv, _ops)                                    \
    static mr_ram_data_t MR_CAT(__mr_ram_data_, _id) = {0};                    \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(ram),                   \
                         MR_DRIVER_TYPE_RAM, &MR_CAT(__mr_ram_data_, _id),     \
                         _priv, _ops)
#endif /* defined(MR_USE_DRIVER_RAM) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_RAM_H__ */
