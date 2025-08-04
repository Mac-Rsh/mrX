/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <test/mr_test.h>
#if defined(MR_USE_TEST)
#include <libc/mr_string.h>
#include <libc/mr_malloc.h>

/* -------------------------------------------------------------------------- */
/* Utilities: fill / verify canary pattern                                    */
static void canary_fill(void *p, mr_size_t n) {
    mr_uint8_t *u8;
    mr_size_t i;

    for (u8 = (mr_uint8_t *)p, i = 0; i < n; ++i) {
        u8[i] = (mr_uint8_t)(i & 0xFFU);
    }
}

static int canary_verify(void *p, mr_size_t n) {
    mr_uint8_t *u8;
    mr_size_t i;

    for (u8 = (mr_uint8_t *)p, i = 0; i < n; ++i) {
        if (u8[i] != (mr_uint8_t)(i & 0xFFU)) {
            return 0;
        }
    }
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Test: memcpy correctness                                                   */
static void test_memcpy_basic(void) {
    enum {
        SZ = 256
    };
    char *src, *dst;

    src = mr_malloc(SZ);
    dst = mr_malloc(SZ);
    MR_TEST_ASSERT_NE_PTR(src, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(dst, MR_NULL);

    canary_fill(src, SZ);
    mr_memcpy(dst, src, SZ);
    MR_TEST_ASSERT_EQ_INT(canary_verify(dst, SZ), 1);

    mr_free(src);
    mr_free(dst);
}
MR_TEST_EXPORT(string, memcpy_basic, test_memcpy_basic);

/* -------------------------------------------------------------------------- */
/* Test: memset sets every byte to the given value                            */
static void test_memset_basic(void) {
    enum {
        SZ = 137
    };
    mr_size_t i;
    char *buf;

    buf = mr_malloc(SZ);
    MR_TEST_ASSERT_NE_PTR(buf, MR_NULL);

    mr_memset(buf, 0x5A, SZ);
    for (i = 0; i < SZ; ++i) {
        MR_TEST_ASSERT_EQ_INT(buf[i], 0x5A);
    }

    mr_free(buf);
}
MR_TEST_EXPORT(string, memset_basic, test_memset_basic);

/* -------------------------------------------------------------------------- */
/* Test: memcmp detects differences                                           */
static void test_memcmp_diff(void) {
    const char a[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    const char b[8] = {0, 1, 2, 3, 4, 5, 6, 8};

    MR_TEST_ASSERT_EQ_INT(mr_memcmp(a, a, sizeof(a)), 0);
    MR_TEST_ASSERT_NE_INT(mr_memcmp(a, b, sizeof(a)), 0);
}
MR_TEST_EXPORT(string, memcmp_diff, test_memcmp_diff);

/* -------------------------------------------------------------------------- */
/* Test: memchr finds the first occurrence of a byte                          */
static void test_memchr_find(void) {
    const char buf[16] = "0123456789ABCDEF";

    MR_TEST_ASSERT_EQ_PTR(mr_memchr(buf, '5', sizeof(buf)), buf + 5);
    MR_TEST_ASSERT_EQ_PTR(mr_memchr(buf, 'X', sizeof(buf)), MR_NULL);
}
MR_TEST_EXPORT(string, memchr_find, test_memchr_find);

/* -------------------------------------------------------------------------- */
/* Test: memccpy stops after the sentinel character                           */
static void test_memccpy_stop(void) {
    const char src[16] = "Hello\0World";
    char dst[16];

    mr_memset(dst, '\0', sizeof(dst));
    void *e = mr_memccpy(dst, src, 'o', sizeof(dst));
    MR_TEST_ASSERT_NE_PTR(e, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(dst[4], 'o');
    MR_TEST_ASSERT_EQ_INT(dst[5], '\0');
}
MR_TEST_EXPORT(string, memccpy_stop, test_memccpy_stop);

/* -------------------------------------------------------------------------- */
/* Test: strcpy copies C-string including terminator                          */
static void test_strcpy_basic(void) {
    const char *src = "string_test";
    char *dst;

    dst = mr_malloc(32);
    MR_TEST_ASSERT_NE_PTR(dst, MR_NULL);

    mr_strcpy(dst, src);
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(dst, src), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_strlen(dst), mr_strlen(src));

    mr_free(dst);
}
MR_TEST_EXPORT(string, strcpy_basic, test_strcpy_basic);

/* -------------------------------------------------------------------------- */
/* Test: strncpy copies at most n bytes                                       */
static void test_strncpy_trunc(void) {
    const char *src = "long_string";
    char buf[8];

    mr_strncpy(buf, src, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    MR_TEST_ASSERT_EQ_INT(mr_memcmp(buf, "long_st", sizeof(buf)), 0);
}
MR_TEST_EXPORT(string, strncpy_trunc, test_strncpy_trunc);

/* -------------------------------------------------------------------------- */
/* Test: strcmp / strncmp compare lexicographically                           */
static void test_strcmp_strncmp(void) {
    const char *a = "abc";
    const char *b = "abd";

    MR_TEST_ASSERT_LT_INT(mr_strcmp(a, b), 0);
    MR_TEST_ASSERT_EQ_INT(mr_strncmp(a, b, 2), 0);
    MR_TEST_ASSERT_LT_INT(mr_strncmp(a, b, 3), 0);
}
MR_TEST_EXPORT(string, strcmp_strncmp, test_strcmp_strncmp);

/* -------------------------------------------------------------------------- */
/* Test: strlen / strnlen return correct lengths                              */
static void test_strlen_strnlen(void) {
    const char *s = "0123456789";

    MR_TEST_ASSERT_EQ_UINT(mr_strlen(s), 10);
    MR_TEST_ASSERT_EQ_UINT(mr_strnlen(s, 5), 5);
    MR_TEST_ASSERT_EQ_UINT(mr_strnlen(s, 20), 10);
}
MR_TEST_EXPORT(string, strlen_strnlen, test_strlen_strnlen);

/* -------------------------------------------------------------------------- */
/* Test: strchr locates first occurrence of character                         */
static void test_strchr_find(void) {
    const char *s = "test_string";

    MR_TEST_ASSERT_EQ_PTR(mr_strchr(s, 's'), s + 2);
    MR_TEST_ASSERT_EQ_PTR(mr_strchr(s, 't'), s);
    MR_TEST_ASSERT_EQ_PTR(mr_strchr(s, 'X'), MR_NULL);
}
MR_TEST_EXPORT(string, strchr_find, test_strchr_find);
#endif /* defined(MR_USE_TEST) */
