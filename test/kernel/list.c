/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mr-X/mr_list.h>

typedef struct test_node {
    mr_list_t link;
    int id;
} test_node_t;

/* -------------------------------------------------------------------------- */
/* Test: static init                                                          */
static void test_list_static_init(void) {
    static mr_list_t head = MR_LIST_INIT(&head);

    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&head), MR_TRUE);
    MR_TEST_ASSERT_EQ_PTR(head.next, &head);
    MR_TEST_ASSERT_EQ_PTR(head.prev, &head);
}
MR_TEST_EXPORT(list, static_init, test_list_static_init);

/* -------------------------------------------------------------------------- */
/* Test: init/empty                                                           */
static void test_list_init_empty(void) {
    mr_list_t head;

    mr_list_init(&head);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&head), MR_TRUE);
}
MR_TEST_EXPORT(list, init_empty, test_list_init_empty);

/* -------------------------------------------------------------------------- */
/* Test: add elements                                                         */
static void test_list_add(void) {
    test_node_t n1, n2, n3;
    int ids[3], idx;
    test_node_t *n;
    mr_list_t *pos;
    mr_list_t head;

    mr_list_init(&head);
    n1.id = 1;
    n2.id = 2;
    n3.id = 3;
    mr_list_add(&head, &n1.link);
    mr_list_add(&head, &n2.link);
    mr_list_add(&head, &n3.link);

    idx = 0;
    MR_LIST_FOR_EACH(pos, &head) {
        n = MR_LIST_ENTRY(pos, test_node_t, link);
        ids[idx++] = n->id;
    }
    MR_TEST_ASSERT_EQ_INT(ids[0], 1);
    MR_TEST_ASSERT_EQ_INT(ids[1], 2);
    MR_TEST_ASSERT_EQ_INT(ids[2], 3);
}
MR_TEST_EXPORT(list, add, test_list_add);

/* -------------------------------------------------------------------------- */
/* Test: insert to head                                                       */
static void test_list_insert(void) {
    test_node_t n1, n2;
    test_node_t *n;
    mr_list_t head;
    mr_list_t *pos;

    mr_list_init(&head);
    n1.id = 1;
    n2.id = 2;
    mr_list_insert(&head, &n1.link);
    mr_list_insert(&head, &n2.link);

    pos = head.next;
    n = MR_LIST_ENTRY(pos, test_node_t, link);
    MR_TEST_ASSERT_EQ_INT(n->id, 2);
    pos = pos->next;
    n = MR_LIST_ENTRY(pos, test_node_t, link);
    MR_TEST_ASSERT_EQ_INT(n->id, 1);
}
MR_TEST_EXPORT(list, insert, test_list_insert);

/* -------------------------------------------------------------------------- */
/* Test: remove element                                                       */
static void test_list_del(void) {
    test_node_t n1, n2;
    test_node_t *n;
    mr_list_t head;

    mr_list_init(&head);
    n1.id = 1;
    n2.id = 2;
    mr_list_add(&head, &n1.link);
    mr_list_add(&head, &n2.link);

    mr_list_del(&n1.link);
    n = MR_LIST_ENTRY(head.next, test_node_t, link);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&head), 0);
    MR_TEST_ASSERT_EQ_INT(n->id, 2);
}
MR_TEST_EXPORT(list, del, test_list_del);

/* -------------------------------------------------------------------------- */
/* Test: replace element                                                      */
static void test_list_replace(void) {
    test_node_t n1, n2, n3;
    test_node_t *n;
    mr_list_t head;
    mr_list_t *pos;

    mr_list_init(&head);
    n1.id = 1;
    n2.id = 2;
    n3.id = 3;
    mr_list_add(&head, &n1.link);
    mr_list_add(&head, &n2.link);

    mr_list_replace(&n3.link, &n1.link);

    pos = head.next;
    n = MR_LIST_ENTRY(pos, test_node_t, link);
    MR_TEST_ASSERT_EQ_INT(n->id, 3);
    pos = pos->next;
    n = MR_LIST_ENTRY(pos, test_node_t, link);
    MR_TEST_ASSERT_EQ_INT(n->id, 2);
}
MR_TEST_EXPORT(list, replace, test_list_replace);

/* -------------------------------------------------------------------------- */
/* Test: move element                                                         */
static void test_list_move(void) {
    mr_list_t head1, head2;
    test_node_t n1;
    test_node_t *n;

    mr_list_init(&head1);
    mr_list_init(&head2);
    n1.id = 42;
    mr_list_add(&head1, &n1.link);

    mr_list_move(&n1.link, &head2);
    n = MR_LIST_ENTRY(head2.next, test_node_t, link);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&head1), 1);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&head2), 0);
    MR_TEST_ASSERT_EQ_INT(n->id, 42);
}
MR_TEST_EXPORT(list, move, test_list_move);

/* -------------------------------------------------------------------------- */
/* Test: safe traversal and delete */
static void test_list_safe_foreach_del(void) {
    test_node_t nodes[4];
    mr_list_t *pos, *l;
    int i, ids[2], idx;
    mr_list_t head;
    test_node_t *n;

    mr_list_init(&head);
    for (i = 0; i < 4; ++i) {
        nodes[i].id = i;
        mr_list_add(&head, &nodes[i].link);
    }

    MR_LIST_FOR_EACH_SAFE(pos, l, &head) {
        test_node_t *node = MR_LIST_ENTRY(pos, test_node_t, link);
        if ((node->id % 2) == 0) {
            mr_list_del(pos);
        }
    }

    idx = 0;
    MR_LIST_FOR_EACH(pos, &head) {
        n = MR_LIST_ENTRY(pos, test_node_t, link);
        ids[idx++] = n->id;
    }
    MR_TEST_ASSERT_EQ_INT(idx, 2);
    MR_TEST_ASSERT_EQ_INT(ids[0], 1);
    MR_TEST_ASSERT_EQ_INT(ids[1], 3);
}
MR_TEST_EXPORT(list, safe_foreach_del, test_list_safe_foreach_del);
#endif /* defined(MR_USE_TEST) */
