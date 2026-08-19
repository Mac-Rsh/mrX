/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_MODULE_XVM_ISA_H__
#define __MR_MODULE_XVM_ISA_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_XVM)
#include <mrX/kernel/mr_initcall.h>
#include <mrX/kernel/mr_object.h>
#endif /* defined(MR_USE_MODULE_XVM) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup XVM-ISA
 * @{
 */

#if defined(MR_USE_MODULE_XVM)
union mr_xvm_event;
enum mr_xvm_return;
struct mr_xvm;

/* XVM reg type definition */
typedef enum mr_xvm_isa_reg_type {
    MR_XVM_ISA_REG_INT,
    MR_XVM_ISA_REG_CODE,
    MR_XVM_ISA_REG_DATA,
} mr_xvm_isa_reg_type_t;

/* XVM reg-descriptor type */
typedef struct mr_xvm_isa_regdesc {
    mr_xvm_isa_reg_type_t type;
    mr_uint16_t bit_size;
    const char *name;
} mr_xvm_isa_regdesc_t;

/* XVM target type */
typedef struct mr_xvm_isa_target {
    const char *feature;
    const char *arch;
    mr_xvm_isa_regdesc_t *regs;
    mr_uint32_t regs_num;
} mr_xvm_isa_target_t;

/* XVM operations type */
typedef struct mr_xvm_isa_ops {
    mr_err_t (*load)(struct mr_xvm *xvm);
    enum mr_xvm_return (*execute)(struct mr_xvm *xvm, union mr_xvm_event *event,
                                  mr_uint32_t step);
    mr_ssize_t (*mem_map)(struct mr_xvm *xvm, mr_uint64_t addr, void **memory,
                          mr_bool_t *writable);
    mr_err_t (*reg_load)(struct mr_xvm *xvm, mr_uint32_t nr, mr_uint64_t *val);
    mr_err_t (*reg_store)(struct mr_xvm *xvm, mr_uint32_t nr, mr_uint64_t val);
    mr_err_t (*interrupt)(struct mr_xvm *xvm, mr_uint32_t irq,
                          mr_bool_t active);
} mr_xvm_isa_ops_t;

/* XVM instruction set architecture type */
MR_CLAZZES_EXPORT(xvm_isa);
typedef struct mr_xvm_isa {
    mr_object_t parent;
    const char *name;
    struct mr_xvm_isa *next;
    const mr_xvm_isa_ops_t *ops;
    mr_xvm_isa_target_t *target;
} mr_xvm_isa_t;

/**
 * @brief This macro function initializes a static XVM ISA.
 *
 * @param _name The XVM ISA name.
 * @param _ops The XVM ISA operations.
 * @param _target The ISA target description, or MR_NULL.
 */
#define MR_XVM_ISA_INIT(_name, _ops, _target)                                  \
    {                                                                          \
        .parent = MR_OBJECT_INIT(MR_CLAZZES_STATIK(xvm_isa)),                  \
        .name = (_name),                                                       \
        .next = MR_NULL,                                                       \
        .ops = (_ops),                                                         \
        .target = (_target),                                                   \
    }

/**
 * @brief This function initializes a static XVM ISA.
 *
 * @param isa The XVM ISA.
 * @param name The XVM ISA name.
 * @param ops The XVM ISA operations.
 * @param target The ISA target description, or MR_NULL.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xvm_isa_init(mr_xvm_isa_t *isa, const char *name,
                         const mr_xvm_isa_ops_t *ops,
                         mr_xvm_isa_target_t *target);

/**
 * @brief This function deletes an XVM ISA object.
 *
 * @param isa The XVM ISA.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xvm_isa_del(mr_xvm_isa_t *isa);

/**
 * @brief This function registers an XVM ISA.
 *
 * @param isa The XVM ISA.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xvm_isa_register(mr_xvm_isa_t *isa);

/**
 * @brief This function unregisters an XVM ISA.
 *
 * @param isa The XVM ISA.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xvm_isa_unregister(mr_xvm_isa_t *isa);

/**
 * @brief This function gets the target description of an XVM ISA.
 *
 * @param isa The XVM ISA.
 * @return The target, or MR_NULL if none.
 */
MR_INLINE mr_xvm_isa_target_t *mr_xvm_isa_target(mr_xvm_isa_t *isa) {
    /* Check parameter */
    MR_ASSERT((isa != MR_NULL) && MR_OBJECT_IS_INITED(isa));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(isa, xvm_isa));

    /* Get target */
    return isa->target;
}

/**
 * @brief This function finds a registered XVM ISA by name.
 *
 * @param name The XVM ISA name.
 * @return The XVM ISA on success, MR_NULL on failure. The returned object holds
 * one reference; call mr_xvm_isa_put() when done.
 */
mr_xvm_isa_t *mr_xvm_isa_find(const char *name);

/**
 * @brief This function gets an XVM ISA object.
 *
 * @param isa The XVM ISA.
 * @return The XVM ISA on success, MR_NULL on failure.
 */
MR_INLINE mr_xvm_isa_t *mr_xvm_isa_get(mr_xvm_isa_t *isa) {
    /* Get XVM ISA reference */
    return (mr_xvm_isa_t *)mr_object_get((mr_object_t *)isa);
}

/**
 * @brief This function puts an XVM ISA object.
 *
 * @param isa The XVM ISA.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_xvm_isa_put(mr_xvm_isa_t *isa) {
    /* Put XVM ISA reference */
    return mr_object_put((mr_object_t *)isa);
}
#endif /* defined(MR_USE_MODULE_XVM) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_MODULE_XVM_ISA_H__ */
