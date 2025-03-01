/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/os/mr_kthread.h>
#if defined(MR_USE_KOS)
#include "FreeRTOS.h"
#include "task.h"
#include <port/mr_port_thread.h>

static mr_err_t err_shift(BaseType_t xReturn) {
    switch (xReturn) {
        case pdPASS: {
            return 0;
        }
#ifdef pdTIMEOUT
        case pdTIMEOUT: {
            return -MR_ETIMEDOUT;
        }
#endif /* pdTIMEOUT */
#ifdef errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY
        case errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY: {
            return -MR_ENOMEM;
        }
#endif /* errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY */
        default: {
            return -MR_EIO;
        }
    }
}

static void Task_Entry(void *kthread) {
    mr_kthread_entry(kthread);
}

void *mr_port_thread_create(const char *name, void *kthread,
                            mr_size_t stack_size, mr_uint32_t priority,
                            mr_tick_t tick) {
    TaskHandle_t thread;
    BaseType_t xReturn;

    /* Shift priority(Low to High) */
    if (priority > (configMAX_PRIORITIES - 1)) {
        priority = configMAX_PRIORITIES - 1;
    }
    priority = (configMAX_PRIORITIES - 1) - priority;

    /* Create thread */
#if (configSUPPORT_DYNAMIC_ALLOCATION == 0)
#error "Please set 'configSUPPORT_DYNAMIC_ALLOCATION' to '1'"
#endif /* (configSUPPORT_DYNAMIC_ALLOCATION == 0) */
    xReturn = xTaskCreate((TaskFunction_t)Task_Entry, (const char *)name,
                          (uint16_t)stack_size, (void *)kthread,
                          (UBaseType_t)priority, &thread);
    if (xReturn == pdPASS) {
        /* Set kthread as task tag */
#if (configUSE_APPLICATION_TASK_TAG == 0)
#error "Please set 'configUSE_APPLICATION_TASK_TAG' to '1'"
#endif /* (configUSE_APPLICATION_TASK_TAG == 0) */
        vTaskSetApplicationTaskTag(thread, kthread);
    }

    /* Shift error code */
    return err_shift(xReturn) == 0 ? thread : MR_NULL;
}

mr_err_t mr_port_thread_del(void *thread) {
    /* Delete thread */
#if (INCLUDE_vTaskDelete == 0)
#error "Please set 'INCLUDE_vTaskDelete' to '1'"
#endif /* (INCLUDE_vTaskDelete == 0) */
    vTaskDelete(thread);
    return 0;
}

mr_err_t mr_port_thread_startup(void *thread) {
    return 0;
}

mr_err_t mr_port_thread_resume(void *thread) {
    /* Resume thread */
#if (INCLUDE_vTaskSuspend == 0)
#error "Please set 'INCLUDE_vTaskSuspend' to '1'"
#endif /* (INCLUDE_vTaskSuspend == 0) */
    vTaskResume(thread);
    return 0;
}

mr_err_t mr_port_thread_yield() {
    /* Yield thread */
    taskYIELD();
    return 0;
}

mr_err_t mr_port_thread_suspend(void *thread) {
    /* Suspend thread */
    vTaskSuspend(thread);
    return 0;
}

mr_err_t mr_port_thread_sleep(mr_tick_t tick) {
    /* Delay thread */
#if (INCLUDE_vTaskDelay == 0)
#error "Please set 'INCLUDE_vTaskDelay' to '1'"
#endif /* (INCLUDE_vTaskDelay == 0) */
    vTaskDelay(tick);
    return 0;
}

void *mr_port_thread_self(void) {
    TaskHandle_t thread;

    /* Get thread self */
    thread = xTaskGetCurrentTaskHandle();

    /* Return kthread */
    return xTaskGetApplicationTaskTag(thread);
}
#endif /* defined(MR_USE_KOS) */
