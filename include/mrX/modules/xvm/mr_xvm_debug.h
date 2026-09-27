/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_MODULE_XVM_DEBUG_H__
#define __MR_MODULE_XVM_DEBUG_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_XVM_DEBUG)
#include <mrX/modules/xvm/mr_xvm_isa.h>
#endif /* defined(MR_USE_MODULE_XVM_DEBUG) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup XVM-Debug
 * @{
 */

#if defined(MR_USE_MODULE_XVM_DEBUG)
union mr_xvm_event;
enum mr_xvm_return;
struct mr_xvm;

/* XVM gate definition */
#define MR_XVM_DEBUG_GATE_OPEN          (0xFFFFFFFFU)
#define MR_XVM_DEBUG_GATE_STEP          (1U)
#define MR_XVM_DEBUG_GATE_HOLD          (0U)

/* XVM probe type */
typedef struct mr_xvm_debug_probe {
    mr_uint32_t step_gate;
    mr_ptr_t entry;
    mr_ptr_t priv;
} mr_xvm_debug_probe_t;

/* XVM breaktable type */
typedef struct mr_xvm_debug_breaktable {
#if !defined(MR_CFG_XVM_BREAKPOINT_NUM)
#define MR_CFG_XVM_BREAKPOINT_NUM       (16)
#endif /* !defined(MR_CFG_XVM_BREAKPOINT_NUM) */
    mr_uint64_t addr[MR_CFG_XVM_BREAKPOINT_NUM];
    mr_uint64_t skip_addr;
    mr_uint32_t num;
    mr_bool_t skip;
} mr_xvm_debug_breaktable_t;

/* XVM debug type */
typedef struct mr_xvm_debug {
    mr_xvm_debug_breaktable_t breaktable;
    mr_xvm_isa_target_t *target;
    mr_xvm_debug_probe_t probe;
} mr_xvm_debug_t;

/* Probe entry type */
typedef void(mr_xvm_debug_probe_entry_t)(void *priv,
                                         enum mr_xvm_return exec_ret,
                                         union mr_xvm_event *event);

/**
 * @brief This function initializes the debug unit of an XVM.
 *
 * @param xvm The XVM.
 *
 * @note For internal use. Called by XVM initialization.
 */
void mr_xvm_debug_init(struct mr_xvm *xvm);

/**
 * @brief This function gets the target description of an XVM.
 *
 * @param xvm The XVM.
 * @return The target, or MR_NULL if none.
 */
mr_xvm_isa_target_t *mr_xvm_debug_target(struct mr_xvm *xvm);

/**
 * @brief This function attaches a debug probe to an XVM.
 *
 * @param xvm The XVM.
 * @param entry The probe callback, stored in probe.entry.
 * @param priv The probe private pointer, stored in probe.priv.
 * @return 0 on success, -MR_EBUSY if a probe is already attached.
 *
 * @note Sets the step gate to 0 and keeps status RUNNING.
 */
mr_err_t mr_xvm_debug_probe_attach(struct mr_xvm *xvm,
                                   mr_xvm_debug_probe_entry_t *entry,
                                   void *priv);

/**
 * @brief This function detaches the debug probe from an XVM.
 *
 * @param xvm The XVM.
 *
 * @note Opens the step gate and clears breakpoints.
 */
void mr_xvm_debug_probe_detach(struct mr_xvm *xvm);

/**
 * @brief This function sets the execute step gate.
 *
 * @param xvm The XVM.
 * @param gate 0 holds, 1 single-steps, MR_XVM_DEBUG_GATE_OPEN releases.
 */
void mr_xvm_debug_probe_set_gate(struct mr_xvm *xvm, mr_uint32_t gate);

/**
 * @brief This function applies the probe step gate before ISA execute.
 *
 * @param xvm The XVM.
 * @param step The remaining step count, clamped to the gate.
 * @param event The XVM event, filled when the probe holds execution.
 * @return MR_TRUE if ISA execute should be skipped, MR_FALSE otherwise.
 *
 * @note For internal use. Called by mr_xvm_execute before ISA execute.
 */
mr_bool_t mr_xvm_debug_probe_enter(struct mr_xvm *xvm, mr_uint32_t *step,
                                   union mr_xvm_event *event);

/**
 * @brief This function reclaims a debug stop after ISA execute.
 *
 * @param xvm The XVM.
 * @param exec_ret The return from ISA execute.
 * @param event The XVM event from ISA execute.
 * @return MR_XVM_NONE when a debug stop is hidden, otherwise exec_ret.
 *
 * @note For internal use. Called by mr_xvm_execute after ISA execute. Holds
 * the target, restores RUNNING, and notifies the probe.
 */
enum mr_xvm_return mr_xvm_debug_probe_leave(struct mr_xvm *xvm,
                                            enum mr_xvm_return exec_ret,
                                            union mr_xvm_event *event);

/**
 * @brief This function inserts an address-compare breakpoint.
 *
 * @param xvm The XVM.
 * @param addr The program counter to break on.
 * @return 0 on success, a negative error code on failure.
 *
 * @note A duplicate address is treated as success. Returns -MR_ENOMEM when
 * the table is full.
 */
mr_err_t mr_xvm_debug_break_insert(struct mr_xvm *xvm, mr_uint64_t addr);

/**
 * @brief This function removes an address-compare breakpoint.
 *
 * @param xvm The XVM.
 * @param addr The program counter to remove.
 * @return 0 on success, -MR_ENOENT if the address is not in the table.
 */
mr_err_t mr_xvm_debug_break_remove(struct mr_xvm *xvm, mr_uint64_t addr);

/**
 * @brief This function clears all breakpoints and the pending skip.
 *
 * @param xvm The XVM.
 */
void mr_xvm_debug_break_clear(struct mr_xvm *xvm);

/**
 * @brief This function skips the next hit at the given program counter.
 *
 * @param xvm The XVM.
 * @param pc The program counter to skip once.
 *
 * @note Used before continue or single-step from a breakpoint. Only a later
 * hit at this address is consumed.
 */
void mr_xvm_debug_break_skip(struct mr_xvm *xvm, mr_uint64_t pc);

/**
 * @brief This function checks whether the program counter hits a breakpoint.
 *
 * @param xvm The XVM.
 * @param pc The program counter.
 * @return MR_TRUE if execution should stop, MR_FALSE otherwise.
 *
 * @note Called by the ISA before instruction fetch. A pending skip matches
 * only this program counter and does not stop.
 */
mr_bool_t mr_xvm_debug_break_hit(struct mr_xvm *xvm, mr_uint64_t pc);
#endif /* defined(MR_USE_MODULE_XVM_DEBUG) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_MODULE_XVM_DEBUG_H__ */
