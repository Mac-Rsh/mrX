/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_VERSION_H__
#define __MR_VERSION_H__

/**
 * @addtogroup Version
 * @{
 */

/* MR-X version definition */
#define MR_VERSION_MAJOR                (1U)
#define MR_VERSION_MINOR                (0U)
#define MR_VERSION_PATCH                (0U)

/**
 * @brief This macro function constructs a version code.
 *
 * @param _major The major version.
 * @param _minor The minor version.
 * @param _patch The patch version.
 * @return The version code.
 */
#define MR_VERSION_CODE(_major, _minor, _patch)                                \
    (((_major) << 16) | ((_minor) << 8) | (_patch))

/* Version code */
#define MR_VERSION                                                             \
    MR_VERSION_CODE(MR_VERSION_MAJOR, MR_VERSION_MINOR, MR_VERSION_PATCH)

/**
 * @brief This macro function checks if the version is the same as the given
 * version.
 *
 * @param _major The major version.
 * @param _minor The minor version.
 * @param _patch The patch version.
 * @return MR_TRUE if the version is the same as the given version, MR_FALSE
 * otherwise.
 */
#define MR_VERSION_IS(_major, _minor, _patch)                                  \
    (MR_VERSION == MR_VERSION_CODE(_major, _minor, _patch))

/** @} */

#endif /* __MR_VERSION_H__ */
