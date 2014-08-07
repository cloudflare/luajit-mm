luajit-mm
=========

Luajit take full advantage of lower 2G memory on AMD64 platform.

Rumdimentary implementation. Not yet fully tested. Not yet cleanup the code.
Quite a few optimizatio is not yet implemented.

Immediate todo
==============

    o.Refine and finish this README.
    o.test, add enhancements.

problem statement:
==================
  On Linux/x86-64 platform, Luajit can use no more than 1G memory due to the
combination of bunch of nasty issues. 1G is way too small for server-side application.

  This package is trying to replace mmap/munmap/mremap with hence provide up to
about 2G space.


Basic ideas
===========
    o. When a application, which contain luajit, is launched, reserve the the space
       from where `sbrk(0)` indidate all the way to 2G.

    o. Perform page allocation on the reserved space. the mmap/munmap/mremap is built
       on this page allocation. Currently, we use buddy allocation for page allocation
       with some optimizations in an attemp to reduce working set.
