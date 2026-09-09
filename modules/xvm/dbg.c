/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/modules/xvm/mr_xvm_dbg.h>
#if defined(MR_USE_MODULE_XVM_DEBUG)
#include <mrX/modules/xvm/mr_xvm.h>
#include <mrX/libc/mr_string.h>

void mr_xvm_dbg_init(mr_xvm_t *xvm) {
    /* Check parameter */
    MR_ASSERT(xvm != MR_NULL);

    /* Init XVM debug */
    xvm->dbg.target = mr_xvm_isa_target(xvm->isa);
    xvm->dbg.probe.step_gate = MR_XVM_DBG_GATE_OPEN;
    xvm->dbg.probe.entry = MR_NULL;
    xvm->dbg.probe.priv = MR_NULL;
    xvm->dbg.breaktable.skip = MR_FALSE;
    xvm->dbg.breaktable.num = 0;
}

mr_xvm_isa_target_t *mr_xvm_dbg_target(mr_xvm_t *xvm) {
    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));

    /* Get target kept by the debug unit */
    return xvm->dbg.target;
}

mr_err_t mr_xvm_dbg_probe_attach(mr_xvm_t *xvm, mr_xvm_dbg_probe_entry_t *entry,
                                 void *priv) {
    mr_xvm_dbg_probe_t *probe;

    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));
    MR_ASSERT(entry != MR_NULL);

    /* One probe per XVM */
    probe = &xvm->dbg.probe;
    if (probe->entry) {
        return -MR_EBUSY;
    }

    /* Implant the probe and hold the target */
    probe->step_gate = MR_XVM_DBG_GATE_HOLD;
    probe->entry = (mr_ptr_t)entry;
    probe->priv = priv;
    return 0;
}

void mr_xvm_dbg_probe_detach(mr_xvm_t *xvm) {
    mr_xvm_dbg_probe_t *probe;

    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));

    /* Pull the probe out and open the gate */
    probe = &xvm->dbg.probe;
    probe->step_gate = MR_XVM_DBG_GATE_OPEN;
    probe->entry = MR_NULL;
    probe->priv = MR_NULL;
    mr_xvm_dbg_break_clear(xvm);
}

void mr_xvm_dbg_probe_set_gate(mr_xvm_t *xvm, mr_uint32_t gate) {
    mr_xvm_dbg_probe_t *probe;

    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));

    /* Update the execute step gate */
    probe = &xvm->dbg.probe;
    probe->step_gate = gate;
}

mr_bool_t mr_xvm_dbg_probe_enter(mr_xvm_t *xvm, mr_uint32_t *step,
                                 mr_xvm_event_t *event) {
    mr_xvm_dbg_probe_t *probe;
    mr_uint32_t gate;

    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));
    MR_ASSERT((step != MR_NULL) && (event != MR_NULL));

    /* No probe, pass through */
    probe = &xvm->dbg.probe;
    if (!probe->entry) {
        return MR_FALSE;
    }

    /* Clamp the step to the probe gate */
    gate = xvm->dbg.probe.step_gate;
    if (gate < (*step)) {
        *step = gate;
    }
    if (*step != 0) {
        return MR_FALSE;
    }

    /* Held by the probe, skip ISA */
    event->none.step = 0;
    return MR_TRUE;
}

mr_xvm_return_t mr_xvm_dbg_probe_leave(mr_xvm_t *xvm, mr_xvm_return_t exec_ret,
                                       mr_xvm_event_t *event) {
    mr_xvm_dbg_probe_entry_t *entry;
    mr_xvm_dbg_probe_t *probe;
    mr_bool_t step_stop;
    void *priv;

    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));
    MR_ASSERT(event != MR_NULL);

    /* No probe, pass through */
    probe = &xvm->dbg.probe;
    if (!probe->entry) {
        return exec_ret;
    }

    /* Step stop: exception, or a completed single-step */
    step_stop = (probe->step_gate == MR_XVM_DBG_GATE_STEP)
             && (exec_ret == MR_XVM_NONE);
    if ((exec_ret != MR_XVM_EXCEPT) && (!step_stop)) {
        return exec_ret;
    }

    /* Save probe entry and priv */
    entry = (mr_xvm_dbg_probe_entry_t *)probe->entry;
    priv = (void *)probe->priv;

    /* Call probe entry */
    entry(priv, exec_ret, event);
    if (exec_ret != MR_XVM_NONE) {
        event->none.step = 0;
    }

    /* Hold the target and hide the stop from the runtime */
    mr_atomic_store(&xvm->status, MR_XVM_STATUS_RUNNING);
    probe->step_gate = MR_XVM_DBG_GATE_HOLD;
    return MR_XVM_NONE;
}

mr_err_t mr_xvm_dbg_break_insert(mr_xvm_t *xvm, mr_uint64_t addr) {
    mr_xvm_dbg_breaktable_t *table;
    mr_uint32_t i;

    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));

    /* Already inserted */
    table = &xvm->dbg.breaktable;
    for (i = 0; i < table->num; i++) {
        if (table->addr[i] == addr) {
            return 0;
        }
    }

    /* Table is full */
    if (table->num >= MR_CFG_XVM_BREAKPOINT_NUM) {
        return -MR_ENOMEM;
    }

    /* Append to the packed table */
    table->addr[table->num] = addr;
    table->num++;
    return 0;
}

mr_err_t mr_xvm_dbg_break_remove(mr_xvm_t *xvm, mr_uint64_t addr) {
    mr_xvm_dbg_breaktable_t *table;
    mr_uint32_t i;

    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));

    /* Get breaktable */
    table = &xvm->dbg.breaktable;

    /* Find and pack the hole with the last entry */
    for (i = 0; i < table->num; i++) {
        if (table->addr[i] == addr) {
            table->addr[i] = table->addr[table->num - 1];
            table->num--;
            return 0;
        }
    }
    return -MR_ENOENT;
}

void mr_xvm_dbg_break_clear(mr_xvm_t *xvm) {
    mr_xvm_dbg_breaktable_t *table;

    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));

    /* Clear breakpoints and the pending skip */
    table = &xvm->dbg.breaktable;
    table->skip = MR_FALSE;
    table->num = 0;
}

void mr_xvm_dbg_break_skip(mr_xvm_t *xvm, mr_uint64_t pc) {
    mr_xvm_dbg_breaktable_t *table;

    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));

    /* Skip the next hit at this program counter */
    table = &xvm->dbg.breaktable;
    table->skip_addr = pc;
    table->skip = MR_TRUE;
}

mr_bool_t mr_xvm_dbg_break_hit(mr_xvm_t *xvm, mr_uint64_t pc) {
    mr_xvm_dbg_breaktable_t *table;
    mr_uint32_t i;

    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));

    /* Get breaktable */
    table = &xvm->dbg.breaktable;

    /* Fast path when the table is empty */
    if (table->num == 0) {
        return MR_FALSE;
    }

    /* Consume one skip only when it matches this program counter */
    if ((table->skip) && (table->skip_addr == pc)) {
        table->skip = MR_FALSE;
        return MR_FALSE;
    }

    /* Scan packed entries [0, num) */
    for (i = 0; i < table->num; i++) {
        if (table->addr[i] == pc) {
            return MR_TRUE;
        }
    }
    return MR_FALSE;
}
#endif /* defined(MR_USE_MODULE_XVM_DEBUG) */
