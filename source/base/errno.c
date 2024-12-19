/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include "mr-base/mr_errno.h"
#include "mr-base/mr_service.h"

static const char *errno_str[] = {
    "Ok",
    "Permission denied",
    "No such file or directory",
    "Input/output error",
    "Out of memory",
    "Device or resource busy",
    "File exists",
    "Invalid argument",
    "Operation timed out",
};

static int errno_map[]
    = {MR_EOK,   MR_EPERM,  MR_ENOENT, MR_EIO,      MR_ENOMEM,
       MR_EBUSY, MR_EEXIST, MR_EINVAL, MR_ETIMEDOUT};

const char *mr_strerror(int errnum) {
    mr_size_t i;

    /* Find error string */
    for (i = 0; i < MR_ARRAY_SIZE(errno_map); i++) {
        if (errno_map[i] == errnum) {
            return errno_str[i];
        }
    }
    /* Return unknown string */
    return "Unknown error";
}
