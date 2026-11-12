/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)

#if defined(MR_USE_ASSERT)
#include <mrX/libc/mr_assert.h>

/* Compile-time static assert checks, a false expression fails the build */
MR_STATIC_ASSERT(static_assert_int_size, sizeof(int) == 4);
MR_STATIC_ASSERT(static_assert_ptr_size, sizeof(void *) >= 2);

MR_TEST(libcinfo, assert_success_path) {
    volatile int value = 5;

    /* A failing assert prints and exits on the host, only test true paths */
    MR_ASSERT(value == 5);
    MR_ASSERT(value < 6);
    MR_ASSERT(1);
    MR_TEST_ASSERT_EQ_INT(value, 5);
}

MR_TEST(libcinfo, assert_static_compile_time) {
    MR_TEST_ASSERT_EQ_INT(1, 1);
}

#endif /* defined(MR_USE_ASSERT) */

#if defined(MR_USE_ERRNO)
#include <mrX/libc/mr_errno.h>

MR_TEST(libcinfo, errno_codes_unique) {
    static const int codes[]
        = {MR_EPERM,     MR_ENOENT,       MR_EIO,     MR_ENODEV, MR_EAGAIN,
           MR_ENOMEM,    MR_EBUSY,        MR_EEXIST,  MR_EINVAL, MR_ENOSYS,
           MR_ETIMEDOUT, MR_ECANCELED,    MR_EBADF,   MR_ENOTDIR, MR_EISDIR,
           MR_ENOTEMPTY, MR_EMFILE,       MR_ENAMETOOLONG, MR_EXDEV, MR_ERANGE,
           MR_ELOOP,     MR_ESPIPE};
    int i;
    int j;

    for (i = 0; i < (int)(sizeof(codes) / sizeof(codes[0])); i++) {
        for (j = i + 1; j < (int)(sizeof(codes) / sizeof(codes[0])); j++) {
            MR_TEST_EXPECT_NE_INT(codes[i], codes[j]);
        }
    }
}

MR_TEST(libcinfo, errno_codes_nonzero) {
    static const int codes[]
        = {MR_EPERM,     MR_ENOENT,       MR_EIO,     MR_ENODEV, MR_EAGAIN,
           MR_ENOMEM,    MR_EBUSY,        MR_EEXIST,  MR_EINVAL, MR_ENOSYS,
           MR_ETIMEDOUT, MR_ECANCELED,    MR_EBADF,   MR_ENOTDIR, MR_EISDIR,
           MR_ENOTEMPTY, MR_EMFILE,       MR_ENAMETOOLONG, MR_EXDEV, MR_ERANGE,
           MR_ELOOP,     MR_ESPIPE};
    int i;

    for (i = 0; i < (int)(sizeof(codes) / sizeof(codes[0])); i++) {
        MR_TEST_EXPECT_NE_INT(codes[i], 0);
    }
}

MR_TEST(libcinfo, errno_einval_posix_value) {
    /* Holds for the built-in table and for POSIX based host errnos */
    MR_TEST_EXPECT_EQ_INT(MR_EINVAL, 22);
}

MR_TEST(libcinfo, errno_erange_posix_value) {
    MR_TEST_EXPECT_EQ_INT(MR_ERANGE, 34);
}

#endif /* defined(MR_USE_ERRNO) */

#if defined(MR_USE_TYPES)
#include <mrX/libc/mr_types.h>

MR_TEST(libcinfo, types_signed_sizes) {
    MR_TEST_ASSERT_EQ_UINT(sizeof(mr_int8_t), 1);
    MR_TEST_ASSERT_EQ_UINT(sizeof(mr_int16_t), 2);
    MR_TEST_ASSERT_EQ_UINT(sizeof(mr_int32_t), 4);
    MR_TEST_ASSERT_EQ_UINT(sizeof(mr_int64_t), 8);
}

MR_TEST(libcinfo, types_unsigned_sizes) {
    MR_TEST_ASSERT_EQ_UINT(sizeof(mr_uint8_t), 1);
    MR_TEST_ASSERT_EQ_UINT(sizeof(mr_uint16_t), 2);
    MR_TEST_ASSERT_EQ_UINT(sizeof(mr_uint32_t), 4);
    MR_TEST_ASSERT_EQ_UINT(sizeof(mr_uint64_t), 8);
}

MR_TEST(libcinfo, types_size_ssize) {
#if defined(MR_USE_ARCH_64B)
    MR_TEST_ASSERT_EQ_UINT(sizeof(mr_size_t), 8);
    MR_TEST_ASSERT_EQ_UINT(sizeof(mr_ssize_t), 8);
    MR_TEST_ASSERT_EQ_UINT((mr_size_t)MR_SIZE_MAX, (mr_size_t)-1);
#else
    MR_TEST_ASSERT_EQ_UINT(sizeof(mr_size_t), 4);
    MR_TEST_ASSERT_EQ_UINT(sizeof(mr_ssize_t), 4);
    MR_TEST_ASSERT_EQ_UINT((mr_size_t)MR_SIZE_MAX, (mr_size_t)-1);
#endif /* defined(MR_USE_ARCH_64B) */
}

MR_TEST(libcinfo, types_pointer_float_sizes) {
    MR_TEST_ASSERT_EQ_UINT(sizeof(mr_ptr_t), sizeof(void *));
    MR_TEST_ASSERT_EQ_UINT(sizeof(mr_f32_t), 4);
    MR_TEST_ASSERT_EQ_UINT(sizeof(mr_f64_t), 8);
}

MR_TEST(libcinfo, types_limits_fixed) {
    MR_TEST_ASSERT_EQ_INT(MR_INT8_MIN, -128);
    MR_TEST_ASSERT_EQ_INT(MR_INT8_MAX, 127);
    MR_TEST_ASSERT_EQ_INT(MR_INT16_MIN, -32768);
    MR_TEST_ASSERT_EQ_INT(MR_INT16_MAX, 32767);
    MR_TEST_ASSERT_EQ_INT(MR_INT32_MIN, (-2147483647 - 1));
    MR_TEST_ASSERT_EQ_INT(MR_INT32_MAX, 2147483647);
    MR_TEST_ASSERT_EQ_UINT(MR_UINT8_MAX, 255U);
    MR_TEST_ASSERT_EQ_UINT(MR_UINT16_MAX, 65535U);
    MR_TEST_ASSERT_EQ_UINT(MR_UINT32_MAX, 4294967295U);
    MR_TEST_EXPECT_EQ_INT(MR_INT64_MAX, 9223372036854775807LL);
    MR_TEST_EXPECT_EQ_INT(MR_INT64_MIN, (-9223372036854775807LL - 1));
    MR_TEST_EXPECT_EQ_UINT(MR_UINT64_MAX, 18446744073709551615ULL);
}

MR_TEST(libcinfo, types_ssize_limits) {
    MR_TEST_ASSERT_EQ_INT((mr_int64_t)MR_SSIZE_MAX,
                          (mr_int64_t)((mr_size_t)MR_SIZE_MAX >> 1));
    MR_TEST_ASSERT_EQ_INT((mr_int64_t)MR_SSIZE_MIN, -MR_SSIZE_MAX - 1);
}

MR_TEST(libcinfo, types_bool_values) {
    MR_TEST_ASSERT_EQ_INT((int)MR_FALSE, 0);
    MR_TEST_ASSERT_EQ_INT((int)MR_TRUE, 1);
    MR_TEST_ASSERT_EQ_UINT(sizeof(mr_bool_t) >= 1, 1);
}

MR_TEST(libcinfo, types_unsigned_wrap) {
    MR_TEST_ASSERT_EQ_UINT((mr_uint8_t)(MR_UINT8_MAX + 1), 0);
    MR_TEST_ASSERT_EQ_UINT((mr_uint16_t)(MR_UINT16_MAX + 1), 0);
    MR_TEST_ASSERT_EQ_UINT((mr_uint32_t)((mr_uint32_t)MR_UINT32_MAX + 1U), 0);
}

#endif /* defined(MR_USE_TYPES) */

#endif /* defined(MR_USE_TEST) */
