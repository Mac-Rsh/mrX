/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_VMC_H__
#define __MR_VMC_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_VMC)
#include <mrX/kernel/mr_object.h>
#endif /* defined(MR_USE_VMC) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup VMC
 * @{
 */

#if defined(MR_USE_VMC)
struct mr_vmc;

/* VMC flag definition */
#define MR_VMC_FLAG_LOCK                (1U << 1)
#define MR_VMC_FLAG_DYING               (1U << 2)
#define MR_VMC_FLAG_PAUSED              (1U << 3)

/* VMC status definition */
typedef enum mr_vmc_status {
    MR_VMC_STATE_INIT,
    MR_VMC_STATE_PAUSED,
    MR_VMC_STATE_RUNNING,
    MR_VMC_STATE_ERROR,
    MR_VMC_STATE_ENDED,
} mr_vmc_state_t;

/* VMC property type */
typedef struct mr_vmc_prop {
    void *memory;
    mr_uint32_t memory_size;
    const void *image;
    mr_uint32_t image_size;
    mr_uint32_t storage_addr;
    mr_uint32_t entry_addr;
} mr_vmc_prop_t;

/* VMC return type definition */
typedef enum mr_vmc_return {
    MR_VMC_NONE,
    MR_VMC_SYSCALL,
    MR_VMC_EXCEPT,
    MR_VMC_END,
} mr_vmc_return_t;

/* VMC none type */
typedef struct mr_vmc_none {
    mr_uint32_t exec_step;
} mr_vmc_none_t;

/* VMC syscall type */
typedef struct mr_vmc_syscall {
    mr_uint32_t nr;
} mr_vmc_syscall_t;

/* VMC exception number definition */
typedef enum mr_vmc_except_nr {
    MR_VMC_EXCEPT_NR_BREAK = 1,
    MR_VMC_EXCEPT_NR_OVERFLOW,
    MR_VMC_EXCEPT_NR_ILLEGAL,
    MR_VMC_EXCEPT_NR_ACCESS,
    MR_VMC_EXCEPT_NR_ALIGN,
    MR_VMC_EXCEPT_NR_DIV0,
} mr_vmc_except_nr_t;

/* VMC exception type */
typedef struct mr_vmc_except {
    mr_uint32_t nr;
} mr_vmc_except_t;

/* VMC event type */
typedef union mr_vmc_event {
    mr_vmc_none_t none;
    mr_vmc_syscall_t syscall;
    mr_vmc_except_t except;
} mr_vmc_event_t;

/* VMC method type */
typedef mr_err_t(mr_vmc_load_t)(struct mr_vmc *vmc, mr_vmc_prop_t *prop);
typedef mr_vmc_return_t(mr_vmc_exec_t)(struct mr_vmc *vmc, mr_vmc_prop_t *prop,
                                       mr_vmc_event_t *event,
                                       mr_uint32_t instr_step);
typedef mr_err_t(mr_vmc_trace_t)(struct mr_vmc *vmc, mr_vmc_prop_t *prop,
                                 void **reg_addr, mr_uint32_t *reg_size,
                                 void **memory_addr, mr_uint32_t *memory_size);

/* VMC clazz type */
typedef struct mr_vmc_clazz {
    const char *name;
    mr_vmc_load_t *load;
    mr_vmc_exec_t *exec;
    mr_vmc_trace_t *trace;
} mr_vmc_clazz_t;

/* VMC type */
MR_CLAZZES_EXPORT(vmc);
typedef struct mr_vmc {
    mr_object_t parent;
    const mr_vmc_clazz_t *clazz;
    mr_vmc_prop_t prop;
    mr_atomic_t flags;
    mr_uint32_t state;
} mr_vmc_t;

/**
 * @brief This macro function initializes VMC clazz.
 *
 * @param _name The VMC clazz name.
 * @param _load The load function.
 * @param _exec The execute function.
 * @param _trace The trace function.
 */
#define MR_VMC_CLAZZ_INIT(_name, _load, _exec, _trace)                         \
    {.name = (_name), .load = (_load), .exec = (_exec), .trace = (_trace)}

/**
 * @brief This function initializes VMC clazz.
 *
 * @param vclazz The VMC clazz.
 * @param name The VMC clazz name.
 * @param load The load function.
 * @param exec The execute function.
 * @param trace The trace function.
 */
MR_INLINE void mr_vmc_clazz_init(mr_vmc_clazz_t *vclazz, const char *name,
                                 mr_vmc_load_t *load, mr_vmc_exec_t *exec,
                                 mr_vmc_trace_t *trace) {
    /* Check parameter */
    if (!vclazz) {
        return;
    }

    /* Init vmc clazz */
    vclazz->name = name;
    vclazz->load = load;
    vclazz->exec = exec;
    vclazz->trace = trace;
}

/**
 * @brief This macro function initializes VMC clazz.
 *
 * @param _name The VMC clazz name.
 * @param _load The load function.
 * @param _exec The execute function.
 * @param _trace The trace function.
 */
#define MR_VMC_CLAZZ_DEFINE(_name, _load, _exec, _trace)                       \
    const mr_vmc_clazz_t MR_CAT(__mr_vmc_clazz_, _name)                        \
        = MR_VMC_CLAZZ_INIT(MR_STR(_name), (_load), (_exec), (_trace))

/**
 * @brief This macro function exports VMC clazz.
 *
 * @param _name The VMC clazz name.
 */
#define MR_VMC_CLAZZ_EXPORT(_name)                                             \
    extern const mr_vmc_clazz_t MR_CAT(__mr_vmc_clazz_, _name)

/**
 * @brief This macro function imports VMC clazz.
 *
 * @param _name The VMC clazz name.
 */
#define MR_VMC_CLAZZ_IMPORT(_name)      MR_VMC_CLAZZ_EXPORT(_name)

/**
 * @brief This macro function finds VMC clazz.
 *
 * @param _name The VMC clazz name.
 * @return The VMC clazz.
 */
#define MR_VMC_CLAZZ_FIND(_name)        (&MR_CAT(__mr_vmc_clazz_, _name))

/**
 * @brief This function initializes a VMC.
 *
 * @param vmc The VMC.
 * @param vclazz The VMC clazz.
 * @param memory The VMC memory.
 * @param memory_size The VMC memory size.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_vmc_init(mr_vmc_t *vmc, const mr_vmc_clazz_t *vclazz, void *memory,
                     mr_uint32_t memory_size);

/**
 * @brief This function creates a VMC.
 *
 * @param vclazz The VMC clazz.
 * @param memory_size The memory size.
 * @return The VMC on success, MR_NULL on failure.
 */
mr_vmc_t *mr_vmc_create(const mr_vmc_clazz_t *vclazz, mr_uint32_t memory_size);

/**
 * @brief This function deletes a VMC.
 *
 * @param vmc The VMC.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_vmc_del(mr_vmc_t *vmc);

/**
 * @brief This function loads a program image into a VMC.
 *
 * @param vmc The VMC.
 * @param image The program image.
 * @param image_size The image size.
 * @param storage_addr The storage address.
 * @param entry_addr The program entry address.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_vmc_load(mr_vmc_t *vmc, const void *image, mr_uint32_t image_size,
                     mr_uint32_t storage_addr, mr_uint32_t entry_addr);

/**
 * @brief This function executes a VMC.
 *
 * @param vmc The VMC.
 * @param event The VMC event.
 * @param instr_step The instruction step.
 * @return The VMC return(describe the corresponding event).
 */
mr_vmc_return_t mr_vmc_execute(mr_vmc_t *vmc, mr_vmc_event_t *event,
                               mr_uint32_t instr_step);

/**
 * @brief This function pauses a VMC.
 *
 * @param vmc The VMC.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Request only, actual pause happens after execute completes, May be
 * overridden by subsequent state changes.
 */
mr_err_t mr_vmc_pause(mr_vmc_t *vmc);

/**
 * @brief This function resumes a VMC.
 *
 * @param vmc The VMC.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Can resume from PAUSED or ERROR state (clears error).
 */
mr_err_t mr_vmc_resume(mr_vmc_t *vmc);

/**
 * @brief This function gets a VMC state.
 *
 * @param vmc The VMC.
 * @return The VMC state.
 */
MR_INLINE mr_vmc_state_t mr_vmc_state(mr_vmc_t *vmc) {
    /* Check parameter */
    MR_ASSERT((vmc != MR_NULL) && MR_OBJECT_IS_INITED(vmc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(vmc, vmc));

    /* Get VMC state */
    return vmc->state;
}

/**
 * @brief This function traces a VMC.
 *
 * @param vmc The VMC.
 * @param reg_addr The register address.
 * @param reg_size The register size.
 * @param memory_addr The memory address.
 * @param memory_size The memory size.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_vmc_trace(mr_vmc_t *vmc, void **reg_addr, mr_uint32_t *reg_size,
                      void **memory_addr, mr_uint32_t *memory_size);

/**
 * @brief This function get a VMC.
 *
 * @param vmc The VMC.
 * @return The VMC on success, MR_NULL on failure.
 */
MR_INLINE mr_vmc_t *mr_vmc_get(mr_vmc_t *vmc) {
    /* Get VMC reference */
    return (mr_vmc_t *)mr_object_get((mr_object_t *)vmc);
}

/**
 * @brief This function put a VMC.
 *
 * @param vmc The VMC.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_vmc_put(mr_vmc_t *vmc) {
    /* Put VMC reference */
    return mr_object_put((mr_object_t *)vmc);
}
#endif /* defined(MR_USE_VMC) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_VMC_H__ */
