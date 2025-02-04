/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_kmap.h>
#include <libc/mr_compiler.h>
#include <libc/mr_malloc.h>

mr_err_t mr_kmap_init(mr_kmap_t *kmap, mr_size_t size, mr_f32_t load_factor) {
    /* Check arguments */
    if ((size == 0) || (load_factor <= 0.0f) || (load_factor > 1.0f)) {
        return -MR_EINVAL;
    }

    /* Initialize kmap */
    kmap->b = mr_calloc(size, sizeof(mr_knode_t));
    if (!kmap->b) {
        return -MR_ENOMEM;
    }
    kmap->size = size;
    kmap->count = 0;
    kmap->load_factor = load_factor;
    return 0;
}

MR_INLINE mr_err_t kmap_resize(mr_kmap_t *kmap, mr_size_t new_size) {
    mr_knode_t *node, *prev;
    mr_knode_t **new_b;
    mr_size_t index, i;

    if (new_size == kmap->size) {
        /* No need to resize */
        return 0;
    }

    /* Allocate new bucket */
    if (new_size > 0) {
        new_b = mr_calloc(new_size, sizeof(mr_knode_t));
        if (!new_b) {
            return -MR_ENOMEM;
        }

        /* Rehash */
        for (i = 0; i < kmap->size; i++) {
            node = kmap->b[i];
            while (node) {
                prev = node;
                node = node->next;
                index = prev->key % new_size;
                prev->next = new_b[index];
                new_b[index] = prev;
            }
        }
    } else {
        new_b = MR_NULL;
    }

    /* Update kmap */
    mr_free(kmap->b);
    kmap->b = new_b;
    kmap->size = new_size;
    return 0;
}

mr_err_t mr_kmap_add(mr_kmap_t *kmap, mr_uint32_t key, void *value) {
    mr_knode_t *node;
    mr_size_t index;
    mr_err_t ret;

    /* Check arguments */
    if ((kmap->size == 0) || (!value)) {
        return -MR_EINVAL;
    }

    /* Check load factor */
    if (((mr_f32_t)kmap->count / (mr_f32_t)kmap->size) >= kmap->load_factor) {
        /* Resize kmap */
        ret = kmap_resize(kmap, kmap->size * 2);
        if (ret != 0) {
            return ret;
        }
    }

    /* Update existing node */
    index = key % kmap->size;
    node = kmap->b[index];
    while (node) {
        if (node->key != key) {
            node = node->next;
            continue;
        }

        /* Update value */
        node->value = value;
        return 0;
    }

    /* Insert new node */
    node = mr_malloc(sizeof(mr_knode_t));
    if (!node) {
        return -MR_ENOMEM;
    }
    node->key = key;
    node->value = value;
    node->next = kmap->b[index];
    kmap->b[index] = node;
    kmap->count++;
    return 0;
}

void *mr_kmap_del(mr_kmap_t *kmap, mr_uint32_t key) {
    mr_knode_t *node, *prev;
    mr_size_t index;
    void *value;

    /* Check arguments */
    if (kmap->size == 0) {
        return MR_NULL;
    }

    /* Remove node */
    index = key % kmap->size;
    node = kmap->b[index];
    prev = MR_NULL;
    while (node) {
        if (node->key != key) {
            prev = node;
            node = node->next;
            continue;
        }
        if (!prev) {
            kmap->b[index] = node->next;
        } else {
            prev->next = node->next;
        }
        value = node->value;
        mr_free(node);
        kmap->count--;

        /* Check shrink factor */
        if (((((mr_f32_t)kmap->size * kmap->load_factor) / 4.0f)
             > (mr_f32_t)kmap->count)) {
            /* Resize kmap */
            kmap_resize(kmap, kmap->size / 2);
        }
        return value;
    }
    return MR_NULL;
}

void *mr_kmap_value(mr_kmap_t *kmap, mr_uint32_t key) {
    mr_knode_t *node;
    mr_size_t index;

    /* Check arguments */
    if (kmap->size == 0) {
        return MR_NULL;
    }

    /* Find node */
    index = key % kmap->size;
    node = kmap->b[index];
    while (node) {
        if (node->key != key) {
            node = node->next;
            continue;
        }
        return node->value;
    }
    return MR_NULL;
}

void mr_kmap_clear(mr_kmap_t *kmap) {
    mr_knode_t *node, *prev;
    mr_size_t i;

    /* Clear kmap */
    for (i = 0; i < kmap->size; i++) {
        node = kmap->b[i];
        while (node) {
            prev = node;
            node = node->next;
            mr_free(prev);
        }
        kmap->b[i] = MR_NULL;
    }
    kmap->count = 0;
}

mr_err_t mr_kmap_shrink(mr_kmap_t *kmap, mr_size_t size) {
    /* Check arguments */
    if (size < kmap->count) {
        return -MR_EINVAL;
    }

    /* Resize kmap */
    return kmap_resize(kmap, size);
}
