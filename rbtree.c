#include <stdio.h>

#include <stdlib.h>
#include "rbtree.h"

#define INVALID_IDX     (-1)
#define SENTINEL_IDX    0
#define likely(x)   __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

#define SWAP(a, b) { typeof(a) t = a; a = b; b = t; }

/****************************************************************************
 *
 *                  Constructors & Destructors
 *
 ****************************************************************************
 */
int
rbt_init(rb_tree_t* rbt) {
    rbt->capacity = 16;
    rbt->tree = (rb_node_t*)malloc(rbt->capacity * sizeof(rb_node_t));
    if (rbt->tree == 0)
        return 0;

    rbt->root = SENTINEL_IDX;
    rbt->node_num = 1; /* sentinel */

    /* Init the sentinel */
    rb_node_t* s = rbt->tree;
    s->left = s->right = s->parent = INVALID_IDX;
    s->color = RB_BLACK;

    return 1;
}

void
rbt_fini(rb_tree_t* rbt) {
    if (rbt && rbt->tree) {
        free((void*)rbt->tree);
        rbt->tree = 0;
        rbt->capacity = rbt->node_num = 0;
        rbt->root = INVALID_IDX;
    }
}

rb_tree_t*
rbt_create(void) {
    rb_tree_t* rbt = (rb_tree_t*)malloc(sizeof(rb_tree_t));
    if (rbt && rbt_init(rbt))
        return rbt;

    return NULL;
}

void
rbt_destroy(rb_tree_t* rbt) {
    if (rbt) {
        rbt_fini(rbt);
        free((void*)rbt);
    }
}

/****************************************************************************
 *
 *                  Untility functions
 *
 ****************************************************************************
 */
/* Return 0 if val < node->data, 1 otherwise */
static inline int
less_than(rb_node_t* node, int key) {
    return key < node->key;
}

static inline int
greater_than(rb_node_t* node, int key) {
    return key > node->key;
}

static void
update_kid(rb_node_t* dad, int kid_was, int kid_is) {
    if (dad->left == kid_was)
        dad->left = kid_is;
    else {
        ASSERT(dad->right == kid_was);
        dad->right = kid_is;
    }
}

static void
rbt_left_rotate(rb_tree_t* rbt, rb_node_t* node) {
    rb_node_t* nd_vect = rbt->tree;

    int node_idx = node - nd_vect;
    int kid_idx = node->right;
    int par_idx = node->parent;

    rb_node_t* kid = nd_vect + kid_idx;
    if (kid->left != INVALID_IDX) {
        nd_vect[kid->left].parent = node_idx;
    }

    node->right = kid->left;
    node->parent = kid_idx;

    kid->left = node_idx;
    kid->parent = par_idx;

    if (par_idx != INVALID_IDX) {
        rb_node_t* dad = nd_vect + par_idx;
        if (dad->left == node_idx)
            dad->left = kid_idx;
        else {
            dad->right = kid_idx;
        }
    } else {
        ASSERT(rbt->root == node_idx);
        rbt->root = kid_idx;
    }
}

static void
rbt_right_rotate(rb_tree_t* rbt, rb_node_t* node) {
    rb_node_t* nd_vect = rbt->tree;

    int node_idx = node - nd_vect;
    int kid_idx = node->left;
    int par_idx = node->parent;

    rb_node_t* kid = nd_vect + kid_idx;
    if (kid->right != INVALID_IDX) {
        nd_vect[kid->right].parent = node_idx;
    }
    node->left = kid->right;
    node->parent = kid_idx;

    kid->right = node_idx;
    kid->parent = par_idx;

    if (par_idx != INVALID_IDX) {
        rb_node_t* dad = nd_vect + par_idx;
        if (dad->left == node_idx)
            dad->left = kid_idx;
        else {
            dad->right = kid_idx;
        }
    } else {
        ASSERT(rbt->root == node_idx);
        rbt->root = kid_idx;
    }
}

/* Try to shrink the node vector */
static int
rbt_try_shrink(rb_tree_t* rbt) {
    if (rbt->capacity < 2*rbt->node_num ||
        rbt->capacity < 32)
        return 1;

    int cap = rbt->node_num * 3 / 2;
    rbt->tree = (rb_node_t*)realloc(rbt->tree, cap * sizeof(rb_node_t));
    if (rbt->tree == 0)
        return 0;

    rbt->capacity = cap;
    return 1;
}

/****************************************************************************
 *
 *              Binary-search-tree operations
 *
 ****************************************************************************
 */

/* Search the binary-search-tree; if found, return the index of node,
 * INVALID_IDX otherwise.
 */
inline static int
bst_search(rb_tree_t* rbt, int key) {
    rb_node_t* nd_vect = rbt->tree;
    rb_node_t* sentinel = rbt->tree;

    rb_node_t* cur = nd_vect + rbt->root;
    while (cur != sentinel) {
        if (less_than(cur, key))
            cur = nd_vect + cur->left;
        else if (greater_than(cur, key))
            cur = nd_vect + cur->right;
        else
            return cur - nd_vect;
    }

    return INVALID_IDX;
}

/* Insert "val" in the the binary-search-tree. the "bst" stands
 * for binary-search-tree. This function has nothing to do with
 * rb tree specific features.
 *
 * Return the index of the node being inserted if it was successful,
 * or INVALID_IDX otherwise.
 */
static int
bst_insert(rb_tree_t* t, int key, intptr_t value) {
    rb_node_t* nodes = t->tree;

    /* Resize the vector if necessary */
    if (t->capacity <= t->node_num) {
        int cap = t->node_num * 3/2;
        if (cap <= 16)
            cap = 16;

        nodes = t->tree = (rb_node_t*)realloc(nodes, 100* sizeof(rb_node_t));
        t->capacity = cap;

        if (!nodes)
            return INVALID_IDX;
    }

    /* The tree is empty */
    if (unlikely(t->root == SENTINEL_IDX)) {
        int root_id = 1;
        t->root = root_id;
        t->node_num = 2;

        rb_node_t* root = nodes + root_id;
        root->key       = key;
        root->value     = value;
        root->left      = root->right = SENTINEL_IDX;
        root->parent    = INVALID_IDX;
        root->color     = RB_BLACK;

        return root_id;
    }

    /* Insert the value in the tree. Care must be taken to avoid inserting a
     * value which is already in the tree.
     */
    rb_node_t* prev = 0;
    rb_node_t* cur = nodes + t->root;
    rb_node_t* sentinel = nodes;

    while (cur != sentinel) {
        prev = cur;
        if (less_than(cur, key))
            cur = nodes + cur->left;
        else if (greater_than(cur, key))
            cur = nodes + cur->right;
        else {
            /* the value is already in the tree */
            return INVALID_IDX;
        }
    }

    int new_nd_idx = t->node_num++;
    rb_node_t* new_nd = nodes + new_nd_idx;
    new_nd->key = key;
    new_nd->value = value;
    new_nd->parent = prev - nodes;
    new_nd->left = new_nd->right = SENTINEL_IDX;
    new_nd->color = RB_RED;

    if (less_than(prev, key))
        prev->left = new_nd_idx;
    else
        prev->right = new_nd_idx;

    return new_nd_idx;
}

/****************************************************************************
 *
 *              RB-tree operations
 *
 ****************************************************************************
 */
RBS_RESULT
rbt_search(rb_tree_t* rbt, int key, intptr_t* value) {
    if (unlikely(rbt_is_empty(rbt)))
        return RBS_FAIL;

    int nd_idx = bst_search(rbt, key);
    if (nd_idx != INVALID_IDX) {
        if (value)
            *value = rbt->tree[nd_idx].value;
        return RBS_EXACT;
    }
    return RBS_FAIL;
}

RBS_RESULT
rbt_search_variant(rb_tree_t* rbt, int key, int* res_key, intptr_t* res_value,
                   int le) {
    if (unlikely(rbt_is_empty(rbt)))
        return RBS_FAIL;

    rb_node_t* nd_vect = rbt->tree;
    rb_node_t* sentinel = rbt->tree;

    rb_node_t* cur = nd_vect + rbt->root;

    rb_node_t* last_left, *last_right;
    last_left = last_right = NULL;

    RBS_RESULT res = RBS_FAIL;
    rb_node_t* res_elemt = NULL;

    while (cur != sentinel) {
        if (less_than(cur, key)) {
            last_left = cur;
            cur = nd_vect + cur->left;
        } else if (greater_than(cur, key)) {
            last_right = cur;
            cur = nd_vect + cur->right;
        } else {
            res = RBS_EXACT;
            res_elemt = cur;
            break;
        }
    }

    if (res == RBS_FAIL) {
        if (le) {
            if (last_right) {
                res_elemt = last_right;
                res = RBS_LESS;
            }
        } else if (last_left) {
            res_elemt = last_left;
            res = RBS_GREATER;
        }
    }

    if (res != RBS_FAIL) {
        if (res_key)
            *res_key = res_elemt->key;

        if (res_value)
            *res_value = res_elemt->value;
    }

    return res;
}

int
rbt_get_min(rb_tree_t* rbt) {
    ASSERT(!rbt_is_empty(rbt));

    rb_node_t* nd_vect = rbt->tree;
    rb_node_t* root = nd_vect + rbt->root;
    rb_node_t* node = 0;
    if (root->left != SENTINEL_IDX)
        node = nd_vect + root->left;
    else if (root->right != SENTINEL_IDX)
        node = nd_vect + root->right;
    else
        node = root;

    while (node->left != SENTINEL_IDX) {
        node = nd_vect + node->left;
    }

    return node->key;
}

int
rbt_set_value(rb_tree_t* rbt, int key, intptr_t value) {
    if (unlikely(rbt_is_empty(rbt)))
        return 0;

    int nd_idx = bst_search(rbt, key);
    if (nd_idx != INVALID_IDX) {
        rbt->tree[nd_idx].value = value;
        return 1;
    }
    return 0;
}

int
rbt_insert(rb_tree_t* rbt, int key, intptr_t value) {
    /* step 1: insert the key/val pair into the binary-search-tree */
    int nd_idx = bst_insert(rbt, key, value);
    if (nd_idx == INVALID_IDX)
        return 0;

    /* step 2: Perform color fix up */
    rb_node_t* nd_vect = rbt->tree;
    rb_node_t* cur = nd_vect + nd_idx;

    while (cur->parent != INVALID_IDX && nd_vect[cur->parent].color == RB_RED) {
        rb_node_t* dad = nd_vect + cur->parent;
        rb_node_t* grandpar = nd_vect + dad->parent;

        if (nd_vect + grandpar->left == dad) {
            rb_node_t* uncle = nd_vect + grandpar->right;
            /* case 1: Both parent and uncle are in red. Just flip the color
             * of parent, uncle and grand-parent.
             */
            if (uncle->color == RB_RED) {
                grandpar->color = RB_RED;
                dad->color = uncle->color = RB_BLACK;
                cur = grandpar;
                continue;
            }

            /* case 2: Parent and uncle's color are different (i.e. parent in
             * red, uncle in black), and "cur" is parent's *RIGHT* kid.
             */
            if (dad->right + nd_vect == cur) {
                /* left rotate around parent */
                rbt_left_rotate(rbt, dad);
                SWAP(cur, dad);

                /* Fall through to case 3 */
            }

            /* case 3: The condition is the same as case 2, except that 'cur'
             *  is the *LEFT* kid of the parent.
             */
            rbt_right_rotate(rbt, grandpar);
            dad->color = RB_BLACK;
            grandpar->color = RB_RED;

            break; /* we are done, almost*/
        } else {
            rb_node_t* uncle = nd_vect + grandpar->left;
            /* case 1': Both parent and uncle are in red. Just flip the color
             * of parent, uncle and grand-parent.
             */
            if (uncle->color == RB_RED) {
                grandpar->color = RB_RED;
                dad->color = uncle->color = RB_BLACK;
                cur = grandpar;
                continue;
            }


            /* case 2': Parent and uncle's color are different (i.e. parent in
             * red, uncle in black), and "cur" is parent's *LEFT* kid.
             */
            if (dad->left + nd_vect == cur) {
                /* left rotate around parent */
                rbt_right_rotate(rbt, dad);
                SWAP(cur, dad);
                /* Fall through to case 3 */
            }

            /* case 3: The condition is the same as case 2, except that 'cur'
             *  is the *RIGHT* kid of the parent.
             */
            rbt_left_rotate(rbt, grandpar);
            dad->color = RB_BLACK;
            grandpar->color = RB_RED;

            break;
        }
    }

    /* make sure the root is in black */
    nd_vect[rbt->root].color = RB_BLACK;

    return 1;
}

static void
rbt_delete_fixup(rb_tree_t* rbt, int node_idx) {
    rb_node_t* nd_vect = rbt->tree;
    while (node_idx != rbt->root && nd_vect[node_idx].color == RB_BLACK) {
        rb_node_t* node = nd_vect + node_idx;
        rb_node_t* dad = nd_vect + node->parent;

        if (dad->left == node_idx) {
            int sibling_idx = dad->right;
            rb_node_t* sibling = nd_vect + sibling_idx;

            /* case 1: sibling is in red color. Rotate around dad. */
            if (sibling->color == RB_RED) {
                sibling->color = RB_BLACK;
                dad->color = RB_RED;
                rbt_left_rotate(rbt, dad);

                /* Both "current" node and its parent remain unchanged, but
                 * sibling is changed.
                 */
                sibling_idx = dad->right;
                sibling = nd_vect + sibling_idx;
            }

            ASSERT(sibling->color == RB_BLACK);
            rb_node_t* slk = nd_vect + sibling->left;
            rb_node_t* srk = nd_vect + sibling->right;

            if (slk->color == RB_BLACK && srk->color == RB_BLACK) {
                /* case 2: sibling's both kids are in black. Set sibling's
                 * color to be red.
                 */
                sibling->color = RB_RED;
                node_idx = dad - nd_vect;
            } else {
                if (srk->color == RB_BLACK) {
                    /* case 3: sibling's right kid is in black, while the left
                     * kid in in red.
                     */
                    slk->color = RB_BLACK;
                    sibling->color = RB_RED;
                    rbt_right_rotate(rbt, sibling);

                    sibling = slk;
                    sibling_idx = sibling - nd_vect;
                }

                /* case 4: sibling's right kid is in red */
                rbt_left_rotate(rbt, dad);

                /* Now dad is still dad, sibling become grand-parent. Propagate
                 * dad's color to grandpar.
                 */
                sibling->color = dad->color;

                /* dad and new uncle are in black */
                dad->color = RB_BLACK;
                nd_vect[sibling->right].color = RB_BLACK;

                break;
            }
            continue;

        } else {
            int sibling_idx = dad->left;
            rb_node_t* sibling = nd_vect + sibling_idx;

            /* case 1': sibling is in red color. Rotate around dad. */
            if (sibling->color == RB_RED) {
                sibling->color = RB_BLACK;
                dad->color = RB_RED;
                rbt_right_rotate(rbt, dad);

                /* Both "current" node and its parent remain unchanged, but
                 * sibling is changed.
                 */
                sibling_idx = dad->left;
                sibling = nd_vect + sibling_idx;
            }

            ASSERT(sibling->color == RB_BLACK);
            rb_node_t* slk = nd_vect + sibling->right;
            rb_node_t* srk = nd_vect + sibling->left;

            if (slk->color == RB_BLACK && srk->color == RB_BLACK) {
                /* case 2': sibling's both kids are in black. Set sibling's
                 * color to be red.
                 */
                sibling->color = RB_RED;
                node_idx = dad - nd_vect;
            } else {
                if (srk->color == RB_BLACK) {
                    /* case 3': sibling's left kid is in black, while the right
                     * kid in in red.
                     */
                    slk->color = RB_BLACK;
                    sibling->color = RB_RED;
                    rbt_left_rotate(rbt, sibling);

                    sibling = slk;
                    sibling_idx = sibling - nd_vect;
                }

                /* case 4': sibling's left kid is in red */
                rbt_right_rotate(rbt, dad);

                /* Now dad is still dad, sibling become grand-parent. Propagate
                 * dad's color to grandpar.
                 */
                sibling->color = dad->color;

                /* dad and new uncle are in black */
                dad->color = RB_BLACK;
                nd_vect[sibling->left].color = RB_BLACK;

                break;
            }
            continue;
        }
    }

    nd_vect[node_idx].color = RB_BLACK;
}

int
rbt_delete(rb_tree_t* rbt, int key, intptr_t* val) {
    /* step 1: find the element to be deleted */
    int nd_idx = bst_search(rbt, key);
    if (nd_idx == INVALID_IDX)
        return 0;

    if (val)
        *val = rbt->tree[nd_idx].value;

    /* step 2: delete the element as we normally do with a binary-search tree */
    rb_node_t* nd_vect = rbt->tree;
    rb_node_t* node = nd_vect + nd_idx; /* the node being deleted*/

    int splice_out_idx;
    if (node->left == SENTINEL_IDX || node->right == SENTINEL_IDX) {
        splice_out_idx = nd_idx;
    } else {
        /* Get the successor of the node corrponding to nd_idx */
        rb_node_t* succ = nd_vect + node->right;
        while (succ->left != SENTINEL_IDX)
            succ = nd_vect + succ->left;

        splice_out_idx = succ - nd_vect;
    }

    rb_node_t* splice_out = nd_vect + splice_out_idx;

    int so_kid_idx = (splice_out->left != SENTINEL_IDX) ?
                      splice_out->left : splice_out->right;

    rb_node_t* so_kid = nd_vect + so_kid_idx;

    if (splice_out->parent != INVALID_IDX) {
        update_kid(nd_vect + splice_out->parent, splice_out_idx/*was*/, so_kid_idx);
    } else {
        ASSERT(rbt->root == splice_out_idx);
        rbt->root = so_kid_idx;
    }
    so_kid->parent = splice_out->parent;

    if (splice_out_idx != nd_idx) {
        nd_vect[nd_idx].key = splice_out->key;
        nd_vect[nd_idx].value = splice_out->value;
    }

    /* step 3: color fix up */
    if (splice_out->color == RB_BLACK) {
        rbt_delete_fixup(rbt, so_kid_idx);
    }

    /* step 4: Misc finialization.
     *
     *  o. Migrate the last element to splice_out to avoid "hole" in
     *     the node vector. If we perform migration before step 3, care must
     *     be taken that the last element could be the "so_kid", in that case,
     *     the "so_kid" need to be re-evaluated.
     *  o. Misc update.
     */
    if (splice_out_idx + 1 != rbt->node_num) {
        int last_idx = rbt->node_num - 1;
        rb_node_t* last = nd_vect + last_idx;
        *splice_out = *last;
        if (last->parent != INVALID_IDX) {
            update_kid(nd_vect + last->parent, last_idx, splice_out_idx);
        } else {
            ASSERT(rbt->root == last_idx);
            rbt->root = splice_out_idx;
        }
        nd_vect[last->left].parent = splice_out_idx;
        nd_vect[last->right].parent = splice_out_idx;
    }

    rbt->node_num--;
    nd_vect[rbt->root].color = RB_BLACK;
    return rbt_try_shrink(rbt);
}

/*****************************************************************************
 *
 *          Debugging and Testing Support
 *
 *****************************************************************************
 */
#if defined(DEBUG) || defined(ENABLE_TESTING)
rb_tree_t*
rbt_create_manually(rb_valcolor_t* node_info, int len) {
    rb_tree_t* rbt = rbt_create();
    if (!rbt)
        return NULL;

    int i;
    for (i = 0; i < len; i++) {
        int nd_idx = bst_insert(rbt, node_info[i].key, node_info[i].value);
        if (nd_idx == INVALID_IDX) {
            rbt_destroy(rbt);
            return NULL;
        }
        rbt->tree[nd_idx].color = node_info[i].color;
    }

    return rbt;
}

int
rbt_verify(rb_tree_t* rbt) {
    /* step 1: Make sure the internal data structure are consistent.*/
    if (rbt->node_num > rbt->capacity)
        return 0;

    if (rbt->tree == 0)
        return 0;

    /* step 2: Make sure it is a tree */
    int node_num = rbt->node_num;
    rb_node_t* nd_vect = rbt->tree;

    int* cnt = (int*)malloc(sizeof(int) * node_num);
    int i;
    for (i = 0; i < node_num; i++) cnt[i] = 0;

    for (i = SENTINEL_IDX + 1; i < node_num; i++) {
        rb_node_t* nd = nd_vect + i;
        int kid = nd->left;
        if (kid < SENTINEL_IDX || kid >= node_num)
            return 0;
        cnt[kid]++;

        kid = nd->right;
        if (kid < SENTINEL_IDX || kid >= node_num)
            return 0;
        cnt[kid]++;

        /* Take this opportunity to check if the color is either red or black.*/
        if (nd->color != RB_BLACK && nd->color != RB_RED)
            return 0;

        /* make sure the "parent" pointer make sense */
        if (nd->parent != INVALID_IDX) {
            rb_node_t* dad = nd_vect + nd->parent;
            if (dad->left != i && dad->right != i)
                return 0;
        }
    }

    int root_cnt = 0;
    for (i = SENTINEL_IDX + 1; i < node_num; i++) {
        int t = cnt[i];
        if (t > 1) {
            /* It is DAG, not tree */
            return 0;
        } else if (t == 0) {
            root_cnt++;
            if (i != rbt->root) {
                /* we either have multiple roots, or rbt->root is not set
                 * properly.
                 */
                free(cnt);
                return 0;
            }
        }
    }
    free(cnt);
    cnt = 0;
    if (root_cnt != 1) {
        if (root_cnt == 0 && node_num != 1)
            return 0;
    }

    /* Following is to check if the RB-tree properies are preserved */

    /* step 3: check if root and leaf are in black color */
    if (nd_vect[rbt->root].color != RB_BLACK ||
        nd_vect[SENTINEL_IDX].color != RB_BLACK)
        return 0;

    /* step 4: Check if there are adjacent red nodes. */
    for (i = SENTINEL_IDX + 1; i < node_num; i++) {
        rb_node_t* nd = nd_vect + i;
        if (nd->color == RB_RED) {
            if (nd_vect[nd->left].color == RB_RED ||
                nd_vect[nd->right].color == RB_RED)
                return 0;
        }
    }

    /* step 5: check if all paths contain the same number of black nodes.*/
    int len = -1;
    rb_node_t* sentinel = rbt->tree;
    for (i = SENTINEL_IDX + 1; i < node_num; i++) {
        rb_node_t* nd = nd_vect + i;
        if (nd->left != SENTINEL_IDX || nd->right != SENTINEL_IDX) {
            /* ignore internal node */
            continue;
        }

        rb_node_t* cur = nd_vect + rbt->root;
        int key = nd->key;
        int this_len = 0;
        while (cur != sentinel) {
            if (cur->color == RB_BLACK)
                this_len++;

            if (less_than(cur, key)) {
                cur = nd_vect + cur->left;
            } else if (greater_than(cur, key)) {
                cur = nd_vect + cur->right;
            } else {
                break;
            }
        }

        if (len == -1)
            len = this_len;

        if (len != this_len)
            return 0;
    }

    return 1;
}

int
rbt_dump_dot(rb_tree_t* rbt, const char* name) {
    FILE* f = fopen(name, "w");
    if (f == 0)
        return 0;

    fprintf(f, "digraph G {\n");

    int i, e = rbt->node_num;
    rb_node_t* nd_vect = rbt->tree;

    for (i = SENTINEL_IDX + 1; i < e; i++) {
        rb_node_t* node = nd_vect + i;
        fprintf(f, "\t\%d [style=filled, color=%s, fontcolor=white];\n",
                node->key, node->color == RB_RED ? "red" : "black");
    }

    for (i = SENTINEL_IDX + 1; i < e; i++) {
        rb_node_t* node = nd_vect + i;
        if (node->left != SENTINEL_IDX)
            fprintf(f, "%d -> %d;\n", node->key, nd_vect[node->left].key);

        if (node->right != SENTINEL_IDX) {
            fprintf(f, "%d -> %d [label=r];\n", node->key,
                    nd_vect[node->right].key);
        }
    }

    fprintf(f, "}");
    fclose(f);

    return 1;
}

/* Print the rb tree directly to the console in plain text format*/
void
rbt_dump_text(rb_tree_t* rbt) {
    rb_node_t* nd_vect = rbt->tree;
    fprintf(stdout, "RB tree: root id:%d, node_num:%d\n",
            rbt->root, rbt->node_num);

    int i, e;
    for (i = 0, e = rbt->node_num; i < e; i++) {
        rb_node_t* node = nd_vect + i;
        fprintf(stdout,
                " Node:%d, key:%d, value:%ld, left:%d, right:%d, parent:%d\n",
                i, node->key, node->value, node->left, node->right,
                node->parent);
    }
    fprintf(stderr, "\n");
    fflush(stdout);
}

#endif /*defined(DEBUG) || defined(ENABLE_TESTING)*/
