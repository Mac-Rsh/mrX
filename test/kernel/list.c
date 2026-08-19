/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_list.h>

/* Helpers */

typedef struct test_node {
    mr_list_t link;
    int id;
} test_node_t;

/* Tests */

MR_TEST(list, static_init) {
    static mr_list_t head = MR_LIST_INIT(&head);

    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&head), MR_TRUE);
    MR_TEST_ASSERT_EQ_PTR(head.next, &head);
    MR_TEST_ASSERT_EQ_PTR(head.prev, &head);
}

MR_TEST(list, init_empty) {
    mr_list_t head;

    mr_list_init(&head);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&head), MR_TRUE);
}

MR_TEST(list, add) {
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

MR_TEST(list, insert) {
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

MR_TEST(list, del) {
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

MR_TEST(list, replace) {
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

MR_TEST(list, move) {
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

MR_TEST(list, safe_foreach_del) {
    test_node_t nodes[4];
    test_node_t *node;
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
        node = MR_LIST_ENTRY(pos, test_node_t, link);
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

MR_TEST(list, single_entry_lifecycle) {
    test_node_t node;
    mr_list_t head;
    mr_list_t other;

    mr_list_init(&head);
    mr_list_init(&other);
    node.id = 7;

    mr_list_add(&head, &node.link);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&head), MR_FALSE);
    MR_TEST_ASSERT_EQ_PTR(head.next, &node.link);
    MR_TEST_ASSERT_EQ_PTR(head.prev, &node.link);

    mr_list_del(&node.link);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&head), MR_TRUE);
    MR_TEST_ASSERT_EQ_PTR(node.link.next, &node.link);
    MR_TEST_ASSERT_EQ_PTR(node.link.prev, &node.link);

    mr_list_move(&node.link, &other);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&other), MR_FALSE);
    MR_TEST_ASSERT_EQ_PTR(other.next, &node.link);
    mr_list_del(&node.link);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&other), MR_TRUE);
}

MR_TEST(list, replace_single_entry) {
    test_node_t old_node;
    test_node_t new_node;
    mr_list_t head;

    mr_list_init(&head);
    old_node.id = 1;
    new_node.id = 2;
    mr_list_add(&head, &old_node.link);
    mr_list_replace(&new_node.link, &old_node.link);

    MR_TEST_ASSERT_EQ_PTR(head.next, &new_node.link);
    MR_TEST_ASSERT_EQ_PTR(head.prev, &new_node.link);
    MR_TEST_ASSERT_EQ_PTR(old_node.link.next, &old_node.link);
    MR_TEST_ASSERT_EQ_PTR(old_node.link.prev, &old_node.link);
    mr_list_del(&new_node.link);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&head), MR_TRUE);
}

MR_TEST(list, array_init) {
    mr_list_t lists[4] = MR_LISTS_INIT(lists, 4);
    mr_size_t i;

    for (i = 0; i < MR_ARRAY_SIZE(lists); i++) {
        MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&lists[i]), MR_TRUE);
        MR_TEST_ASSERT_EQ_PTR(lists[i].next, &lists[i]);
        MR_TEST_ASSERT_EQ_PTR(lists[i].prev, &lists[i]);
    }
}

MR_TEST(list, add_insert_order) {
    test_node_t n1, n2, n3;
    test_node_t *n;
    mr_list_t head;
    mr_list_t *pos;
    int ids[3];
    int idx;

    mr_list_init(&head);
    n1.id = 1;
    n2.id = 2;
    n3.id = 3;
    mr_list_add(&head, &n1.link);
    mr_list_insert(&head, &n2.link);
    mr_list_add(&head, &n3.link);

    idx = 0;
    MR_LIST_FOR_EACH(pos, &head) {
        n = MR_LIST_ENTRY(pos, test_node_t, link);
        ids[idx++] = n->id;
    }
    MR_TEST_ASSERT_EQ_INT(idx, 3);
    MR_TEST_ASSERT_EQ_INT(ids[0], 2);
    MR_TEST_ASSERT_EQ_INT(ids[1], 1);
    MR_TEST_ASSERT_EQ_INT(ids[2], 3);
}

MR_TEST(list, empty_foreach) {
    mr_list_t head;
    mr_list_t *pos;
    int count;

    mr_list_init(&head);
    count = 0;
    MR_LIST_FOR_EACH(pos, &head) {
        count++;
    }
    MR_TEST_ASSERT_EQ_INT(count, 0);
}

MR_TEST(list, del_all) {
    test_node_t n1, n2;
    mr_list_t head;

    mr_list_init(&head);
    n1.id = 1;
    n2.id = 2;
    mr_list_add(&head, &n1.link);
    mr_list_add(&head, &n2.link);
    mr_list_del(&n1.link);
    mr_list_del(&n2.link);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&head), MR_TRUE);
    MR_TEST_ASSERT_EQ_PTR(head.next, &head);
    MR_TEST_ASSERT_EQ_PTR(head.prev, &head);
}

#endif /* defined(MR_USE_TEST) */
