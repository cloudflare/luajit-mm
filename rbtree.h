#ifndef _RBTREE_H_
#define _RBTREE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h> /* for intptr_t */
typedef enum {
    RB_BLACK = 0,
    RB_RED = 1
} rb_color_t;

typedef struct {
    intptr_t value;
    int key;
    int parent;
    int left;
    int right;
    unsigned char color; /* either RB_BLACK or RB_RED */
} rb_node_t;

typedef struct {
    rb_node_t* tree;
    int root;
    int node_num;
    int capacity;
} rb_tree_t;

/* Construt/destruct RB tree */
rb_tree_t* rbt_create(void);
void rbt_destroy(rb_tree_t*);

/* Alternative way to construct/destrut RB tree -- the instance is already
 * allocated (say a global variable), but the content is not yet initialized.
 */
int rbt_init(rb_tree_t*);
void rbt_fini(rb_tree_t*);

/* RB-tree operations */
int rbt_insert(rb_tree_t*, int key, intptr_t val);
int rbt_delete(rb_tree_t*, int key);
int rbt_get_min(rb_tree_t*);
int rbt_get_max(rb_tree_t*);

typedef enum {
    RBS_FAIL = 0,
    RBS_EXACT = 1,
    RBS_LESS = 2,
    RBS_GREATER = 3,
} RBS_RESULT;
/* Return the element matching the given key. */
RBS_RESULT rbt_search(rb_tree_t*, int key, intptr_t* val);

/* Return the maximum value which is no greater than the given key*/
RBS_RESULT rbt_search_variant(rb_tree_t*, int key,
                              int* res_key, intptr_t* res_val, int le);
#define rbt_search_le(t, k, k2, v) rbt_search_variant((t), (k), (k2), (v), 1)
#define rbt_search_ge(t, k, k2, v) rbt_search_variant((t), (k), (k2), (v), 0)

int rbt_set_value(rb_tree_t*, int key, intptr_t value);

#define rbt_is_empty(rbt) ((rbt)->node_num == 1 ? 1 : 0)

/* RB-tree iterator */
typedef rb_node_t* rb_iter_t;
#define rbt_iter_begin(rbt)         ((rbt)->tree + 1)
#define rbt_iter_end(rbt)           ((rbt)->tree + (rbt)->node_num)
#define rbt_iter_inc(rbt, iter)     ((iter) + 1)

#define rbt_iter_deref(iter)        ((iter))

#if defined(DEBUG) || defined(ENABLE_TESTING)
/* NOTE: If DEBUG is off and ENABLE_TESTING is on, functions enclosed by this
 *   directive should not be invoked by RB-tree opertaions. The ENABLE_TESTING
 *   can be used with release-build without degrading the performance.
 */

/* Create a RB tree manually. The nodes' value, color as well as their
 * insertion order are specified by "node_info". The color fix-up is disabled
 * each time a node is inserted. It's up to the caller to ensure the final
 * resulting tree conform to the RB-tree spec.
 *
 * The purpose of this function is to constuct a RB tree for testing
 * purpose.
 */
typedef struct {
    intptr_t value;
    int key;
    rb_color_t color;
} rb_valcolor_t;
rb_tree_t* rbt_create_manually(rb_valcolor_t* node_info, int len);

int rbt_verify(rb_tree_t*);
/* dump in dotty format */
int rbt_dump_dot(rb_tree_t*, const char* file_name);
void rbt_dump_text(rb_tree_t* rbt);

#endif /*defined(DEBUG) || defined(ENABLE_TESTING)*/

#ifdef DEBUG
// Usage examples: ASSERT(a > b),  ASSERT(foo() && "Opps, foo() reutrn 0");
#define ASSERT(c) if (!(c))\
        { fprintf(stderr, "%s:%d Assert: %s\n", __FILE__, __LINE__, #c); abort(); }
#else

#define ASSERT(c) ((void)0)

#endif

#ifdef __cplusplus
}
#endif

#endif /*_RBTREE_H_*/
