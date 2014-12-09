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

class UNIT_TEST;
class MemExt {
public:
    MemExt(UNIT_TEST& ut, int page_num, int fraction,
            int first_page = -1) :
           _ut(ut), _page_num(page_num), _fraction(fraction),
           _first_page(first_page) {}

    inline size_t getLen() const;
    inline char* getStartAddr() const;
    char* getEndAddr() const { return getStartAddr() + getLen(); }

private:
    UNIT_TEST& _ut;
    int _page_num;
    int _fraction;
    int _first_page;
};

class UNIT_TEST {
public:
    UNIT_TEST(int test_id, int page_num);
    ~UNIT_TEST();

    void VerifyStatus(blk_info2_t* alloc_blk_v, int alloc_blk_v_len,
                      blk_info2_t* free_blk_v, int free_blk_v_len);

    int getPageSize() const { return _page_size; }
    char* getChunkBase() const { return _chunk_base; }
    char* getPageAddr(int page_idx) const {
        return _chunk_base + page_idx * getPageSize();
    }

    // Allocate "full_page_num * page-size + fraction" bytes.
    bool Alloc(const MemExt&);
    bool Mmap(const MemExt&);

    // Munmap [first-page : first + page_num * page-size + fraction].
    bool Munmap(const MemExt&);
    bool Mremap(const MemExt& old, const MemExt& new_ext, bool maymove=true);

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
    char* _chunk_base;
};

inline size_t
MemExt::getLen() const {
    return _ut.getPageSize() * (size_t)_page_num + _fraction;
}

inline char*
MemExt::getStartAddr() const {
    return _ut.getChunkBase() + _ut.getPageSize() * _first_page;
}

UNIT_TEST::UNIT_TEST(int test_id, int page_num)
    : _test_id(test_id) {
    ljmm_opt_t mm_opt;

    lm_init_mm_opt(&mm_opt);
    mm_opt.dbg_alloc_page_num = _page_num = page_num;
    mm_opt.mode = LM_USER_MODE;

    _init_succ = lm_init2(&mm_opt);
    _test_succ = _init_succ ? true : false;
    _page_size = sysconf(_SC_PAGESIZE);
    if (_init_succ) {
        const lm_status_t* status = ljmm_get_status();
        _chunk_base = status->first_page;
        lm_free_status(const_cast<lm_status_t*>(status));
    } else {
        _chunk_base = NULL;
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
UNIT_TEST::Alloc(const MemExt& mem_ext) {
    if (!_init_succ || !_test_succ)
        return false;

    void* p = lm_malloc(mem_ext.getLen());
    if (!p) {
        _test_succ = false;
        return false;
    }

    return true;
}

// allocate "page_num * page_size + page_fraction" bytes.
//
bool
UNIT_TEST::Mmap(const MemExt& mem_ext) {
    if (!_init_succ || !_test_succ)
        return false;

    void* p = lm_mmap(NULL, mem_ext.getLen(),
                      PROT_READ|PROT_WRITE,
                      MAP_32BIT|MAP_PRIVATE|MAP_ANONYMOUS,
                      -1, 0);
    _test_succ = (p != MAP_FAILED);
    return _test_succ;
}

bool
UNIT_TEST::Munmap(const MemExt& mem_ext) {
    if (!_init_succ || !_test_succ)
        return false;

    _test_succ = (lm_munmap(mem_ext.getStartAddr(), mem_ext.getLen()) == 0);
    return _test_succ;
}

bool
UNIT_TEST::Mremap(const MemExt& old, const MemExt& new_one, bool maymove) {
    if (!_init_succ || !_test_succ)
        return false;

    void* r;
    r = lm_mremap(old.getStartAddr(), old.getLen(), new_one.getLen(),
                  maymove ? MREMAP_MAYMOVE : 0);

    _test_succ = (r != MAP_FAILED);
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

    const lm_status_t* status = ljmm_get_status();
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
    fprintf(stdout, "\n>>Mmap unit testing\n");
    // test1
    //
    {
        //allocate chunk containing 2+4+8 pages.
        UNIT_TEST ut(1, 2+4+8);

        // allocate 103 bytes
        ut.Mmap(MemExt(ut, 0, 103));

        // allocate 1 page + 101 byte
        ut.Mmap(MemExt(ut, 1, 101));

        // allocate 104 bytes
        ut.Mmap(MemExt(ut, 0, 104));

        blk_info2_t free_blk[] = { {6, 3, 8, 0} /* 8-page block */,
                                   {4, 1, 2, 0} /* splitted from 4-page blk*/,
                                 };

        blk_info2_t alloc_blk[] = { {2, 1, 1, 101},
                                    {0, 0, 0, 103},
                                    {1, 0, 0, 104}};

        ut.VerifyStatus(alloc_blk, ARRAY_SIZE(alloc_blk),
                        free_blk, ARRAY_SIZE(free_blk));
    }
    fprintf(stdout, "\n>>Munmap unit testing\n");

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
        ut.Mmap(MemExt(ut, 5, 123));      // map [page0 - page5:123]
        ut.Munmap(MemExt(ut, 2, 120, 3)); // unmap [page3 - page5:120]

        // allocated block is [page0 - page2], and unallocated is [page4:page7].
        blk_info2_t alloc_blk[] = { {0, 2, 3, 0} };
        blk_info2_t free_blk[] = { {4, 2, 4, 0} };
        ut.VerifyStatus(alloc_blk, ARRAY_SIZE(alloc_blk),
                        free_blk, ARRAY_SIZE(free_blk));
    }

    // Test2: unmapping the leading port of mapped area
    {
        UNIT_TEST ut(2, 8);
        ut.Mmap(MemExt(ut, 5, 123));     // map [page0 - page5:123]
        ut.Munmap(MemExt(ut, 3, 450, 0));// unmap [page0 - page3:450]

        // allocated [page4 - page5:123], and free blocks are :
        //   - [page0 - page4]. and
        //   - [page6 - page7]
        blk_info2_t alloc_blk[] = { {4, 1, 1, 123} };
        blk_info2_t free_blk[] = { {0, 2, 4, 0}, {6, 1, 2, 0}};
        ut.VerifyStatus(alloc_blk, ARRAY_SIZE(alloc_blk),
                        free_blk, ARRAY_SIZE(free_blk));
    }

    fprintf(stdout, "\n>>Remap unit testing\n");

    // Test1: remap, expand in place
    {
        // allocate 16-page chunk, then allocate 2 pages, then expand allocated
        // block to 7 pages.
        UNIT_TEST ut(1, 16);
        ut.Mmap(MemExt(ut, 1, 123)); // allocate one-page + 123-byte.

        // expand to 6-page + 234 bytes.
        ut.Mremap(MemExt(ut, 1, 123, 0), MemExt(ut, 6, 234));

        blk_info2_t alloc_blk[] = { {0, 3, 6, 234} };
        blk_info2_t free_blk[] = { {8, 3, 8, 0} };
        ut.VerifyStatus(alloc_blk, ARRAY_SIZE(alloc_blk),
                        free_blk, ARRAY_SIZE(free_blk));
    }

    // Test2: remap, expand and move
    {
        // similar to the test1, except that it's not able to expand the
        // allocated by merge free buddy blocks. It needs to allocate a new
        // blocks, and copy the original content to the new one.
        //
        UNIT_TEST ut(2, 16);

        ut.Mmap(MemExt(ut, 1, 123)); // allocate a block (blk1) w/ one-page + 123-byte.
        ut.Mmap(MemExt(ut, 2, 456)); // allocate a block (blk2) w/ two-page + 456-byte.
        // So the blk1 extends [page0..page1], while the blk2 extends [page4 .. page6]

        // blk1 relocate to [page8 .. page13] ut.Mremap(MemExt(ut, 1, 123, 0), MemExt(ut, 6, 234));
        ut.Mremap(MemExt(ut, 1, 123, 0), MemExt(ut, 6, 234));

        blk_info2_t alloc_blk[] = { {8, 3, 6, 234}/*blk1*/, {4, 2, 2, 456} /* blk2*/ };
        blk_info2_t free_blk[] = { {0, 2, 4, 0}};
        ut.VerifyStatus(alloc_blk, ARRAY_SIZE(alloc_blk),
                        free_blk, ARRAY_SIZE(free_blk));
    }

    // Test 3: remap, shrink
    {
        UNIT_TEST ut(3, 16);

        ut.Mmap(MemExt(ut, 1, 123)); // allocate a block (blk1) w/ one-page + 123-byte.
        ut.Mmap(MemExt(ut, 2, 456)); // allocate a block (blk2) w/ two-page + 456-byte.

        // shrink the blk2 from 2*page+456-byte to 1*page + 12 bytes
        ut.Mremap(MemExt(ut, 2, 456, 4), MemExt(ut, 1, 12, 4));

        blk_info2_t alloc_blk[] = { {0, 1, 1, 123}/*blk1*/, {4, 1, 1, 12} /* blk2*/ };
        blk_info2_t free_blk[] = { {2, 1, 2, 0} /* blk1's buddy */, {6, 1, 2, 0}, {8, 3, 8, 0}};
        ut.VerifyStatus(alloc_blk, ARRAY_SIZE(alloc_blk),
                        free_blk, ARRAY_SIZE(free_blk));
    }

    return fail_num == 0 ? 0 : -1;
}
