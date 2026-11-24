/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_STRING)
#include <mrX/libc/mr_string.h>
#if defined(MR_USE_MALLOC)
#include <mrX/libc/mr_malloc.h>
#endif /* defined(MR_USE_MALLOC) */

static int bytes_first_diff(const char *actual, const char *expected,
                            mr_size_t size) {
    mr_size_t i;

    for (i = 0; i < size; i++) {
        if ((mr_uint8_t)actual[i] != (mr_uint8_t)expected[i]) {
            return (int)i;
        }
    }
    return -1;
}

static void range_reference(mr_uint8_t *expected, const mr_uint8_t *base,
                            mr_size_t total, mr_size_t dst_off,
                            mr_size_t src_off, mr_size_t size) {
    mr_size_t i;

    for (i = 0; i < total; i++) {
        expected[i] = base[i];
    }
    for (i = 0; i < size; i++) {
        expected[dst_off + i] = base[src_off + i];
    }
}

static void fill_reference(mr_uint8_t *expected, const mr_uint8_t *base,
                           mr_size_t total, mr_size_t offset, mr_size_t size,
                           mr_uint8_t value) {
    mr_size_t i;

    for (i = 0; i < total; i++) {
        expected[i] = base[i];
    }
    for (i = 0; i < size; i++) {
        expected[offset + i] = value;
    }
}

MR_TEST(string, strncpy_exact_fit) {
    char buf[9];
    char guard;

    guard = (char)0xA5;
    mr_memset(buf, 0xAA, sizeof(buf));
    buf[8] = guard;
    MR_TEST_EXPECT_EQ_PTR(mr_strncpy(buf, "abcdefg", 8), buf);
    MR_TEST_EXPECT_EQ_INT(bytes_first_diff(buf, "abcdefg\0", 8), -1);
    /* One byte past the limit must stay untouched */
    MR_TEST_EXPECT_EQ_INT((mr_uint8_t)buf[8], 0xA5);
}

MR_TEST(string, strncpy_truncate_guard) {
    char buf[8];

    mr_memset(buf, 0xAA, sizeof(buf));
    MR_TEST_EXPECT_EQ_PTR(mr_strncpy(buf, "abcdefghij", 5), buf);
    MR_TEST_EXPECT_EQ_INT(bytes_first_diff(buf, "abcde", 5), -1);
    /* Bytes beyond the limit keep the old pattern */
    MR_TEST_EXPECT_EQ_INT((mr_uint8_t)buf[5], 0xAA);
    MR_TEST_EXPECT_EQ_INT((mr_uint8_t)buf[6], 0xAA);
    MR_TEST_EXPECT_EQ_INT((mr_uint8_t)buf[7], 0xAA);
}

MR_TEST(string, strncpy_zero_and_embedded_nul) {
    const char src[] = {'a', '\0', 'b', 'c'};
    char buf[6];

    /* Zero size leaves the destination untouched */
    mr_memset(buf, 0x5A, sizeof(buf));
    MR_TEST_EXPECT_EQ_PTR(mr_strncpy(buf, "xyz", 0), buf);
    MR_TEST_EXPECT_EQ_INT((mr_uint8_t)buf[0], 0x5A);
    MR_TEST_EXPECT_EQ_INT((mr_uint8_t)buf[5], 0x5A);

    /* The embedded terminator stops the copy and pads with NULs */
    MR_TEST_EXPECT_EQ_PTR(mr_strncpy(buf, src, 4), buf);
    MR_TEST_EXPECT_EQ_INT(buf[0], 'a');
    MR_TEST_EXPECT_EQ_INT(buf[1], '\0');
    MR_TEST_EXPECT_EQ_INT(buf[2], '\0');
    MR_TEST_EXPECT_EQ_INT(buf[3], '\0');
}

MR_TEST(string, memccpy_match_positions) {
    const char src[] = {'x', 'y', 'z', 'w'};
    char dst[8];
    void *e;

    /* Match at the first byte */
    mr_memset(dst, 0xCC, sizeof(dst));
    e = mr_memccpy(dst, src, 'x', 4);
    MR_TEST_EXPECT_EQ_PTR(e, dst + 1);
    MR_TEST_EXPECT_EQ_INT(bytes_first_diff(dst, "x", 1), -1);
    MR_TEST_EXPECT_EQ_INT((mr_uint8_t)dst[1], 0xCC);

    /* Match in the middle */
    mr_memset(dst, 0xCC, sizeof(dst));
    e = mr_memccpy(dst, src, 'z', 4);
    MR_TEST_EXPECT_EQ_PTR(e, dst + 3);
    MR_TEST_EXPECT_EQ_INT(bytes_first_diff(dst, "xyz", 3), -1);
    MR_TEST_EXPECT_EQ_INT((mr_uint8_t)dst[3], 0xCC);

    /* Match at the last byte of the range */
    mr_memset(dst, 0xCC, sizeof(dst));
    e = mr_memccpy(dst, src, 'w', 4);
    MR_TEST_EXPECT_EQ_PTR(e, dst + 4);
    MR_TEST_EXPECT_EQ_INT(bytes_first_diff(dst, "xyzw", 4), -1);
    MR_TEST_EXPECT_EQ_INT((mr_uint8_t)dst[4], 0xCC);
}

MR_TEST(string, memccpy_raw_copy) {
    const char src[] = {'a', 'b', '\0', 'd', 'e', 'f'};
    char dst[8];
    void *e;

    /* No sentinel: exactly size bytes are copied and MR_NULL returned */
    mr_memset(dst, 0xCC, sizeof(dst));
    e = mr_memccpy(dst, "abcdef", 'z', 6);
    MR_TEST_EXPECT_EQ_PTR(e, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(bytes_first_diff(dst, "abcdef", 6), -1);
    MR_TEST_EXPECT_EQ_INT((mr_uint8_t)dst[6], 0xCC);

    /* Bytes beyond an embedded NUL are still copied */
    e = mr_memccpy(dst, src, 'z', sizeof(src));
    MR_TEST_EXPECT_EQ_PTR(e, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(bytes_first_diff(dst, "ab\0def", 6), -1);
}

MR_TEST(string, memcmp_range_edges) {
    const char a[] = {0, 'a', 0, 'b'};
    const char b[] = {0, 'a', 0, 'b'};
    const char c[] = {0, 'a', 0, 'c'};
    const char low[] = {(char)0x7F};
    const char high[] = {(char)0x80};

    MR_TEST_EXPECT_EQ_INT(mr_memcmp(a, b, sizeof(a)), 0);
    /* Difference only beyond the compared size is ignored */
    MR_TEST_EXPECT_EQ_INT(mr_memcmp(a, c, 3), 0);
    MR_TEST_EXPECT_LT_INT(mr_memcmp(a, c, sizeof(a)), 0);
    MR_TEST_EXPECT_GT_INT(mr_memcmp(c, a, sizeof(a)), 0);
    /* Byte order is unsigned even for high bit bytes */
    MR_TEST_EXPECT_LT_INT(mr_memcmp(low, high, 1), 0);
    MR_TEST_EXPECT_GT_INT(mr_memcmp(high, low, 1), 0);
}

MR_TEST(string, strcmp_prefix_and_high_bit) {
    const char x[] = {'a', 'b', (char)0x80, '\0'};
    const char y[] = {'a', 'b', (char)0x80, '\0'};

    MR_TEST_EXPECT_EQ_INT(mr_strcmp("abc", "abc"), 0);
    MR_TEST_EXPECT_LT_INT(mr_strcmp("abc", "abcdef"), 0);
    MR_TEST_EXPECT_GT_INT(mr_strcmp("abcdef", "abc"), 0);
    MR_TEST_EXPECT_LT_INT(mr_strcmp("", "a"), 0);
    MR_TEST_EXPECT_GT_INT(mr_strcmp("a", ""), 0);
    MR_TEST_EXPECT_EQ_INT(mr_strcmp(x, y), 0);
    MR_TEST_EXPECT_LT_INT(mr_strcmp("ab", x), 0);
}

MR_TEST(string, strncmp_edges) {
    const char abd[] = {'a', 'b', '\0', 'd'};

    MR_TEST_EXPECT_EQ_INT(mr_strncmp("abc", "abd", 0), 0);
    MR_TEST_EXPECT_EQ_INT(mr_strncmp("abc", "abd", 2), 0);
    MR_TEST_EXPECT_LT_INT(mr_strncmp("abc", "abd", 3), 0);
    /* The difference at index 2 is found before either terminator */
    MR_TEST_EXPECT_LT_INT(mr_strncmp("abc", "abd", 4), 0);
    MR_TEST_EXPECT_EQ_INT(mr_strncmp("ab", abd, 3), 0);
    MR_TEST_EXPECT_LT_INT(mr_strncmp("", "a", 1), 0);
}

MR_TEST(string, strnlen_clamp) {
    const char s[] = "hello";
    const char embedded[] = {'a', 'b', '\0', 'c', 'd'};

    MR_TEST_EXPECT_EQ_UINT(mr_strnlen(s, 0), 0);
    MR_TEST_EXPECT_EQ_UINT(mr_strnlen(s, 4), 4);
    MR_TEST_EXPECT_EQ_UINT(mr_strnlen(s, 5), 5);
    MR_TEST_EXPECT_EQ_UINT(mr_strnlen(s, 6), 5);
    MR_TEST_EXPECT_EQ_UINT(mr_strnlen(s, 100), 5);
    MR_TEST_EXPECT_EQ_UINT(mr_strnlen("", 1), 0);
    MR_TEST_EXPECT_EQ_UINT(mr_strnlen(embedded, sizeof(embedded)), 2);
    MR_TEST_EXPECT_EQ_UINT(mr_strnlen(embedded, 2), 2);
}

MR_TEST(string, memmove_self_abut_overlap) {
    enum {
        BUFFER_SIZE = 96
    };
    static const struct {
        mr_size_t dst_off;
        mr_size_t src_off;
        mr_size_t size;
    } cases[] = {
        {16, 16, 32},
        {40,  8, 24},
        {32,  8, 24},
        {31,  8, 24},
        {16,  8, 24},
        {40,  8, 40},
        {44,  4, 20}
    };
    union {
        mr_uintptr_t align;
        mr_uint8_t bytes[BUFFER_SIZE];
    } storage;
    mr_uint8_t expected[BUFFER_SIZE];
    mr_size_t i, j;
    void *ret;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        for (j = 0; j < BUFFER_SIZE; j++) {
            storage.bytes[j] = (mr_uint8_t)(j * 37U + 0x80U);
        }
        range_reference(expected, storage.bytes, BUFFER_SIZE, cases[i].dst_off,
                        cases[i].src_off, cases[i].size);
        ret = mr_memmove(storage.bytes + cases[i].dst_off,
                         storage.bytes + cases[i].src_off, cases[i].size);
        MR_TEST_EXPECT_EQ_PTR(ret, storage.bytes + cases[i].dst_off);
        MR_TEST_EXPECT_EQ_INT(bytes_first_diff((const char *)storage.bytes,
                                               (const char *)expected,
                                               BUFFER_SIZE),
                              -1);
    }
}

MR_TEST(string, memmove_aligned_word_overlap) {
    enum {
        BUFFER_SIZE = 96
    };
    static const mr_size_t sizes[] = {8, 16, 24, 32, 48, 64};
    union {
        mr_uintptr_t align;
        mr_uint8_t bytes[BUFFER_SIZE];
    } storage;
    mr_uint8_t expected[BUFFER_SIZE];
    mr_size_t i, k;
    void *ret;

    /* Forward into higher addresses with identical alignment */
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        for (k = 0; k < BUFFER_SIZE; k++) {
            storage.bytes[k] = (mr_uint8_t)(k * 13U + 5U);
        }
        range_reference(expected, storage.bytes, BUFFER_SIZE, 16, 8, sizes[i]);
        ret = mr_memmove(storage.bytes + 16, storage.bytes + 8, sizes[i]);
        MR_TEST_EXPECT_EQ_PTR(ret, storage.bytes + 16);
        MR_TEST_EXPECT_EQ_INT(bytes_first_diff((const char *)storage.bytes,
                                               (const char *)expected,
                                               BUFFER_SIZE),
                              -1);
    }

    /* Backward into lower addresses with identical alignment */
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        for (k = 0; k < BUFFER_SIZE; k++) {
            storage.bytes[k] = (mr_uint8_t)(k * 13U + 5U);
        }
        range_reference(expected, storage.bytes, BUFFER_SIZE, 8, 16, sizes[i]);
        ret = mr_memmove(storage.bytes + 8, storage.bytes + 16, sizes[i]);
        MR_TEST_EXPECT_EQ_PTR(ret, storage.bytes + 8);
        MR_TEST_EXPECT_EQ_INT(bytes_first_diff((const char *)storage.bytes,
                                               (const char *)expected,
                                               BUFFER_SIZE),
                              -1);
    }
}

MR_TEST(string, memcpy_word_boundaries) {
    enum {
        BUFFER_SIZE = 64
    };
    static const mr_size_t sizes[] = {1, 7, 8, 9, 15, 16, 17, 24, 31, 32};
    union {
        mr_uintptr_t align;
        mr_uint8_t bytes[BUFFER_SIZE];
    } source, destination;
    mr_uint8_t expected[BUFFER_SIZE];
    mr_size_t i, k, off;

    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        for (off = 0; off < 2; off++) {
            for (k = 0; k < BUFFER_SIZE; k++) {
                source.bytes[k] = (mr_uint8_t)(k * 7U + 1U);
                destination.bytes[k] = (mr_uint8_t)(k * 11U + 3U);
            }
            /* Offset 0 hits the aligned word path, offset 1 the byte path
               the reference builds on the destination content and copies
               the source range over it */
            for (k = 0; k < BUFFER_SIZE; k++) {
                expected[k] = destination.bytes[k];
            }
            for (k = 0; k < sizes[i]; k++) {
                expected[off + k] = source.bytes[off + k];
            }
            MR_TEST_EXPECT_EQ_PTR(mr_memcpy(destination.bytes + off,
                                            source.bytes + off, sizes[i]),
                                  destination.bytes + off);
            MR_TEST_EXPECT_EQ_INT(
                bytes_first_diff((const char *)destination.bytes,
                                 (const char *)expected, BUFFER_SIZE),
                -1);
        }
    }
}

MR_TEST(string, memset_value_truncation) {
    enum {
        BUFFER_SIZE = 64
    };
    static const int values[] = {0x00, 0xFF, 0x80, 0x100, -1, 0x1FF};
    static const mr_size_t offsets[] = {0, 1, 5, 7, 8, 9, 15};
    static const mr_size_t sizes[] = {1, 3, 8, 9, 16, 17};
    union {
        mr_uintptr_t align;
        mr_uint8_t bytes[BUFFER_SIZE];
    } storage;
    mr_uint8_t expected[BUFFER_SIZE];
    mr_size_t v, o, s, i;
    void *ret;

    for (v = 0; v < sizeof(values) / sizeof(values[0]); v++) {
        for (o = 0; o < sizeof(offsets) / sizeof(offsets[0]); o++) {
            for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
                if (offsets[o] + sizes[s] > BUFFER_SIZE) {
                    continue;
                }
                for (i = 0; i < BUFFER_SIZE; i++) {
                    storage.bytes[i] = (mr_uint8_t)(i * 19U + 9U);
                }
                fill_reference(expected, storage.bytes, BUFFER_SIZE, offsets[o],
                               sizes[s], (mr_uint8_t)values[v]);
                ret = mr_memset(storage.bytes + offsets[o], values[v],
                                sizes[s]);
                MR_TEST_EXPECT_EQ_PTR(ret, storage.bytes + offsets[o]);
                MR_TEST_EXPECT_EQ_INT(
                    bytes_first_diff((const char *)storage.bytes,
                                     (const char *)expected, BUFFER_SIZE),
                    -1);
            }
        }
    }
}

MR_TEST(string, memchr_positions) {
    const char buf[] = {'a', 'b', 'c', 'd', 'b'};
    const char high[] = {0x01, (char)0xFF, 0x02, (char)0xFF};

    MR_TEST_EXPECT_EQ_PTR(mr_memchr(buf, 'a', 5), buf);
    MR_TEST_EXPECT_EQ_PTR(mr_memchr(buf, 'b', 5), buf + 1);
    /* Only the first match is returned */
    MR_TEST_EXPECT_NE_PTR(mr_memchr(buf, 'b', 5), buf + 4);
    MR_TEST_EXPECT_EQ_PTR(mr_memchr(buf, 'd', 5), buf + 3);
    MR_TEST_EXPECT_EQ_PTR(mr_memchr(buf, 'z', 5), MR_NULL);
    /* Negative and oversized values are truncated to one byte */
    MR_TEST_EXPECT_EQ_PTR(mr_memchr(high, -1, 4), high + 1);
    /* 0x102 truncates to 0x02 which sits at index 2 */
    MR_TEST_EXPECT_EQ_PTR(mr_memchr(high, 0x102, 4), high + 2);
    MR_TEST_EXPECT_EQ_PTR(mr_memchr(high, 0x102, 2), MR_NULL);
}

#if defined(MR_USE_MALLOC)
MR_TEST(string, strcpy_long_and_empty) {
    char src[201];
    char *dst;
    mr_size_t i;

    dst = mr_malloc(256);
    if (dst == MR_NULL) {
        MR_TEST_EXPECT_NE_PTR(dst, MR_NULL);
        return;
    }

    for (i = 0; i < 200; i++) {
        src[i] = (char)('A' + (i % 26));
    }
    src[200] = '\0';
    mr_memset(dst, 0x5A, 256);
    MR_TEST_EXPECT_EQ_PTR(mr_strcpy(dst, src), dst);
    MR_TEST_EXPECT_EQ_INT(mr_strcmp(dst, src), 0);
    MR_TEST_EXPECT_EQ_UINT(mr_strlen(dst), 200);
    MR_TEST_EXPECT_EQ_INT(dst[200], '\0');
    /* The copy stops exactly at the terminator */
    MR_TEST_EXPECT_EQ_INT((mr_uint8_t)dst[201], 0x5A);

    MR_TEST_EXPECT_EQ_PTR(mr_strcpy(dst, ""), dst);
    MR_TEST_EXPECT_EQ_INT(dst[0], '\0');
    mr_free(dst);
}
#endif /* defined(MR_USE_MALLOC) */

MR_TEST(string, strchr_positions) {
    const char text[] = {'q', (char)0x80, 'r', '\0', 's'};

    MR_TEST_EXPECT_EQ_PTR(mr_strchr(text, 'q'), text);
    MR_TEST_EXPECT_EQ_PTR(mr_strchr(text, (char)0x80), text + 1);
    MR_TEST_EXPECT_EQ_PTR(mr_strchr(text, '\0'), text + 3);
    MR_TEST_EXPECT_EQ_PTR(mr_strchr(text, 'z'), MR_NULL);
}

MR_TEST(string, strerror_negative_and_extended) {
    MR_TEST_EXPECT_EQ_INT(mr_strcmp(mr_strerror(-MR_EINVAL), "Invalid argument"),
                          0);
    MR_TEST_EXPECT_EQ_INT(
        mr_strcmp(mr_strerror(MR_EBADF), "Bad file descriptor"), 0);
    MR_TEST_EXPECT_EQ_INT(
        mr_strcmp(mr_strerror(-MR_ERANGE), "Result too large"), 0);
}

MR_TEST(string, memcpy_same_unaligned_offset) {
    mr_uint8_t src[32], dst[32];
    mr_size_t i;

    for (i = 0; i < sizeof(src); i++) {
        src[i] = (mr_uint8_t)(i + 1U);
        dst[i] = 0U;
    }
    MR_TEST_ASSERT_EQ_PTR(mr_memcpy(dst + 1, src + 1, 25), dst + 1);
    for (i = 1; i < 26; i++) {
        MR_TEST_EXPECT_EQ_UINT(dst[i], src[i]);
    }
    MR_TEST_EXPECT_EQ_UINT(dst[0], 0U);
    MR_TEST_EXPECT_EQ_UINT(dst[26], 0U);
}

#endif /* defined(MR_USE_STRING) */
#endif /* defined(MR_USE_TEST) */
