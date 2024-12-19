/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr-base/mr_assert.h>
#include <mr-base/mr_autoinit.h>
#include <mr-base/mr_heap.h>
#include <mr-task/mr_ffs.h>
#include <mr-task/mr_idle.h>
#include <mr-task/mr_scheduler.h>
#include <mr_hw.h>

#if (MR_CFG_TASK_PRIORITY_MAX > 32)
#error "Task priority max is too large(must <= 32)"
#endif /* (MR_CFG_TASK_PRIORITY_MAX > 32) */
static mr_list_t sched_priority[MR_CFG_TASK_PRIORITY_MAX];
static mr_uint32_t sched_priority_mask;
static mr_task_t *sched_curr_task;
static mr_list_t sched_exited_static;
static mr_list_t sched_exited_dynmic;

void mr_scheduler_init(void) {
    size_t i;

    /* Initialize priority lists */
    for (i = 0; i < MR_CFG_TASK_PRIORITY_MAX; i++) {
        mr_list_init(&sched_priority[i]);
    }
    sched_priority_mask = 0;

    /* Initialize exited task list */
    mr_list_init(&sched_exited_static);
    mr_list_init(&sched_exited_dynmic);

    /* Initialize idle task */
    mr_task_idle_init();
}

static mr_task_t *s_schedule(void) {
    mr_uint32_t priority;

    /* Get highest priority task */
    priority = mr_ffs(sched_priority_mask) - 1;
    return MR_LIST_ENTRY(sched_priority[priority].next, mr_task_t, list);
}

void mr_scheduler_start(void) {
    /* Start scheduler */
    sched_curr_task = s_schedule();
    sched_curr_task->stat = MR_TASK_STAT_RUNNING;

    /* Switch context */
    mr_hw_context_switch_to(sched_curr_task->stack);
}

#ifdef MR_USE_TASK_STACK_OVERFLOW_CHECK
static void s_task_stack_overflow_check(mr_task_t *task) {
    /* Stack overflow check */
    if ((*((char *)task->stack_addr) != '#')
        || (task->stack <= task->stack_addr)
        || (task->stack > (task->stack_addr + task->stack_size))) {
        /* Stack overflow */
        mr_hw_irq_save();
        MR_ASSERT(*((char *)task->stack_addr) == '#');
        MR_ASSERT(task->stack > task->stack_addr);
        MR_ASSERT(task->stack <= (task->stack_addr + task->stack_size));
        while (1) {
        }
    }
}
#endif /* MR_USE_TASK_STACK_OVERFLOW_CHECK */

void mr_scheduler(void) {
    mr_task_t *from_task, *to_task;
    int mask;

    /* Disable interrupt */
    mask = mr_hw_irq_save();

    /* Get current task */
    from_task = sched_curr_task;

    /* Check whether current task need yield */
    if (from_task->flags & MR_TASK_FLAG_YIELD) {
        /* Only yield running task */
        if (from_task->stat == MR_TASK_STAT_RUNNING) {
            /* Move task to priority list tail */
            mr_list_move(&from_task->list,
                         &sched_priority[from_task->priority]);
            from_task->stat = MR_TASK_STAT_READY;
        }
        from_task->flags &= ~MR_TASK_FLAG_YIELD;
    }

    /* Get next task */
    sched_curr_task = to_task = s_schedule();
    to_task->stat = MR_TASK_STAT_RUNNING;

    /* Enable interrupt */
    mr_hw_irq_restore(mask);

    /* Check whether tasks need to be switched */
    if (from_task != to_task) {
#ifdef MR_USE_TASK_STACK_OVERFLOW_CHECK
        /* Stack overflow check */
        s_task_stack_overflow_check(to_task);
#endif /* MR_USE_TASK_STACK_OVERFLOW_CHECK */

        /* Switch context */
        mr_hw_context_switch(from_task->stack, to_task->stack);
    }
}

void mr_scheduler_add(mr_task_t *task) {
    int mask;

    /* Disable interrupt */
    mask = mr_hw_irq_save();

    /* Check task state is ready */
    if (task->stat != MR_TASK_STAT_READY) {
        /* Add task to priority list */
        mr_list_del(&task->list);
        mr_list_add(&sched_priority[task->priority], &task->list);
        sched_priority_mask |= (1 << task->priority);
        task->stat = MR_TASK_STAT_READY;
    }

    /* Enable interrupt */
    mr_hw_irq_restore(mask);
}

void mr_scheduler_del(mr_task_t *task) {
    int mask;

    /* Disable interrupt */
    mask = mr_hw_irq_save();

    /* Remove task from priority list */
    mr_list_del(&task->list);
    if (mr_list_is_empty(&sched_priority[task->priority])) {
        sched_priority_mask &= ~(1 << task->priority);
    }
    task->stat = MR_TASK_STAT_SUSPEND;

    /* Enable interrupt */
    mr_hw_irq_restore(mask);
}

void mr_scheduler_yield(mr_task_t *task) {
    int mask;

    /* Disable interrupt */
    mask = mr_hw_irq_save();

    /* Mark task need yield */
    task->flags |= MR_TASK_FLAG_YIELD;

    /* Enable interrupt */
    mr_hw_irq_restore(mask);
}

void mr_scheduler_exit(mr_task_t *task) {
    int mask;

    /* Disable interrupt */
    mask = mr_hw_irq_save();

    /* Remove task from scheduler */
    mr_scheduler_del(task);

    /* Add task to exited list */
    task->stat = MR_TASK_STAT_EXIT;
    if (task->flags & MR_TASK_FLAG_STATIC) {
        mr_list_add(&sched_exited_static, &task->list);
    } else {
        mr_list_add(&sched_exited_dynmic, &task->list);
    }

    /* Enable interrupt */
    mr_hw_irq_restore(mask);
}

void mr_scheduler_cleanup(void) {
    mr_list_t *l_pos, *l_n;
    mr_task_t *task;

    /* Free dynamic exited tasks */
    MR_LIST_FOR_EACH_SAFE(l_pos, l_n, &sched_exited_dynmic) {
        task = MR_LIST_ENTRY(l_pos, mr_task_t, list);

        /* Free task */
        mr_list_del(&task->list);
        mr_free(task);
    }
}

mr_task_t *mr_scheduler_task_curr_get(void) {
    return sched_curr_task;
}
