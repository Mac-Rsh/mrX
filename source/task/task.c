/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr-base/mr_assert.h>
#include <mr-base/mr_heap.h>
#include <mr-task/mr_scheduler.h>
#include <mr-task/mr_task.h>
#include <mr_hw.h>

static void t_entry(void) {
    mr_task_t *task;

    /* Run task entry */
    task = mr_task_self();
    ((void (*)(void *))task->entry)(task->args);

    /* Exit task from scheduler */
    mr_scheduler_exit(task);

    /* Schedule task */
    mr_scheduler();
}

static void t_init(mr_task_t *task, const char *name, void *entry, void *args,
                   mr_uint8_t *stack_addr, mr_size_t stack_size,
                   mr_uint8_t priority) {
    /* Init task */
    strncpy(task->name, name, MR_CFG_TASK_NAME_MAX - 1);
    task->priority = priority;
    task->flags = MR_TASK_FLAG_MAGIC | MR_TASK_FLAG_STATIC;
    task->stat = MR_TASK_STAT_INIT;
    mr_list_init(&task->list);
    task->error = MR_EOK;

    /* Init task stack */
    task->entry = entry;
    task->args = args;
    task->stack_addr = stack_addr;
    task->stack_size = stack_size;
    memset(stack_addr, '#', stack_size);
    stack_addr = stack_addr + stack_size;
    task->stack = mr_hw_stack_init(t_entry, stack_addr);
}

void mr_task_init(mr_task_t *task, const char *name, void (*entry)(void *),
                  void *args, void *stack, mr_size_t stack_size,
                  mr_uint8_t priority) {
    MR_ASSERT((task != MR_NULL) && (!(task->flags & MR_TASK_FLAG_MAGIC)));
    MR_ASSERT((name != MR_NULL) && (entry != MR_NULL));
    MR_ASSERT((stack != MR_NULL) && (stack_size > 0));

    /* Init task */
    t_init(task, name, entry, args, stack, stack_size, priority);
}

mr_task_t *mr_task_create(const char *name, void (*entry)(void *), void *args,
                          mr_size_t stack_size, mr_uint8_t priority) {
    mr_uint8_t *stack_addr;
    mr_task_t *task;

    MR_ASSERT((name != MR_NULL) && (entry != MR_NULL));
    MR_ASSERT(stack_size > 0);

    /* Allocate memory(task + stack) */
    task = mr_malloc(sizeof(mr_task_t) + stack_size);
    if (task == MR_NULL) {
        return MR_NULL;
    }

    /* Offset stack address */
    stack_addr = (mr_uint8_t *)task + sizeof(mr_task_t);
    /* Init task */
    t_init(task, name, entry, args, stack_addr, stack_size, priority);

    /* Mark task as dynamic */
    task->flags &= ~MR_TASK_FLAG_STATIC;
    return task;
}

mr_err_t mr_task_startup(mr_task_t *task) {
    MR_ASSERT((task != MR_NULL) && (task->flags & MR_TASK_FLAG_MAGIC));

    /* Check task stat */
    if (task->stat != MR_TASK_STAT_INIT) {
        return -MR_EINVAL;
    }

    /* Startup task */
    task->stat = MR_TASK_STAT_SUSPEND;
    mr_scheduler_add(task);
    return MR_EOK;
}

mr_err_t mr_task_resume(mr_task_t *task) {
    MR_ASSERT((task != MR_NULL) && (task->flags & MR_TASK_FLAG_MAGIC));

    /* Check task stat */
    if (task->stat == MR_TASK_STAT_SUSPEND) {
        /* Add task to scheduler */
        mr_scheduler_add(task);
    } else if (task->stat == MR_TASK_STAT_READY) {
        /* Task is ready */
    } else {
        return -MR_EINVAL;
    }

    /* Yield current task */
    mr_task_yield();
    return MR_EOK;
}

void mr_task_yield(void) {
    /* Yield current task */
    mr_scheduler_yield(mr_task_self());

    /* Schedule task */
    mr_scheduler();
}

void mr_task_suspend(mr_task_t *task) {
    MR_ASSERT((task != MR_NULL) && (task->flags & MR_TASK_FLAG_MAGIC));

    /* Remove task from scheduler */
    mr_scheduler_del(task);
}

void mr_task_exit(mr_task_t *task) {
    MR_ASSERT((task != MR_NULL) && (task->flags & MR_TASK_FLAG_MAGIC));

    /* Exit task */
    mr_scheduler_exit(task);
}

void mr_task_wait(mr_task_t *task) {
    MR_ASSERT((task != MR_NULL) && (task->flags & MR_TASK_FLAG_MAGIC));
    MR_ASSERT(task != mr_task_self());

    /* Wait task */
    while (task->stat != MR_TASK_STAT_EXIT) {
        mr_task_yield();
    }

    /* Cleanup scheduler */
    mr_scheduler_cleanup();
}

mr_task_t *mr_task_self(void) {
    /* Get current task */
    return mr_scheduler_task_curr_get();
}
