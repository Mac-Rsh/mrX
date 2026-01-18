/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_VMCORE_H__
#define __MR_VMCORE_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_VMCORE)
#include <mrX/kernel/mr_object.h>
#endif /* defined(MR_USE_VMCORE) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup VMCore
 * @{
 */

#if defined(MR_USE_VMCORE)
struct mr_vmcore;

/* VMCore flag definition */
#define MR_VMCORE_FLAG_LOCK             (1U << 1)
#define MR_VMCORE_FLAG_DYING            (1U << 2)

/* VMCore status definition */
typedef enum mr_vmcore_status {
    MR_VMCORE_STATE_INIT,
    MR_VMCORE_STATE_PAUSED,
    MR_VMCORE_STATE_RUNNING,
    MR_VMCORE_STATE_ERROR,
    MR_VMCORE_STATE_ENDED,
} mr_vmcore_status_t;

/* VMCore operation type */
typedef mr_err_t(mr_vmcore_operation_t)(struct mr_vmcore *vmcore);
typedef mr_err_t(mr_vmcore_load_t)(struct mr_vmcore *vmcore);
typedef mr_err_t(mr_vmcore_execute_t)(struct mr_vmcore *vmcore,
                                      mr_uint32_t step);

/* VMCore clazz type */
typedef struct mr_vmcore_clazz {
    const char *name;
    mr_vmcore_operation_t *init;
    mr_vmcore_operation_t *deinit;
    mr_vmcore_load_t *load;
    mr_vmcore_execute_t *execute;
} mr_vmcore_clazz_t;

/* VMCore type */
MR_CLAZZES_EXPORT(vmcore);
typedef struct mr_vmcore {
    mr_object_t parent;
    const mr_vmcore_clazz_t *clazz;
    mr_ptr_t memory;
    mr_uint32_t memory_size;
    const void *image;
    mr_uint32_t image_size;
    mr_uint32_t load_addr;
    mr_atomic_t flags;
    mr_uint16_t state;
    mr_int16_t error;
} mr_vmcore_t;

/**
 * @brief This macro function initializes vmcore clazz.
 *
 * @param _name The vmcore clazz name.
 * @param _init The init function.
 * @param _deinit The deinit function.
 * @param _load The load function.
 * @param _execute The execute function.
 */
#define MR_VMCORE_CLAZZ_INIT(_name, _init, _deinit, _load, _execute)           \
    {.name = (_name),                                                          \
     .init = (_init),                                                          \
     .deinit = (_deinit),                                                      \
     .load = (_load),                                                          \
     .execute = (_execute)}

/**
 * @brief This function initializes vmcore clazz.
 *
 * @param vclazz The vmcore clazz.
 * @param name The vmcore clazz name.
 * @param init The init function.
 * @param deinit The deinit function.
 * @param load The load function.
 * @param execute The execute function.
 */
MR_INLINE void mr_vmcore_clazz_init(mr_vmcore_clazz_t *vclazz, const char *name,
                                    mr_vmcore_operation_t *init,
                                    mr_vmcore_operation_t *deinit,
                                    mr_vmcore_load_t *load,
                                    mr_vmcore_execute_t *execute) {
    /* Check parameter */
    if (!vclazz) {
        return;
    }

    /* Init vmcore clazz */
    vclazz->name = name;
    vclazz->init = init;
    vclazz->deinit = deinit;
    vclazz->load = load;
    vclazz->execute = execute;
}

/**
 * @brief This macro function initializes vmcore clazz.
 *
 * @param _name The vmcore clazz name.
 * @param _init The init function.
 * @param _deinit The deinit function.
 * @param _load The load function.
 * @param _execute The execute function.
 */
#define MR_VMCORE_CLAZZ_DEFINE(_name, _init, _deinit, _load, _execute)         \
    const mr_vmcore_clazz_t MR_CAT(__mr_vmcore_clazz_, _name)                  \
        = MR_VMCORE_CLAZZ_INIT(MR_STR(_name), (_init), (_deinit), (_load),     \
                               (_execute))

/**
 * @brief This macro function exports vmcore clazz.
 *
 * @param _name The vmcore clazz name.
 */
#define MR_VMCORE_CLAZZ_EXPORT(_name)                                          \
    extern const mr_vmcore_clazz_t MR_CAT(__mr_vmcore_clazz_, _name)

/**
 * @brief This macro function imports vmcore clazz.
 *
 * @param _name The vmcore clazz name.
 */
#define MR_VMCORE_CLAZZ_IMPORT(_name)   MR_VMCORE_CLAZZ_EXPORT(_name)

/**
 * @brief This macro function finds vmcore clazz.
 *
 * @param _name The vmcore clazz name.
 * @return The vmcore clazz.
 */
#define MR_VMCORE_CLAZZ_FIND(_name)     (&MR_CAT(__mr_vmcore_clazz_, _name))

/**
 * @brief This function initializes a vmcore.
 *
 * @param vmcore The vmcore.
 * @param vclazz The vmcore clazz.
 * @param memory The vmcore memory.
 * @param memory_size The vmcore memory size.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_vmcore_init(mr_vmcore_t *vmcore, const mr_vmcore_clazz_t *vclazz,
                        void *memory, mr_uint32_t memory_size);

/**
 * @brief This function creates a vmcore.
 *
 * @param vclazz The vmcore clazz.
 * @param memory_size The memory size.
 * @return The vmcore on success, MR_NULL on failure.
 */
mr_vmcore_t *mr_vmcore_create(const mr_vmcore_clazz_t *vclazz,
                              mr_uint32_t memory_size);

/**
 * @brief This macro function gets vmcore memory.
 *
 * @param _vmcore The vmcore.
 * @return The vmcore memory.
 */
#define MR_VMCORE_MEMORY(_vmcore)       (((mr_vmcore_t *)(_vmcore))->memory)

/**
 * @brief This macro function gets vmcore memory size.
 *
 * @param _vmcore The vmcore.
 * @return The vmcore memory size.
 */
#define MR_VMCORE_MEMORY_SIZE(_vmcore)                                         \
    (((mr_vmcore_t *)(_vmcore))->memory_size)

/**
 * @brief This macro function gets vmcore image.
 *
 * @param _vmcore The vmcore.
 * @return The vmcore image.
 */
#define MR_VMCORE_IMAGE(_vmcore)        (((mr_vmcore_t *)(_vmcore))->image)

/**
 * @brief This macro function gets vmcore image size.
 *
 * @param _vmcore The vmcore.
 * @return The vmcore image size.
 */
#define MR_VMCORE_IMAGE_SIZE(_vmcore)   (((mr_vmcore_t *)(_vmcore))->image_size)

/**
 * @brief This macro function gets vmcore load address.
 *
 * @param _vmcore The vmcore.
 * @return The vmcore load address.
 */
#define MR_VMCORE_LOAD_ADDR(_vmcore)    (((mr_vmcore_t *)(_vmcore))->load_addr)

/**
 * @brief This function deletes a vmcore.
 *
 * @param vmcore The vmcore.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_vmcore_del(mr_vmcore_t *vmcore);

/**
 * @brief This function loads a program image into a vmcore.
 *
 * @param vmcore The vmcore.
 * @param image The program image.
 * @param image_size The image size.
 * @param load_addr The load address.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_vmcore_load(mr_vmcore_t *vmcore, const void *image,
                        mr_uint32_t image_size, mr_uint32_t load_addr);

/**
 * @brief This function executes a vmcore.
 *
 * @param vmcore The vmcore.
 * @param step The execution step.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_vmcore_execute(mr_vmcore_t *vmcore, mr_uint32_t step);
#endif /* defined(MR_USE_VMCORE) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_VMCORE_H__ */
