#include <stdio.h>
#include <string>
#include "rbtree.h"

using namespace std;

#define KEY_VAL_DELTA 123
#define RN(v) {v + KEY_VAL_DELTA, v, RB_RED}
#define BN(v) {v + KEY_VAL_DELTA, v, RB_BLACK}

#define ARRAY_SIZE(o) (sizeof((o))/sizeof((o)[0]))

class RB_UNIT_TEST {
public:
    // If come across a bug, turn on "dump_tree" to instruct the unit-tester
    // to dump the RB-tree before and after the problematic operation.
    //
    // The "id" is just used to identify the testing case, and hence ease
    // trouble-shooting.
    RB_UNIT_TEST(int test_id, rb_valcolor_t* nodes, int nd_num,
                 bool dump_tree = false) : _test_id(test_id),
                                           _dump_tree(dump_tree) {
        fprintf(stdout, "Testing unit test %d ...", test_id);
        _rbt = rbt_create_manually(nodes, nd_num);
        _save_fail_cnt = _fail_cnt;
    }

    RB_UNIT_TEST(int test_id, bool dump_tree = false):
                 _test_id(test_id), _dump_tree(dump_tree) {
        fprintf(stdout, "Testing unit test %d ...", test_id);
        _rbt = rbt_create();
        _save_fail_cnt = _fail_cnt;
    }

    ~RB_UNIT_TEST() {
        if (_rbt)
            rbt_destroy(_rbt);
        fprintf(stdout, " %s\n",  (_save_fail_cnt == _fail_cnt) ? "succ" : "fail");
    }

    static void Reset_Fail_Cnt() { _fail_cnt = 0; }
    static int Get_Fail_Cnt() { return _fail_cnt; }

    bool Delete(int val, int expect_ret_val = 1) {
        if (DeleteHelper(val, expect_ret_val))
            return true;
        _fail_cnt ++;
        return false;
    }

    bool Insert(int val, int expect_ret_val = 1) {
        if (InsertHelper(val, expect_ret_val))
            return true;
        _fail_cnt ++;
        return false;
    }

    bool BulkInsert(int* val_vect, int vect_len) {
        for (int i = 0; i < vect_len; i++) {
            if (!InsertHelper(val_vect[i], 1)) {
                _fail_cnt ++;
                return false;
            }
        }
        return true;
    }

    bool Search(int key, intptr_t val, RBS_RESULT expect_ret_val = RBS_EXACT) {
        if (SearchHelper(key, val, expect_ret_val))
            return true;
        _fail_cnt ++;
        return false;
    }

    bool SearchLessEqu(int key, int le_key, RBS_RESULT expect_ret_val) {
        if (SearchVariantHelper(key, le_key, true /*LE*/, expect_ret_val)) {
            return true;
        }
        _fail_cnt ++;
        return false;
    }

    bool SearchGreaterEqu(int key, int ge_key, RBS_RESULT expect_ret_val) {
        if (SearchVariantHelper(key, ge_key, false /*GE*/, expect_ret_val)) {
            return true;
        }
        _fail_cnt ++;
        return false;
    }

private:
    bool DeleteHelper(int val, int expect_ret_val) {
        if (!_rbt)
            return false;

        if (_dump_tree)
            Dump_Tree("before_del");

        if (!rbt_verify(_rbt))
            return false;

        int ret = rbt_delete(_rbt, val);

        if (_dump_tree)
            Dump_Tree("after_del");

        if (ret == expect_ret_val && rbt_verify(_rbt) && VerifyKeyVal())
            return true;

        fprintf(stdout, " fail to delete %d;", val);
        return false;
    }

    bool InsertHelper(int key, int expect_ret_val) {
        if (!_rbt)
            return false;

        if (_dump_tree)
            Dump_Tree("before_insert");

        if (!rbt_verify(_rbt))
            return false;

        int ret = rbt_insert(_rbt, key, key + KEY_VAL_DELTA);

        if (_dump_tree)
            Dump_Tree("after_insert");

        if (ret == expect_ret_val && rbt_verify(_rbt) && VerifyKeyVal())
            return true;

        fprintf(stdout, " fail to insert %d;", key);
        return false;
    }

    bool SearchHelper(int key, intptr_t val, RBS_RESULT expect_ret_val) {
        if (!_rbt)
            return false;

        if (_dump_tree)
            Dump_Tree("before_search");

        if (!rbt_verify(_rbt))
            return false;

        intptr_t tmpval;
        int ret = rbt_search(_rbt, key, &tmpval);

        if (_dump_tree)
            Dump_Tree("after_search");

        bool succ = (ret == expect_ret_val && val == tmpval);
        succ = succ || expect_ret_val == RBS_FAIL;
        succ = succ && rbt_verify(_rbt);

        if (succ)
            return true;

        fprintf(stdout, " fail to search %d;", key);
        return false;
    }

    bool SearchVariantHelper(int key, int res_key,
                             bool le_variant, RBS_RESULT expect_ret_val) {
        if (!_rbt)
            return false;

        if (_dump_tree)
            Dump_Tree("before_search");

        if (!rbt_verify(_rbt))
            return false;

        intptr_t res_val;
        int res_key2;
        RBS_RESULT ret = rbt_search_variant(_rbt, key, &res_key2, &res_val,
                                            le_variant ? 1 : 0);

        if (_dump_tree)
            Dump_Tree("after_search");

        bool succ = (ret == expect_ret_val) &&
                    (res_key2 == res_key) && (res_val == (KEY_VAL_DELTA + res_key));
        succ = succ || (expect_ret_val == RBS_FAIL);
        succ = succ && rbt_verify(_rbt);
        if (succ)
            return true;

        fprintf(stdout, " fail to search %d;", key);
        return false;
    }

    string GetDumpFileName(const char* op_name) {
        char buf[200];
        int len = snprintf(buf, sizeof(buf), "test_%d_%s.dot", _test_id, op_name);
        buf[len] = '\0';
        return string(buf);
    }

    // In most testing cases, we set "value = key + KEY_VAL_DELTA" (The
    // rationale is just to ease testing).  This function is to verify the
    // key/value relation is broken.
    //
    bool VerifyKeyVal() {
        for (rb_iter_t iter = rbt_iter_begin(_rbt),
                iter_e = rbt_iter_begin(_rbt);
             iter != iter_e;
             iter = rbt_iter_inc(_iter, iter)) {
            rb_node_t* nd = rbt_iter_deref(iter);
            if (nd->value != nd->key + KEY_VAL_DELTA)
                return false;
        }

        return true;
    }

    void Dump_Tree(const char* op_name) {
#ifdef DEBUG
        rbt_dump_dot(_rbt, GetDumpFileName(op_name).c_str());
#else
        (void)op_name;
#endif
    }

    int _test_id;
    bool _dump_tree;
    rb_tree_t* _rbt;
    static int _fail_cnt;
    int _save_fail_cnt;
};

int RB_UNIT_TEST::_fail_cnt = 0;

bool
unit_test() {
    RB_UNIT_TEST::Reset_Fail_Cnt();

    // test 8.
    {
        rb_valcolor_t nodes[] = { BN(1), RN(2) };
        RB_UNIT_TEST ut(8, nodes, ARRAY_SIZE(nodes));
        ut.Delete(1);
        ut.Delete(2);
    }

    /////////////////////////////////////////////////////////////////////////
    //
    //              Insert tests
    //
    /////////////////////////////////////////////////////////////////////////
    //

    fprintf(stdout, "\n>Testing insert operation...\n");
    // Test 1, Cover the case 1, 2 and 3 of insertion.
    //    Testing case is from book :
    //      Thomas H. Cormen et. al, Instruction to Algorithm, 2nd edition,
    //      page 282.
    {
        rb_valcolor_t nodes[] = { BN(11), RN(2), BN(14), BN(1), BN(7),
                                  RN(15), RN(5), RN(8) };
        RB_UNIT_TEST ut(1, nodes, ARRAY_SIZE(nodes));
        ut.Insert(4);
    }

    // Test 2.  A contrived example for covering case 1', 2' and 3'
    //
    {
        rb_valcolor_t nodes[] = { BN(4), BN(2), RN(10), RN(1), BN(7),
                                  BN(11), RN(6), RN(9) };
        RB_UNIT_TEST ut(2, nodes, ARRAY_SIZE(nodes));
        ut.Insert(8);
    }

    /////////////////////////////////////////////////////////////////////////
    //
    //              Delete Tests
    //
    /////////////////////////////////////////////////////////////////////////
    //
    fprintf(stdout, "\n>Testing delete operation...\n");
    // test 1
    {
        rb_valcolor_t nodes[] = { BN(40), BN(20), BN(60), RN(10), RN(50), RN(70) };
        RB_UNIT_TEST ut(1, nodes, ARRAY_SIZE(nodes));
        ut.Delete(20);
    }

    // test 2
    {
        rb_valcolor_t nodes[] = { BN(40), BN(20), BN(60), RN(10), RN(30),
                                  RN(50), RN(70) };
        RB_UNIT_TEST ut(2, nodes, ARRAY_SIZE(nodes));
        ut.Delete(20);
    }

    // test 3
    {
        rb_valcolor_t nodes[] = { BN(40), BN(20), BN(60), BN(10), BN(30),
                                  BN(50), BN(70), RN(21) };
        RB_UNIT_TEST ut(3, nodes, ARRAY_SIZE(nodes));
        ut.Delete(20);
    }

    // test 4. Test the case 1 and 2 in rbt_delete_fixup().
    {
        rb_valcolor_t nodes[] = { BN(8), RN(2), BN(10), BN(0), BN(4), BN(9),
                                  BN(11), BN(-1), BN(1), BN(3), RN(6),
                                  BN(5), BN(7) };
        RB_UNIT_TEST ut(4, nodes, ARRAY_SIZE(nodes));
        ut.Delete(2);
    }

    // test 5. Test the case 2 and 4 in rbt_delete_fixup().
    {
        rb_valcolor_t nodes[] = { BN(80), RN(20), BN(100), BN(00), BN(40), BN(90),
                                  BN(110), BN(-10), BN(10), BN(30),
                                  BN(60), RN(50), RN(70),
                                  BN(49), BN(51), BN(69), BN(71),
                                  BN(-11), BN(-9), BN(9), BN(11),
                                  BN(29), BN(31),
                                  BN(89), BN(91), BN(109), BN(111) };
        RB_UNIT_TEST ut(5, nodes, ARRAY_SIZE(nodes));
        ut.Delete(20);
    }

    // test 6. For case 1' and 2'
    {
        rb_valcolor_t nodes[] = { BN(4), RN(2), BN(5), BN(1), BN(3) };
        RB_UNIT_TEST ut(6, nodes, ARRAY_SIZE(nodes));
        ut.Delete(5);
    }

    // test 7. For case 2', 3' and 4'.
    {
        rb_valcolor_t nodes[] = { BN(6), BN(2), BN(8), BN(1), RN(4), BN(7),
                                  BN(9), BN(3), BN(5) };
        RB_UNIT_TEST ut(7, nodes, ARRAY_SIZE(nodes));
        ut.Delete(8);
    }

    // test 8.
    {
        rb_valcolor_t nodes[] = { BN(1), RN(2) };
        RB_UNIT_TEST ut(8, nodes, ARRAY_SIZE(nodes));
        ut.Delete(1);
        ut.Delete(2);
    }

    // test 9.
    {
        rb_valcolor_t nodes[] = { BN(1) };
        RB_UNIT_TEST ut(9, nodes, ARRAY_SIZE(nodes));
        ut.Delete(1);
    }

    /////////////////////////////////////////////////////////////////////////
    //
    //              Search Tests
    //
    /////////////////////////////////////////////////////////////////////////
    //

    // test 100
    {
        int val[] = { 1, 2, 3, 5, 7, 8 };
        RB_UNIT_TEST ut(100);
        ut.BulkInsert(val, ARRAY_SIZE(val));
        ut.SearchLessEqu(4, 3, RBS_LESS);
        ut.SearchLessEqu(3, 3, RBS_EXACT);
        ut.SearchGreaterEqu(6, 7, RBS_GREATER);
        ut.SearchGreaterEqu(7, 7, RBS_EXACT);
    }

    return RB_UNIT_TEST::Get_Fail_Cnt() == 0;
};

int
main(int argc, char** argv) {
    if (!unit_test())
        return 1;

    return 0;
}
