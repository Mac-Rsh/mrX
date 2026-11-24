/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_MODULE_XVM_MMIO_H__
#define __MR_MODULE_XVM_MMIO_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_XVM_MMIO)
#include <mrX/modules/xvm/mr_xvm_isa.h>
#endif /* defined(MR_USE_MODULE_XVM_MMIO) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup XVM-MMIO
 * @{
 */

#if defined(MR_USE_MODULE_XVM_MMIO)
struct mr_xvm;

/* XVM MMIO operations type */
typedef struct mr_xvm_mmio_ops {
    mr_err_t (*load)(void *user, mr_uint64_t offset, mr_size_t size,
                     mr_uint64_t *val);
    mr_err_t (*store)(void *user, mr_uint64_t offset, mr_size_t size,
                      mr_uint64_t val);
} mr_xvm_mmio_ops_t;

/* XVM MMIO region type */
typedef struct mr_xvm_mmio_region {
    mr_uint64_t base;
    mr_uint64_t size;
    const mr_xvm_mmio_ops_t *ops;
    void *user;
} mr_xvm_mmio_region_t;

/* XVM MMIO table type */
typedef struct mr_xvm_mmio {
#if !defined(MR_CFG_XVM_MMIO_NUM)
#define MR_CFG_XVM_MMIO_NUM             (8)
#endif /* !defined(MR_CFG_XVM_MMIO_NUM) */
    mr_xvm_mmio_region_t regions[MR_CFG_XVM_MMIO_NUM];
    mr_size_t num;
} mr_xvm_mmio_t;

/**
 * @brief This function registers an MMIO region.
 *
 * @param xvm The XVM.
 * @param base The base address.
 * @param size The region size(bytes).
 * @param ops The MMIO operations.
 * @param user The user data.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xvm_mmio_register(struct mr_xvm *xvm, mr_uint64_t base,
                              mr_uint64_t size, const mr_xvm_mmio_ops_t *ops,
                              void *user);

/**
 * @brief This function unregisters an MMIO region.
 *
 * @param xvm The XVM.
 * @param base The base address.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xvm_mmio_unregister(struct mr_xvm *xvm, mr_uint64_t base);

/**
 * @brief This function loads data from an MMIO region.
 *
 * @param xvm The XVM.
 * @param addr The MMIO address.
 * @param size The access size(bytes).
 * @param val The data pointer.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xvm_mmio_load(struct mr_xvm *xvm, mr_uint64_t addr, mr_size_t size,
                          mr_uint64_t *val);

/**
 * @brief This function stores data to an MMIO region.
 *
 * @param xvm The XVM.
 * @param addr The MMIO address.
 * @param size The access size(bytes).
 * @param val The data.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xvm_mmio_store(struct mr_xvm *xvm, mr_uint64_t addr, mr_size_t size,
                           mr_uint64_t val);
#endif /* defined(MR_USE_MODULE_XVM_MMIO) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_MODULE_XVM_MMIO_H__ */
