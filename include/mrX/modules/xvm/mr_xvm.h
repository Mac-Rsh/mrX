/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_MODULE_XVM_H__
#define __MR_MODULE_XVM_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_XVM)
#include <mrX/kernel/mr_initcall.h>
#include <mrX/kernel/mr_object.h>
#endif /* defined(MR_USE_MODULE_XVM) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup XVM
 * @{
 */

#if defined(MR_USE_MODULE_XVM)
struct mr_xvm_isa;

/* XVM flag definition */
#define MR_XVM_FLAG_LOCK                (1U << 0)
#define MR_XVM_FLAG_DYING               (1U << 1)

/* XVM status definition */
typedef enum mr_xvm_status {
    MR_XVM_STATUS_INIT,
    MR_XVM_STATUS_PAUSED,
    MR_XVM_STATUS_RUNNING,
    MR_XVM_STATUS_EXCEPT,
    MR_XVM_STATUS_ENDED,
} mr_xvm_status_t;

/* XVM return type */
typedef enum mr_xvm_return {
    MR_XVM_NONE,
    MR_XVM_SYSCALL,
    MR_XVM_EXCEPT,
    MR_XVM_EXIT,
} mr_xvm_return_t;

/* XVM none type */
typedef struct mr_xvm_none {
    mr_uint32_t step;
} mr_xvm_none_t;

/* XVM syscall type */
typedef struct mr_xvm_syscall {
    mr_uint32_t nr;
#if !defined(MR_CFG_XVM_SYSCALL_PARAM_NUM)
#define MR_CFG_XVM_SYSCALL_PARAM_NUM    (7)
#endif /* !defined(MR_CFG_XVM_SYSCALL_PARAM_NUM) */
    mr_uint64_t *param[MR_CFG_XVM_SYSCALL_PARAM_NUM];
    mr_uint64_t *ret;
} mr_xvm_syscall_t;

/* XVM except number definition */
typedef enum mr_xvm_except_nr {
    MR_XVM_EXCEPT_NR_BREAK = 1,
    MR_XVM_EXCEPT_NR_OVERFLOW,
    MR_XVM_EXCEPT_NR_ILLEGAL,
    MR_XVM_EXCEPT_NR_ACCESS,
    MR_XVM_EXCEPT_NR_ALIGN,
    MR_XVM_EXCEPT_NR_DIV0,
} mr_xvm_except_nr_t;

/* XVM except type */
typedef struct mr_xvm_except {
    mr_uint32_t nr;
} mr_xvm_except_t;

/* XVM event type */
typedef union mr_xvm_event {
    mr_xvm_none_t none;
    mr_xvm_syscall_t syscall;
    mr_xvm_except_t except;
} mr_xvm_event_t;

/* XVM data memory type */
typedef struct mr_xvm_datamem {
    mr_uint8_t *data;
    mr_size_t size;
} mr_xvm_datamem_t;

/* XVM binary type */
typedef struct mr_xvm_binary {
    const mr_uint8_t *image;
    mr_size_t size;
    mr_uint64_t entry;
} mr_xvm_binary_t;

/* XVM type */
MR_CLAZZES_EXPORT(xvm);
typedef struct mr_xvm {
    mr_object_t parent;
    struct mr_xvm_isa *isa;
    mr_xvm_datamem_t datamem;
    mr_xvm_binary_t bin;
    mr_atomic_t status;
    mr_atomic_t flags;
} mr_xvm_t;

/* XVM operations type */
typedef struct mr_xvm_ops {
    mr_err_t (*load)(mr_xvm_t *xvm);
    mr_xvm_return_t (*execute)(mr_xvm_t *xvm, mr_xvm_event_t *event,
                               mr_uint32_t step);
    mr_ssize_t (*mem_map)(mr_xvm_t *xvm, mr_uint64_t addr, void **memory,
                          mr_bool_t *writable);
    mr_err_t (*reg_load)(mr_xvm_t *xvm, const char *name, mr_uint64_t *val);
    mr_err_t (*reg_store)(mr_xvm_t *xvm, const char *name, mr_uint64_t val);
    mr_err_t (*interrupt)(mr_xvm_t *xvm, mr_uint32_t irq, mr_bool_t active);
} mr_xvm_ops_t;

/* XVM instruction set architecture type */
MR_CLAZZES_EXPORT(xvm_isa);
typedef struct mr_xvm_isa {
    mr_object_t parent;
    const char *name;
    struct mr_xvm_isa *next;
    const mr_xvm_ops_t *ops;
} mr_xvm_isa_t;

/**
 * @brief This function initializes a static XVM.
 *
 * @param xvm The XVM.
 * @param isa_name The XVM ISA name.
 * @param data The XVM RAM.
 * @param data_size The XVM RAM size.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xvm_init(mr_xvm_t *xvm, const char *isa_name, void *data,
                     mr_size_t data_size);

/**
 * @brief This function creates an XVM.
 *
 * @param isa_name The XVM ISA name.
 * @param data_size The XVM RAM size.
 * @return The XVM on success, MR_NULL on failure.
 */
mr_xvm_t *mr_xvm_create(const char *isa_name, mr_size_t data_size);

/**
 * @brief This macro function gets the data memory of an XVM.
 *
 * @param _xvm The XVM.
 * @return The XVM data memory.
 */
#define MR_XVM_DATAMEM(_xvm)            (&((mr_xvm_t *)(_xvm))->datamem)

/**
 * @brief This function deletes an XVM object.
 *
 * @param xvm The XVM.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xvm_del(mr_xvm_t *xvm);

/**
 * @brief This function loads a binary image into an XVM.
 *
 * @param xvm The XVM.
 * @param image The XVM binary image(flash).
 * @param image_size The XVM binary image size.
 * @param entry The XVM entry address(must be outside the image region,
 * i.e. entry >= image_size, since flash and RAM do not overlap).
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xvm_load(mr_xvm_t *xvm, const void *image, mr_size_t image_size,
                     mr_uint64_t entry);

/**
 * @brief This macro function gets the binary image of an XVM.
 *
 * @param _xvm The XVM.
 * @return The XVM binary image.
 */
#define MR_XVM_BIN(_xvm)                (&((mr_xvm_t *)(_xvm))->bin)

/**
 * @brief This function executes an XVM for up to a number of instructions.
 *
 * @param xvm The XVM.
 * @param event The XVM event, filled in on return to describe why execution
 * stopped(none, syscall, or exception).
 * @param step The maximum number of instructions to execute in this call.
 * @return The XVM return type that stopped execution(MR_XVM_NONE,
 * MR_XVM_SYSCALL, MR_XVM_EXCEPT, or MR_XVM_EXIT).
 */
mr_xvm_return_t mr_xvm_execute(mr_xvm_t *xvm, mr_xvm_event_t *event,
                               mr_uint32_t step);

/**
 * @brief This function reads data memory from an XVM.
 *
 * @param xvm The XVM.
 * @param addr The data memory address.
 * @param datamem The buffer to hold the data memory read from the XVM.
 * @param size The size of the data memory to read, in bytes.
 * @return The number of bytes read on success, a negative error code on
 * failure.
 */
mr_ssize_t mr_xvm_datamem_load(mr_xvm_t *xvm, mr_uint64_t addr, void *datamem,
                               mr_size_t size);

/**
 * @brief This function writes data memory to an XVM.
 *
 * @param xvm The XVM.
 * @param addr The data memory address.
 * @param datamem The buffer holding the data memory to write to the XVM.
 * @param size The size of the data memory to write, in bytes.
 * @return The number of bytes written on success, a negative error code on
 * failure (e.g. -MR_EPERM if the region is read-only).
 */
mr_ssize_t mr_xvm_datamem_store(mr_xvm_t *xvm, mr_uint64_t addr,
                                const void *datamem, mr_size_t size);

/**
 * @brief This function loads a register of an XVM by name.
 *
 * @param xvm The XVM.
 * @param name The register name.
 * @param val The buffer to hold the register value.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_xvm_reg_load(mr_xvm_t *xvm, const char *name,
                                   mr_uint64_t *val) {
    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));
    MR_ASSERT(xvm->isa->ops->reg_load != MR_NULL);
    MR_ASSERT((name != MR_NULL) && (name[0] != '\0'));
    MR_ASSERT(val != MR_NULL);

    /* Load register to user */
    return xvm->isa->ops->reg_load(xvm, name, val);
}

/**
 * @brief This function stores a register of an XVM by name.
 *
 * @param xvm The XVM.
 * @param name The register name.
 * @param val The register value to store.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_xvm_reg_store(mr_xvm_t *xvm, const char *name,
                                    mr_uint64_t val) {
    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));
    MR_ASSERT(xvm->isa->ops->reg_store != MR_NULL);
    MR_ASSERT((name != MR_NULL) && (name[0] != '\0'));

    /* Store register from user */
    return xvm->isa->ops->reg_store(xvm, name, val);
}

/**
 * @brief This function sends an interrupt to an XVM.
 *
 * @param xvm The XVM.
 * @param irq The interrupt number.
 * @param active MR_TRUE to assert the interrupt, MR_FALSE to deassert.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_xvm_interrupt(mr_xvm_t *xvm, mr_uint32_t irq,
                                    mr_bool_t active) {
    mr_atomic_t status;

    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));

    /* Get current status */
    status = mr_atomic_load(&xvm->status);

    /* Check if XVM is in INIT or ENDED status */
    if ((status == MR_XVM_STATUS_INIT) || (status == MR_XVM_STATUS_ENDED)) {
        return -MR_EPERM;
    }

    /* Check if interrupt is supported */
    if (!xvm->isa->ops->interrupt) {
        return -MR_ENOSYS;
    }

    /* Send interrupt to XVM */
    return xvm->isa->ops->interrupt(xvm, irq, active);
}

/**
 * @brief This function pauses a XVM.
 *
 * @param xvm The XVM.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_xvm_pause(mr_xvm_t *xvm) {
    mr_atomic_t status;

    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));

    /* Pause XVM */
    mr_atomic_init(&status, MR_XVM_STATUS_RUNNING);
    mr_atomic_compare_exchange(&xvm->status, &status, MR_XVM_STATUS_PAUSED);
    return 0;
}

/**
 * @brief This function resumes a XVM.
 *
 * @param xvm The XVM.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Can resume from PAUSED or EXCEPT status (clears except).
 */
MR_INLINE mr_err_t mr_xvm_resume(mr_xvm_t *xvm) {
    mr_atomic_t status;

    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));

    /* Get current status */
    status = mr_atomic_load(&xvm->status);

    /* Check if XVM is in PAUSED or ERROR status */
    if ((status != MR_XVM_STATUS_PAUSED) && (status != MR_XVM_STATUS_EXCEPT)) {
        return -MR_EPERM;
    }

    /* Resume XVM */
    mr_atomic_compare_exchange(&xvm->status, &status, MR_XVM_STATUS_RUNNING);
    return 0;
}

/**
 * @brief This function gets a XVM status.
 *
 * @param xvm The XVM.
 * @return The XVM status.
 */
MR_INLINE mr_xvm_status_t mr_xvm_status(mr_xvm_t *xvm) {
    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));

    /* Get XVM status */
    return (mr_xvm_status_t)mr_atomic_load(&xvm->status);
}

/**
 * @brief This function gets an XVM object.
 *
 * @param xvm The XVM.
 * @return The XVM on success, MR_NULL on failure.
 */
MR_INLINE mr_xvm_t *mr_xvm_get(mr_xvm_t *xvm) {
    /* Get XVM reference */
    return (mr_xvm_t *)mr_object_get((mr_object_t *)xvm);
}

/**
 * @brief This function puts an XVM object.
 *
 * @param xvm The XVM.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_xvm_put(mr_xvm_t *xvm) {
    /* Put XVM reference */
    return mr_object_put((mr_object_t *)xvm);
}

/**
 * @brief This macro function initializes a static XVM ISA.
 *
 * @param _name The XVM ISA name.
 * @param _ops The XVM ISA operations.
 */
#define MR_XVM_ISA_INIT(_name, _ops)                                           \
    {                                                                          \
        .parent = MR_OBJECT_INIT(MR_CLAZZES_STATIK(xvm_isa)),                  \
        .name = (_name),                                                       \
        .next = MR_NULL,                                                       \
        .ops = (_ops),                                                         \
    }

/**
 * @brief This function initializes a static XVM ISA.
 *
 * @param isa The XVM ISA.
 * @param name The XVM ISA name.
 * @param ops The XVM ISA operations.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xvm_isa_init(mr_xvm_isa_t *isa, const char *name,
                         const mr_xvm_ops_t *ops);

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

#endif /* __MR_MODULE_XVM_H__ */
