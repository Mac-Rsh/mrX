/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KVERSION_H__
#define __MR_KVERSION_H__

/**
 * @addtogroup Kversion
 * @{
 */

/* MR-X kernel version definition */
#define MR_KVERSION_MAJOR               1
#define MR_KVERSION_MINOR               0
#define MR_KVERSION_PATCH               0

/**
 * @brief This macro function constructs a kernel version code.
 *
 * @param _major The major version.
 * @param _minor The minor version.
 * @param _patch The patch version.
 * @return The kernel version code.
 */
#define MR_KVERSION(_major, _minor, _patch)                                    \
    ((_major) << 16 | (_minor) << 8 | (_patch))

/* Kversion code */
#define MR_KVERSION_CODE                                                       \
    MR_KVERSION(MR_KVERSION_MAJOR, MR_KVERSION_MINOR, MR_KVERSION_PATCH)

/**
 * @brief This macro function checks if the kernel version is the same as the
 * given version.
 *
 * @param _major The major version.
 * @param _minor The minor version.
 * @param _patch The patch version.
 * @return MR_TRUE if the kernel version is the same as the given version,
 * MR_FALSE otherwise.
 */
#define MR_KVERSION_IS(_major, _minor, _patch)                                 \
    (MR_KVERSION_CODE == MR_KVERSION(_major, _minor, _patch))

/** @} */

#endif /* __MR_KVERSION_H__ */
