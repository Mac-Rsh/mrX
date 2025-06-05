#ifndef __MR_CONFIG_H__
#define __MR_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define MR_USE_PRINTF
#define MR_CFG_PRINTF_BUF_SIZE 64
#define MR_USE_TIMER
#define MR_USE_WORKQUEUE
#define MR_USE_IRQ
#define MR_USE_IRQ_DEFER
#define MR_CFG_IRQ_TABLE_SIZE  32
#define MR_CFG_CLOCK_HOOK_SIZE 2

#define MR_USE_ENTRY
#define MR_USE_64BIT_ARCH
#define MR_USE_MALLOC
#define MR_CFG_HEAP_SIZE 4096
#define MR_USE_SPRINTF
#define MR_USE_ASSERT
#define MR_USE_STRING
#define MR_USE_STRING_SAFE
#define MR_USE_SCANF
#define MR_USE_TYPES
#define MR_USE_ERRNO

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_CONFIG_H__ */
