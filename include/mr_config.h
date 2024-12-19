#ifndef __MR_CONFIG_H__
#define __MR_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define MR_USE_DEBUG
#define MR_CFG_HEAP_SIZE 16384000
#define MR_USE_LOG
#define MR_CFG_LOG_LEVEL 4
#define MR_CFG_TASK_NAME_MAX 8
#define MR_CFG_TASK_PRIORITY_MAX 32
#define MR_USE_TASK_STACK_OVERFLOW_CHECK
#define MR_CFG_TASK_IDLE_STACK_SIZE 512
#define MR_CFG_TASK_MAIN_PRIORITY 15
#define MR_CFG_TASK_MAIN_STACK_SIZE 512000

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_CONFIG_H__ */
