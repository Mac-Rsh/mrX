/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_list.h>

/* Helpers */

typedef struct test_deep_node {
    mr_list_t link;
    int id;
} test_deep_node_t;

typedef struct test_link_first {
    mr_list_t link;
    int id;
} test_link_first_t;

typedef struct test_link_last {
    int id;
    mr_list_t link;
} test_link_last_t;

/* Tests */

/* Test: every link pointer after two adds is exactly where it belongs */
MR_TEST(list, links_integrity_add) {
    mr_list_t head;
    test_deep_node_t n1;
    test_deep_node_t n2;

    mr_list_init(&head);
    n1.id = 1;
    n2.id = 2;
    mr_list_add(&head, &n1.link);
    mr_list_add(&head, &n2.link);

    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&head), MR_FALSE);
    MR_TEST_ASSERT_EQ_PTR(head.next, &n1.link);
    MR_TEST_ASSERT_EQ_PTR(head.prev, &n2.link);
    MR_TEST_ASSERT_EQ_PTR(n1.link.next, &n2.link);
    MR_TEST_ASSERT_EQ_PTR(n1.link.prev, &head);
    MR_TEST_ASSERT_EQ_PTR(n2.link.next, &head);
    MR_TEST_ASSERT_EQ_PTR(n2.link.prev, &n1.link);
}

/* Test: inserting into an empty list wires all four directions correctly */
MR_TEST(list, links_integrity_insert) {
    mr_list_t head;
    test_deep_node_t n1;
    test_deep_node_t n2;

    mr_list_init(&head);
    n1.id = 1;
    n2.id = 2;

    mr_list_insert(&head, &n1.link);
    MR_TEST_ASSERT_EQ_PTR(head.next, &n1.link);
    MR_TEST_ASSERT_EQ_PTR(head.prev, &n1.link);
    MR_TEST_ASSERT_EQ_PTR(n1.link.next, &head);
    MR_TEST_ASSERT_EQ_PTR(n1.link.prev, &head);

    mr_list_insert(&head, &n2.link);
    MR_TEST_ASSERT_EQ_PTR(head.next, &n2.link);
    MR_TEST_ASSERT_EQ_PTR(head.prev, &n1.link);
    MR_TEST_ASSERT_EQ_PTR(n2.link.next, &n1.link);
    MR_TEST_ASSERT_EQ_PTR(n2.link.prev, &head);
    MR_TEST_ASSERT_EQ_PTR(n1.link.next, &head);
    MR_TEST_ASSERT_EQ_PTR(n1.link.prev, &n2.link);
}

/* Test: backward traversal via prev yields the reverse order */
MR_TEST(list, backward_traverse) {
    mr_list_t head;
    test_deep_node_t nodes[3];
    test_deep_node_t *n;
    mr_list_t *pos;
    int ids[3];
    int idx;

    mr_list_init(&head);
    for (idx = 0; idx < 3; idx++) {
        nodes[idx].id = idx + 1;
        mr_list_add(&head, &nodes[idx].link);
    }

    MR_TEST_ASSERT_EQ_PTR(head.prev, &nodes[2].link);
    idx = 0;
    for (pos = head.prev; pos != &head; pos = pos->prev) {
        n = MR_LIST_ENTRY(pos, test_deep_node_t, link);
        ids[idx++] = n->id;
    }
    MR_TEST_ASSERT_EQ_INT(idx, 3);
    MR_TEST_ASSERT_EQ_INT(ids[0], 3);
    MR_TEST_ASSERT_EQ_INT(ids[1], 2);
    MR_TEST_ASSERT_EQ_INT(ids[2], 1);
}

/* Test: deleting a middle node relinks the neighbors on both sides */
MR_TEST(list, del_middle_relinks) {
    mr_list_t head;
    test_deep_node_t nodes[3];
    test_deep_node_t *n;
    mr_list_t *pos;
    int idx;

    mr_list_init(&head);
    for (idx = 0; idx < 3; idx++) {
        nodes[idx].id = idx + 1;
        mr_list_add(&head, &nodes[idx].link);
    }

    mr_list_del(&nodes[1].link);
    MR_TEST_ASSERT_EQ_PTR(nodes[0].link.next, &nodes[2].link);
    MR_TEST_ASSERT_EQ_PTR(nodes[2].link.prev, &nodes[0].link);
    MR_TEST_ASSERT_EQ_PTR(head.next, &nodes[0].link);
    MR_TEST_ASSERT_EQ_PTR(head.prev, &nodes[2].link);
    MR_TEST_ASSERT_EQ_PTR(nodes[1].link.next, &nodes[1].link);
    MR_TEST_ASSERT_EQ_PTR(nodes[1].link.prev, &nodes[1].link);

    idx = 0;
    MR_LIST_FOR_EACH(pos, &head) {
        n = MR_LIST_ENTRY(pos, test_deep_node_t, link);
        MR_TEST_ASSERT_EQ_INT(n->id, idx == 0 ? 1 : 3);
        idx++;
    }
    MR_TEST_ASSERT_EQ_INT(idx, 2);
}

/* Test: deleting an unlinked or already deleted node is a safe no-op */
MR_TEST(list, del_idempotent_self_linked) {
    mr_list_t head;
    test_deep_node_t n1;
    test_deep_node_t n2;

    mr_list_init(&head);
    mr_list_init(&n1.link);
    n1.id = 1;
    n2.id = 2;

    /* Delete of a node that was never added keeps it self linked */
    mr_list_del(&n1.link);
    MR_TEST_ASSERT_EQ_PTR(n1.link.next, &n1.link);
    MR_TEST_ASSERT_EQ_PTR(n1.link.prev, &n1.link);

    mr_list_add(&head, &n1.link);
    mr_list_add(&head, &n2.link);
    mr_list_del(&n1.link);
    mr_list_del(&n1.link);

    MR_TEST_ASSERT_EQ_PTR(head.next, &n2.link);
    MR_TEST_ASSERT_EQ_PTR(n2.link.prev, &head);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&head), MR_FALSE);
    MR_TEST_ASSERT_EQ_PTR(n1.link.next, &n1.link);
    MR_TEST_ASSERT_EQ_PTR(n1.link.prev, &n1.link);
}

/* Test: moving entries within the same list reorders them to the tail */
MR_TEST(list, move_within_same_list) {
    mr_list_t head;
    test_deep_node_t nodes[3];
    test_deep_node_t *n;
    mr_list_t *pos;
    int idx;

    mr_list_init(&head);
    for (idx = 0; idx < 3; idx++) {
        nodes[idx].id = idx + 1;
        mr_list_add(&head, &nodes[idx].link);
    }

    /* Move the head entry to the tail */
    mr_list_move(&nodes[0].link, &head);
    idx = 0;
    MR_LIST_FOR_EACH(pos, &head) {
        n = MR_LIST_ENTRY(pos, test_deep_node_t, link);
        MR_TEST_ASSERT_EQ_INT(n->id, idx == 0 ? 2 : (idx == 1 ? 3 : 1));
        idx++;
    }
    MR_TEST_ASSERT_EQ_INT(idx, 3);

    /* Moving the tail entry keeps the order unchanged */
    mr_list_move(&nodes[0].link, &head);
    idx = 0;
    MR_LIST_FOR_EACH(pos, &head) {
        n = MR_LIST_ENTRY(pos, test_deep_node_t, link);
        MR_TEST_ASSERT_EQ_INT(n->id, idx == 0 ? 2 : (idx == 1 ? 3 : 1));
        idx++;
    }
    MR_TEST_ASSERT_EQ_INT(idx, 3);

    /* Move the middle entry to the tail */
    mr_list_move(&nodes[2].link, &head);
    idx = 0;
    MR_LIST_FOR_EACH(pos, &head) {
        n = MR_LIST_ENTRY(pos, test_deep_node_t, link);
        MR_TEST_ASSERT_EQ_INT(n->id, idx == 0 ? 2 : (idx == 1 ? 1 : 3));
        idx++;
    }
    MR_TEST_ASSERT_EQ_INT(idx, 3);
}

/* Test: moving the only entry within its own list keeps the list intact */
MR_TEST(list, move_single_element_same_list) {
    mr_list_t head;
    test_deep_node_t n1;

    mr_list_init(&head);
    n1.id = 42;
    mr_list_add(&head, &n1.link);

    mr_list_move(&n1.link, &head);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&head), MR_FALSE);
    MR_TEST_ASSERT_EQ_PTR(head.next, &n1.link);
    MR_TEST_ASSERT_EQ_PTR(head.prev, &n1.link);
    MR_TEST_ASSERT_EQ_PTR(n1.link.next, &head);
    MR_TEST_ASSERT_EQ_PTR(n1.link.prev, &head);
}

/* Test: replacing the head and the tail relinks the head correctly */
MR_TEST(list, replace_head_and_tail) {
    mr_list_t head;
    test_deep_node_t nodes[3];
    test_deep_node_t n4;
    test_deep_node_t n5;
    test_deep_node_t *n;
    mr_list_t *pos;

    mr_list_init(&head);
    nodes[0].id = 1;
    nodes[1].id = 2;
    nodes[2].id = 3;
    mr_list_add(&head, &nodes[0].link);
    mr_list_add(&head, &nodes[1].link);
    mr_list_add(&head, &nodes[2].link);
    n4.id = 4;
    n5.id = 5;

    /* Replace the head entry */
    mr_list_replace(&n4.link, &nodes[0].link);
    MR_TEST_ASSERT_EQ_PTR(head.next, &n4.link);
    MR_TEST_ASSERT_EQ_PTR(n4.link.prev, &head);
    MR_TEST_ASSERT_EQ_PTR(n4.link.next, &nodes[1].link);
    MR_TEST_ASSERT_EQ_PTR(nodes[1].link.prev, &n4.link);
    MR_TEST_ASSERT_EQ_PTR(nodes[0].link.next, &nodes[0].link);
    MR_TEST_ASSERT_EQ_PTR(nodes[0].link.prev, &nodes[0].link);

    /* Replace the tail entry */
    mr_list_replace(&n5.link, &nodes[2].link);
    MR_TEST_ASSERT_EQ_PTR(head.prev, &n5.link);
    MR_TEST_ASSERT_EQ_PTR(n5.link.next, &head);
    MR_TEST_ASSERT_EQ_PTR(n5.link.prev, &nodes[1].link);
    MR_TEST_ASSERT_EQ_PTR(nodes[1].link.next, &n5.link);
    MR_TEST_ASSERT_EQ_PTR(nodes[2].link.next, &nodes[2].link);
    MR_TEST_ASSERT_EQ_PTR(nodes[2].link.prev, &nodes[2].link);

    /* Forward iteration sees the new entries in place */
    pos = head.next;
    n = MR_LIST_ENTRY(pos, test_deep_node_t, link);
    MR_TEST_ASSERT_EQ_INT(n->id, 4);
    pos = pos->next;
    n = MR_LIST_ENTRY(pos, test_deep_node_t, link);
    MR_TEST_ASSERT_EQ_INT(n->id, 2);
    pos = pos->next;
    n = MR_LIST_ENTRY(pos, test_deep_node_t, link);
    MR_TEST_ASSERT_EQ_INT(n->id, 5);
    MR_TEST_ASSERT_EQ_PTR(pos->next, &head);
}

/* Test: safe iteration on an empty list and deleting every entry */
MR_TEST(list, safe_foreach_empty_and_delete_all) {
    mr_list_t head;
    test_deep_node_t nodes[3];
    mr_list_t *pos;
    mr_list_t *l;
    int count;

    mr_list_init(&head);
    count = 0;
    MR_LIST_FOR_EACH_SAFE(pos, l, &head) {
        count++;
    }
    MR_TEST_ASSERT_EQ_INT(count, 0);

    for (count = 0; count < 3; count++) {
        nodes[count].id = count;
        mr_list_add(&head, &nodes[count].link);
    }
    MR_LIST_FOR_EACH_SAFE(pos, l, &head) {
        mr_list_del(pos);
    }
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&head), MR_TRUE);
    MR_TEST_ASSERT_EQ_PTR(head.next, &head);
    MR_TEST_ASSERT_EQ_PTR(head.prev, &head);
    MR_TEST_ASSERT_EQ_PTR(nodes[0].link.next, &nodes[0].link);
    MR_TEST_ASSERT_EQ_PTR(nodes[2].link.prev, &nodes[2].link);
}

/* Test: MR_LIST_ENTRY works regardless of the member position */
MR_TEST(list, entry_macro_member_positions) {
    test_link_first_t first;
    test_link_last_t last;
    test_link_first_t *ef;
    test_link_last_t *el;

    first.id = 1;
    last.id = 2;
    ef = MR_LIST_ENTRY(&first.link, test_link_first_t, link);
    el = MR_LIST_ENTRY(&last.link, test_link_last_t, link);
    MR_TEST_ASSERT_EQ_PTR(ef, &first);
    MR_TEST_ASSERT_EQ_PTR(el, &last);
    MR_TEST_ASSERT_EQ_INT(ef->id, 1);
    MR_TEST_ASSERT_EQ_INT(el->id, 2);
}

/* Test: the list array init macro self links arrays of any size
 *
 * Potential bug: MR_LIST_ARRAY_INIT feeds MR_ARRAY_SIZE (a sizeof
 * expression) into MR_LISTIFY, which token-pastes the count and therefore
 * requires a literal number. The macro cannot compile as documented, so
 * this case uses the underlying MR_LISTS_INIT with literal counts instead
 * and the macro defect is recorded here without being fixed.
 */
MR_TEST(list, array_init_macro_sizes) {
    mr_list_t one[1] = MR_LISTS_INIT(one, 1);
    mr_list_t many[3] = MR_LISTS_INIT(many, 3);
    mr_size_t i;

    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&one[0]), MR_TRUE);
    MR_TEST_ASSERT_EQ_PTR(one[0].next, &one[0]);
    MR_TEST_ASSERT_EQ_PTR(one[0].prev, &one[0]);

    for (i = 0; i < MR_ARRAY_SIZE(many); i++) {
        MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&many[i]), MR_TRUE);
        MR_TEST_ASSERT_EQ_PTR(many[i].next, &many[i]);
        MR_TEST_ASSERT_EQ_PTR(many[i].prev, &many[i]);
    }
}

/* Test: add and insert accept any list node as the anchor */
MR_TEST(list, node_as_anchor) {
    mr_list_t head;
    test_deep_node_t nodes[4];
    test_deep_node_t *n;
    mr_list_t *pos;
    int idx;

    mr_list_init(&head);
    for (idx = 0; idx < 4; idx++) {
        nodes[idx].id = idx + 1;
    }
    mr_list_add(&head, &nodes[0].link);
    mr_list_add(&head, &nodes[1].link);

    /* Add with a node anchor inserts before that node (tail semantics) */
    mr_list_add(&nodes[0].link, &nodes[2].link);

    /* Insert with a node anchor inserts after that node (head semantics) */
    mr_list_insert(&nodes[1].link, &nodes[3].link);

    idx = 0;
    MR_LIST_FOR_EACH(pos, &head) {
        n = MR_LIST_ENTRY(pos, test_deep_node_t, link);
        MR_TEST_ASSERT_EQ_INT(n->id, idx == 0 ? 3 : (idx == 1 ? 1 : (idx == 2 ? 2 : 4)));
        idx++;
    }
    MR_TEST_ASSERT_EQ_INT(idx, 4);
    MR_TEST_ASSERT_EQ_PTR(head.next, &nodes[2].link);
    MR_TEST_ASSERT_EQ_PTR(head.prev, &nodes[3].link);
    MR_TEST_ASSERT_EQ_PTR(nodes[2].link.next, &nodes[0].link);
    MR_TEST_ASSERT_EQ_PTR(nodes[0].link.next, &nodes[1].link);
    MR_TEST_ASSERT_EQ_PTR(nodes[1].link.next, &nodes[3].link);
    MR_TEST_ASSERT_EQ_PTR(nodes[3].link.next, &head);
}

/* Test: moving entries between lists and back preserves both structures */
MR_TEST(list, move_between_lists_roundtrip) {
    mr_list_t head1;
    mr_list_t head2;
    test_deep_node_t n1;
    test_deep_node_t n2;
    test_deep_node_t n3;

    mr_list_init(&head1);
    mr_list_init(&head2);
    n1.id = 1;
    n2.id = 2;
    n3.id = 3;
    mr_list_add(&head1, &n1.link);
    mr_list_add(&head1, &n2.link);
    mr_list_add(&head2, &n3.link);

    /* Move the tail of head1 onto the non empty head2 */
    mr_list_move(&n2.link, &head2);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&head1), MR_FALSE);
    MR_TEST_ASSERT_EQ_PTR(head1.next, &n1.link);
    MR_TEST_ASSERT_EQ_PTR(head1.prev, &n1.link);
    MR_TEST_ASSERT_EQ_PTR(n1.link.next, &head1);
    MR_TEST_ASSERT_EQ_PTR(n1.link.prev, &head1);
    MR_TEST_ASSERT_EQ_PTR(head2.next, &n3.link);
    MR_TEST_ASSERT_EQ_PTR(head2.prev, &n2.link);
    MR_TEST_ASSERT_EQ_PTR(n3.link.next, &n2.link);
    MR_TEST_ASSERT_EQ_PTR(n2.link.prev, &n3.link);
    MR_TEST_ASSERT_EQ_PTR(n2.link.next, &head2);

    /* Move it back, head1 is restored in order */
    mr_list_move(&n2.link, &head1);
    MR_TEST_ASSERT_EQ_PTR(head1.next, &n1.link);
    MR_TEST_ASSERT_EQ_PTR(head1.prev, &n2.link);
    MR_TEST_ASSERT_EQ_PTR(n1.link.next, &n2.link);
    MR_TEST_ASSERT_EQ_PTR(n2.link.prev, &n1.link);
    MR_TEST_ASSERT_EQ_INT(mr_list_is_empty(&head2), MR_FALSE);
    MR_TEST_ASSERT_EQ_PTR(head2.next, &n3.link);
    MR_TEST_ASSERT_EQ_PTR(head2.prev, &n3.link);
}

#endif /* defined(MR_USE_TEST) */
