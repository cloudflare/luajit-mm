#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "lj_mm.h"

static void*
mmap_wrap(size_t len) {
    return lm_mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_32BIT | MAP_PRIVATE, -1, 0);
}

int
main(int argc, char** argv) {
    lm_init(1);
    dump_page_alloc(stderr);

    int size1 = 100;
    char* p1 = mmap_wrap(size1);
    fprintf(stderr, "\nsize=%d, %p\n", size1, p1);
    dump_page_alloc(stderr);


    int size2 = 4097;
    char* p2 = mmap_wrap(size2);
    fprintf(stderr, "\nsize=%d, %p\n", size2, p2);
    dump_page_alloc(stderr);

    int size3 = 4097;
    char* p3 = mmap_wrap(size3);
    fprintf(stderr, "\nsize=%d %p\n", size3, p3);
    dump_page_alloc(stderr);

    int size4 = 4096 * 3;
    char* p4 = mmap_wrap(size4);
    fprintf(stderr, "\nsize=%d %p\n", size4, p4);
    dump_page_alloc(stderr);

    int size5 = 4096 * 2;
    char* p5 = mmap_wrap(size5);
    fprintf(stderr, "\nsize=%d %p\n", size5, p5);
    dump_page_alloc(stderr);

    lm_munmap(p1, size1);
    lm_munmap(p2, size2);
    lm_munmap(p3, size3);
    lm_munmap(p4, size4);
    lm_munmap(p5, size5);

    fprintf(stderr, "\n\nAfter delete all allocations\n");
    dump_page_alloc(stderr);
    /*lm_fini(); */

    return 0;
}
