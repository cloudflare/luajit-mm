#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/mman.h>

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include "lj_mm.h"

using namespace std;

#define ARRAY_SIZE(a) (sizeof((a))/sizeof((a)[0]))

static int fail_num = 0;

typedef struct {
    int page_idx;
    int order;
    int page_num;
    int fraction;
} blk_info2_t;

class UNIT_TEST {
public:
    UNIT_TEST(int test_id, int page_num);
    ~UNIT_TEST();

    void VerifyStatus(blk_info2_t* alloc_blk_v, int alloc_blk_v_len,
                      blk_info2_t* free_blk_v, int free_blk_v_len);

    int getPageSize() const { return _page_size; }

    // Allocate "full_page_num * page-size + fraction" bytes.
    bool Alloc(int page_num, int fraction);
    bool Mmap(int page_num, int page_fraction);

    // Unmap [first-page : first + page_num * page-size + fraction].
    bool Unmap(int first_page, int page_num, int fraction=0);

private:
    void Transfer_Block_Info(vector<block_info_t>& to,
                             blk_info2_t* from, int len);

    bool Compare_Blk_Info_Vect(const vector<block_info_t>& v1,
                               const vector<block_info_t>& v2);
private:
    int _test_id;
    int _page_size;
    int _page_num;
    bool _init_succ;
    bool _test_succ;
    char* _trunk_base;
};

UNIT_TEST::UNIT_TEST(int test_id, int page_num)
    : _test_id(test_id) {
    lj_mm_opt_t mm_opt;
    mm_opt.page_num = _page_num = page_num;
    _init_succ = lm_init2(0, &mm_opt);
    _test_succ = _init_succ ? true : false;
    _page_size = sysconf(_SC_PAGESIZE);
    if (_init_succ) {
        const lm_status_t* status = lm_get_status();
        _trunk_base = status->first_page;
        lm_free_status(const_cast<lm_status_t*>(status));
    } else {
        _trunk_base = NULL;
    }

    fprintf(stderr, " unit test %0d ...", test_id);
}

UNIT_TEST::~UNIT_TEST() {
    if (_init_succ) {
        lm_fini();
    }

    fprintf(stdout, " %s\n", _test_succ ? "succ" : "fail");

    if (!_init_succ || !_test_succ)
        fail_num ++;
}


bool
UNIT_TEST::Compare_Blk_Info_Vect(const vector<block_info_t>& v1,
                                 const vector<block_info_t>& v2) {
    if (v1.size() != v2.size())
        return false;

    vector<block_info_t>::const_iterator
        iter = v1.begin(), iter_e = v1.end(), iter2 = v2.begin();

    for (; iter != iter_e; ++iter, ++iter2) {
        const block_info_t& t1 = *iter;
        const block_info_t& t2 = *iter2;

        if (t1.page_idx != t2.page_idx ||
            t1.order != t2.order ||
            t1.size != t2.size) {
            return false;
        }
    }
    return true;
}

bool
UNIT_TEST::Alloc(int page_num, int page_fraction) {
    if (!_init_succ || !_test_succ)
        return false;

    void* p = lm_malloc(page_num * getPageSize() + page_fraction);
    if (!p) {
        _test_succ = false;
        return false;
    }

    return true;
}

// allocate "page_num * page_size + page_fraction" bytes.
//
bool
UNIT_TEST::Mmap(int page_num, int page_fraction) {
    if (!_init_succ || !_test_succ)
        return false;

    void* p = lm_mmap(NULL, page_num * _page_size + page_fraction,
                      PROT_READ|PROT_WRITE,
                      MAP_32BIT|MAP_PRIVATE|MAP_ANONYMOUS,
                      -1, 0);
    _test_succ = (p != MAP_FAILED);
    return _test_succ;
}

// Unmap [first-page : first + page_num * page-size + fraction]
bool
UNIT_TEST::Unmap(int first_page, int page_num, int fraction) {

    if (!_init_succ || !_test_succ)
        return false;

    char* start_addr = _trunk_base + first_page * _page_size;
    int len = page_num * _page_size  + fraction;
    _test_succ = (lm_munmap(start_addr, len) == 0);

    return _test_succ;
}

void
UNIT_TEST::Transfer_Block_Info(vector<block_info_t>& to,
                               blk_info2_t* from, int len) {
    int page_sz = _page_size;
    to.clear();
    for (int i = 0; i < len; i++) {
        block_info_t bi;
        bi.page_idx = from[i].page_idx;
        bi.order = from[i].order;
        bi.size = from[i].page_num * page_sz + from[i].fraction;
        to.push_back(bi);
    }
}

// Functor for std::sort to rearrange block_info_t in the ascending order
// of its page_idx.
static bool
compare_blk_info(const block_info_t& b1, const block_info_t& b2) {
    return b1.page_idx < b2.page_idx;
}

void
UNIT_TEST::VerifyStatus(blk_info2_t* alloc_blk_v, int alloc_blk_v_len,
                        blk_info2_t* free_blk_v, int free_blk_v_len) {
    if (!_test_succ)
        return;

    const lm_status_t* status = lm_get_status();
    if (free_blk_v_len != status->free_blk_num ||
        alloc_blk_v_len != status->alloc_blk_num) {
        _test_succ = false;
    }

    if (_test_succ && free_blk_v_len) {
        vector<block_info_t> v1, v2;

        Transfer_Block_Info(v1, free_blk_v, free_blk_v_len);
        for (int i = 0; i < free_blk_v_len; i++) {
            v2.push_back(status->free_blk_info[i]);
        }

        sort(v1.begin(), v1.end(), compare_blk_info);
        sort(v2.begin(), v2.end(), compare_blk_info);

        _test_succ = Compare_Blk_Info_Vect(v1, v2);
    }

    if (_test_succ && alloc_blk_v_len) {
        vector<block_info_t> v1, v2;

        Transfer_Block_Info(v1, alloc_blk_v, alloc_blk_v_len);
        for (int i = 0; i < alloc_blk_v_len; i++)
            v2.push_back(status->alloc_blk_info[i]);

        sort(v1.begin(), v1.end(), compare_blk_info);
        sort(v2.begin(), v2.end(), compare_blk_info);

        _test_succ = Compare_Blk_Info_Vect(v1, v2);
    }

    lm_free_status(const_cast<lm_status_t*>(status));
}

int
main(int argc, char** argv) {
    fprintf(stdout, ">>Unmap unit testing\n");

    // Notation for address.
    //    page-n: if it show up in the beginning position, it means,
    //            begining-addr of page-n. otherwise, it means the address
    //            of the last byte of page-n.
    //
    //    page-n:offset:  address of the "page-n + offset".
    //
    //    e.g.  [page0 - page5:123] = [ 0 - 5 * page-size + 123]
    //
    // Test1: unmapping the trailing port of mapped area
    {
        UNIT_TEST ut(1, 8);
        ut.Mmap(5, 123);     // map [page0 - page5:123]
        ut.Unmap(3, 2, 120); // unmap [page3 - page5:120]

        // allocated block is [page0 - page2], and unallocated is [page4:page7].
        blk_info2_t alloc_blk[] = { {0, 2, 3, 0} };
        blk_info2_t free_blk[] = { {4, 2, 4, 0} };
        ut.VerifyStatus(alloc_blk, ARRAY_SIZE(alloc_blk),
                        free_blk, ARRAY_SIZE(free_blk));
    }

    // Test2: unmapping the leading port of mapped area
    {
        UNIT_TEST ut(2, 8);
        ut.Mmap(5, 123);     // map [page0 - page5:123]
        ut.Unmap(0, 3, 450); // unmap [page0 - page3:450]

        // allocated [page4 - page5:123], and free blocks are :
        //   - [page0 - page4]. and
        //   - [page6 - page7]
        blk_info2_t alloc_blk[] = { {4, 1, 1, 123} };
        blk_info2_t free_blk[] = { {0, 2, 4, 0}, {6, 1, 2, 0}};
        ut.VerifyStatus(alloc_blk, ARRAY_SIZE(alloc_blk),
                        free_blk, ARRAY_SIZE(free_blk));
    }

    return fail_num == 0 ? 0 : -1;
}
