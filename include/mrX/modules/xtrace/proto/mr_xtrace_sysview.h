/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_MODULE_XTRACE_SYSVIEW_H__
#define __MR_MODULE_XTRACE_SYSVIEW_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_XTRACE_PROTO_SYSVIEW)
#include <mrX/modules/xtrace/mr_xtrace.h>
#endif /* defined(MR_USE_MODULE_XTRACE_PROTO_SYSVIEW) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup XTrace-SysView
 * @{
 */

#if defined(MR_USE_MODULE_XTRACE_PROTO_SYSVIEW)
/* XTrace SystemView protocol definition */
#define MR_SYSVIEW_SYNC_LEN             (10U)
#define MR_SYSVIEW_STR_MAX              (127U)
#define MR_SYSVIEW_ARGS_MAX             (8U)

/* XTrace SystemView event ID definition */
#define MR_SYSVIEW_ID_OVERFLOW          (1U)
#define MR_SYSVIEW_ID_START             (10U)
#define MR_SYSVIEW_ID_STOP              (11U)
#define MR_SYSVIEW_ID_SYSDESC           (14U)
#define MR_SYSVIEW_ID_MARK_START        (15U)
#define MR_SYSVIEW_ID_MARK_STOP         (16U)
#define MR_SYSVIEW_ID_INIT              (24U)
#define MR_SYSVIEW_ID_PRINT             (26U)
#define MR_SYSVIEW_ID_EX                (31U)
#define MR_SYSVIEW_EX_NAME_MARKER       (1U)

/**
 * @brief This function registers the SEGGER SystemView protocol encoder with
 * xtrace.
 *
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xtrace_sysview_register(void);
#endif /* defined(MR_USE_MODULE_XTRACE_PROTO_SYSVIEW) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_MODULE_XTRACE_SYSVIEW_H__ */
