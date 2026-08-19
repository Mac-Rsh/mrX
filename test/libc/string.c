/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/libc/mr_string.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_errno.h>

/* -------------------------------------------------------------------------- */
/* Utilities: fill / verify canary pattern                                    */
static void canary_fill(void *p, mr_size_t n) {
    mr_uint8_t *d;
    mr_size_t i;

    for (d = (mr_uint8_t *)p, i = 0; i < n; ++i) {
        d[i] = (mr_uint8_t)(i & 0xFFU);
    }
}

static int canary_verify(void *p, mr_size_t n) {
    mr_uint8_t *d;
    mr_size_t i;

    for (d = (mr_uint8_t *)p, i = 0; i < n; ++i) {
        if (d[i] != (mr_uint8_t)(i & 0xFFU)) {
            return 0;
        }
    }
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Test: memcpy correctness                                                   */
MR_TEST(string, memcpy_basic) {
    enum {
        SZ = 256
    };
    char *src, *dst;

    src = mr_malloc(SZ);
    dst = mr_malloc(SZ);
    if ((src == MR_NULL) || (dst == MR_NULL)) {
        MR_TEST_EXPECT_NE_PTR(src, MR_NULL);
        MR_TEST_EXPECT_NE_PTR(dst, MR_NULL);
        goto _exit;
    }

    canary_fill(src, SZ);
    mr_memcpy(dst, src, SZ);
    MR_TEST_EXPECT_EQ_INT(canary_verify(dst, SZ), 1);

    canary_fill(src, SZ / 3);
    mr_memcpy(dst, src, SZ / 3);
    MR_TEST_EXPECT_EQ_INT(canary_verify(dst, SZ / 3), 1);

    canary_fill(src + 1, SZ - 1);
    mr_memcpy(dst + 1, src + 1, SZ - 1);
    MR_TEST_EXPECT_EQ_INT(canary_verify(dst + 1, SZ - 1), 1);

    canary_fill(src + 1, SZ - 1);
    mr_memcpy(dst, src + 1, SZ - 1);
    MR_TEST_EXPECT_EQ_INT(canary_verify(dst, SZ - 1), 1);

_exit:
    if (src != MR_NULL) {
        mr_free(src);
    }
    if (dst != MR_NULL) {
        mr_free(dst);
    }
}

/* -------------------------------------------------------------------------- */
/* Test: memset sets every byte to the given value                            */
MR_TEST(string, memset_basic) {
    enum {
        SZ = 137
    };
    mr_size_t i;
    char *buf;

    buf = mr_malloc(SZ);
    if (buf == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(buf, MR_NULL);
        goto _exit;
    }

    mr_memset(buf, 0x5A, SZ);
    for (i = 0; i < SZ; ++i) {
        MR_TEST_EXPECT_EQ_INT(buf[i], 0x5A);
    }

    mr_memset(buf + 1, 0x5A, SZ - 1);
    for (i = 1; i < SZ - 1; ++i) {
        MR_TEST_EXPECT_EQ_INT(buf[i], 0x5A);
    }

_exit:
    if (buf != MR_NULL) {
        mr_free(buf);
    }
}

/* -------------------------------------------------------------------------- */
/* Test: memcmp detects differences                                           */
MR_TEST(string, memcmp_diff) {
    const char a[] = {0, 1, 2, 3, 4, 5, 6, 7};
    const char b[] = {0, 1, 2, 3, 4, 5, 6, 8};

    MR_TEST_ASSERT_EQ_INT(mr_memcmp(a, a, sizeof(a)), 0);
    MR_TEST_ASSERT_NE_INT(mr_memcmp(a, b, sizeof(a)), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: memchr finds the first occurrence of a byte                          */
MR_TEST(string, memchr_find) {
    const char buf[] = "0123456789ABCDEF";

    MR_TEST_ASSERT_EQ_PTR(mr_memchr(buf, '5', sizeof(buf)), buf + 5);
    MR_TEST_ASSERT_EQ_PTR(mr_memchr(buf, 'X', sizeof(buf)), MR_NULL);
}

/* -------------------------------------------------------------------------- */
/* Test: memccpy stops after the sentinel character                           */
MR_TEST(string, memccpy_stop) {
    const char src[] = "Hello\0World";
    char dst[sizeof(src)];
    void *e;

    mr_memset(dst, '\0', sizeof(dst));
    e = mr_memccpy(dst, src, 'o', sizeof(dst));
    MR_TEST_ASSERT_NE_PTR(e, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(dst[4], 'o');
    MR_TEST_ASSERT_EQ_INT(dst[5], '\0');
}

/* -------------------------------------------------------------------------- */
/* Test: strcpy copies C-string including terminator                          */
MR_TEST(string, strcpy_basic) {
    const char src[] = "string_test";
    char *dst;

    dst = mr_malloc(32);
    if (dst == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(dst, MR_NULL);
        goto _exit;
    }

    mr_strcpy(dst, src);
    MR_TEST_EXPECT_EQ_INT(mr_strcmp(dst, src), 0);
    MR_TEST_EXPECT_EQ_UINT(mr_strlen(dst), mr_strlen(src));

_exit:
    if (dst != MR_NULL) {
        mr_free(dst);
    }
}

/* -------------------------------------------------------------------------- */
/* Test: strerror copies C-string including terminator                        */
MR_TEST(string, strerror_basic) {
    mr_strerror(0);
    mr_strerror(MR_EPERM);
    mr_strerror(MR_ENOENT);
    mr_strerror(MR_EIO);
    mr_strerror(MR_EAGAIN);
    mr_strerror(MR_ENOMEM);
    mr_strerror(MR_EBUSY);
    mr_strerror(MR_EEXIST);
    mr_strerror(MR_EINVAL);
    mr_strerror(MR_ENOSYS);
    mr_strerror(MR_ETIMEDOUT);
    mr_strerror(-99999);
}

/* -------------------------------------------------------------------------- */
/* Test: strncpy copies at most n bytes                                       */
MR_TEST(string, strncpy_trunc) {
    const char long_src[] = "long_string";
    const char short_src[] = "str";
    char buf[8];

    mr_strncpy(buf, long_src, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    MR_TEST_ASSERT_EQ_INT(mr_memcmp(buf, "long_st", sizeof(buf)), 0);

    mr_strncpy(buf, short_src, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    MR_TEST_ASSERT_EQ_INT(mr_memcmp(buf, "str", 4), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: strcmp / strncmp compare lexicographically                           */
MR_TEST(string, strcmp_strncmp) {
    const char a[] = "abc";
    const char b[] = "abd";
    const char c[] = {'a', '\0', 'x', '\0'};
    const char d[] = {'a', '\0', 'y', '\0'};

    MR_TEST_ASSERT_LT_INT(mr_strcmp(a, b), 0);
    MR_TEST_ASSERT_EQ_INT(mr_strncmp(a, b, 2), 0);
    MR_TEST_ASSERT_LT_INT(mr_strncmp(a, b, 3), 0);
    MR_TEST_ASSERT_EQ_INT(mr_strncmp(c, d, 3), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: strlen / strnlen return correct lengths                              */
MR_TEST(string, strlen_strnlen) {
    const char s[] = "0123456789";

    MR_TEST_ASSERT_EQ_UINT(mr_strlen(s), 10);
    MR_TEST_ASSERT_EQ_UINT(mr_strnlen(s, 5), 5);
    MR_TEST_ASSERT_EQ_UINT(mr_strnlen(s, 20), 10);
}

/* -------------------------------------------------------------------------- */
/* Test: strchr locates first occurrence of character                         */
MR_TEST(string, strchr_find) {
    const char s[] = "test_string";

    MR_TEST_ASSERT_EQ_PTR(mr_strchr(s, 's'), s + 2);
    MR_TEST_ASSERT_EQ_PTR(mr_strchr(s, 't'), s);
    MR_TEST_ASSERT_EQ_PTR(mr_strchr(s, 'X'), MR_NULL);
}

/* -------------------------------------------------------------------------- */
/* Test: ffs32 basic cases                                                 */
MR_TEST(string, ffs32_basic) {
    MR_TEST_ASSERT_EQ_INT(mr_ffs32(0), 0);
    MR_TEST_ASSERT_EQ_INT(mr_ffs32(1), 1);
    MR_TEST_ASSERT_EQ_INT(mr_ffs32(0x80000000U), 32);
    MR_TEST_ASSERT_EQ_INT(mr_ffs32(0x00010000U), 17);
    MR_TEST_ASSERT_EQ_INT(mr_ffs32(0x000000FFU), 1);
}

/* -------------------------------------------------------------------------- */
/* Test: ffs64 basic cases                                                 */
MR_TEST(string, ffs64_basic) {
    MR_TEST_ASSERT_EQ_INT(mr_ffs64(0), 0);
    MR_TEST_ASSERT_EQ_INT(mr_ffs64(1ULL), 1);
    MR_TEST_ASSERT_EQ_INT(mr_ffs64(0x8000000000000000ULL), 64);
    MR_TEST_ASSERT_EQ_INT(mr_ffs64(0x0000000100000000ULL), 33);
    MR_TEST_ASSERT_EQ_INT(mr_ffs64(0x00000000000000FFULL), 1);
}

/* -------------------------------------------------------------------------- */
/* Test: fls32 basic cases                                                 */
MR_TEST(string, fls32_basic) {
    MR_TEST_ASSERT_EQ_INT(mr_fls32(0), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fls32(1), 1);
    MR_TEST_ASSERT_EQ_INT(mr_fls32(0x80000000U), 32);
    MR_TEST_ASSERT_EQ_INT(mr_fls32(0x00010000U), 17);
    MR_TEST_ASSERT_EQ_INT(mr_fls32(0x000000FFU), 8);
}

/* -------------------------------------------------------------------------- */
/* Test: fls64 basic cases                                                 */
MR_TEST(string, fls64_basic) {
    MR_TEST_ASSERT_EQ_INT(mr_fls64(0), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fls64(1ULL), 1);
    MR_TEST_ASSERT_EQ_INT(mr_fls64(0x8000000000000000ULL), 64);
    MR_TEST_ASSERT_EQ_INT(mr_fls64(0x0000000100000000ULL), 33);
    MR_TEST_ASSERT_EQ_INT(mr_fls64(0x00000000000000FFULL), 8);
}

/* -------------------------------------------------------------------------- */
/* Test: zero-size memory operations leave buffers unchanged                  */
MR_TEST(string, zero_size) {
    char src[8];
    char dst[8];

    canary_fill(src, sizeof(src));
    canary_fill(dst, sizeof(dst));
    MR_TEST_ASSERT_EQ_PTR(mr_memcpy(dst, src, 0), dst);
    MR_TEST_ASSERT_EQ_INT(canary_verify(dst, sizeof(dst)), 1);
    MR_TEST_ASSERT_EQ_PTR(mr_memset(dst, 0x5A, 0), dst);
    MR_TEST_ASSERT_EQ_INT(canary_verify(dst, sizeof(dst)), 1);
    MR_TEST_ASSERT_EQ_INT(mr_memcmp(src, dst, 0), 0);
    MR_TEST_ASSERT_EQ_PTR(mr_memchr(src, src[0], 0), MR_NULL);
    MR_TEST_ASSERT_EQ_PTR(mr_memccpy(dst, src, src[0], 0), MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_strncmp("abc", "abd", 0), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_strnlen("abc", 0), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: memcmp reports the sign of the first differing byte                  */
MR_TEST(string, memcmp_order) {
    const char a[] = {0, 1, 2, 3};
    const char b[] = {0, 1, 2, 4};

    MR_TEST_ASSERT_LT_INT(mr_memcmp(a, b, sizeof(a)), 0);
    MR_TEST_ASSERT_GT_INT(mr_memcmp(b, a, sizeof(a)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_memcmp(a, b, 3), 0);
}

/* -------------------------------------------------------------------------- */
/* Test: memccpy copies the whole range when the sentinel is absent           */
MR_TEST(string, memccpy_not_found) {
    const char src[] = "abcd";
    char dst[8];
    void *e;

    mr_memset(dst, 0xAA, sizeof(dst));
    e = mr_memccpy(dst, src, 'z', 4);
    MR_TEST_ASSERT_EQ_PTR(e, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_memcmp(dst, src, 4), 0);
    MR_TEST_ASSERT_EQ_INT((unsigned char)dst[4], 0xAA);
}

/* -------------------------------------------------------------------------- */
/* Test: strncpy pads the remainder with NULs                                 */
MR_TEST(string, strncpy_pad) {
    char buf[8];
    mr_size_t i;

    mr_memset(buf, 0xAA, sizeof(buf));
    MR_TEST_ASSERT_EQ_PTR(mr_strncpy(buf, "hi", sizeof(buf)), buf);
    MR_TEST_ASSERT_EQ_INT(buf[0], 'h');
    MR_TEST_ASSERT_EQ_INT(buf[1], 'i');
    for (i = 2; i < sizeof(buf); i++) {
        MR_TEST_ASSERT_EQ_INT(buf[i], '\0');
    }
}

/* -------------------------------------------------------------------------- */
/* Test: empty strings and strchr on the terminator                           */
MR_TEST(string, empty_and_terminator) {
    const char s[] = "ab";
    const char empty[] = "";
    char buf[4];

    MR_TEST_ASSERT_EQ_UINT(mr_strlen(empty), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_strnlen(empty, 4), 0);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(empty, ""), 0);
    MR_TEST_ASSERT_GT_INT(mr_strcmp("b", "a"), 0);
    MR_TEST_ASSERT_EQ_PTR(mr_strcpy(buf, empty), buf);
    MR_TEST_ASSERT_EQ_INT(buf[0], '\0');
    MR_TEST_ASSERT_EQ_PTR(mr_strchr(s, '\0'), s + 2);
    MR_TEST_ASSERT_EQ_PTR(mr_strchr(empty, '\0'), empty);
}

/* -------------------------------------------------------------------------- */
/* Test: strerror returns a non-empty string for known and unknown codes      */
MR_TEST(string, strerror_text) {
    MR_TEST_ASSERT_NE_PTR(mr_strerror(0), MR_NULL);
    MR_TEST_ASSERT_NE_PTR(mr_strerror(MR_EPERM), MR_NULL);
    MR_TEST_ASSERT_NE_PTR(mr_strerror(MR_ELOOP), MR_NULL);
    MR_TEST_ASSERT_NE_PTR(mr_strerror(-99999), MR_NULL);
    MR_TEST_ASSERT_GT_UINT(mr_strlen(mr_strerror(0)), 0);
    MR_TEST_ASSERT_GT_UINT(mr_strlen(mr_strerror(-99999)), 0);
#if !defined(MR_USE_STRING_STD)
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(mr_strerror(0), "Success"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(mr_strerror(-99999), "Unknown error"), 0);
#endif /* !defined(MR_USE_STRING_STD) */
}

/* -------------------------------------------------------------------------- */
/* Test: ffs/fls for every single-bit position                                */
MR_TEST(string, ffs_fls_all_bits) {
    int i;

    for (i = 0; i < 32; i++) {
        MR_TEST_ASSERT_EQ_INT(mr_ffs32(1U << i), i + 1);
        MR_TEST_ASSERT_EQ_INT(mr_fls32(1U << i), i + 1);
    }
    for (i = 0; i < 64; i++) {
        MR_TEST_ASSERT_EQ_INT(mr_ffs64(1ULL << i), i + 1);
        MR_TEST_ASSERT_EQ_INT(mr_fls64(1ULL << i), i + 1);
    }
    MR_TEST_ASSERT_EQ_INT(mr_ffs32(0x12U), 2);
    MR_TEST_ASSERT_EQ_INT(mr_fls32(0x12U), 5);
    MR_TEST_ASSERT_EQ_INT(mr_ffs64(0x12ULL), 2);
    MR_TEST_ASSERT_EQ_INT(mr_fls64(0x12ULL), 5);
}

#endif /* defined(MR_USE_TEST) */
