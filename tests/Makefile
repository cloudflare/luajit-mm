.PHONY = default all clean

default : all

OPT_FLAGS := -O3 -g -march=native -DENABLE_TESTING #-DDEBUG
CFLAGS := -I.. -fvisibility=hidden -MMD -Wall $(OPT_FLAGS)
CXXFLAGS = $(CFLAGS)

CC = gcc
CXX = g++

# Targets to be built.
UNIT_TEST := unit_test
ADAPTOR := libadaptor.so
RBTREE_TEST := rbt_test
MYMALLOC  := libmymalloc.so

# Source codes
UNIT_TEST_SRCS = unit_test.cxx
ADAPTOR_SRCS = adaptor.c mymalloc.c
RB_TEST_SRCS = rb_test.cxx
MYMALLOC_SRCS = mymalloc.c

-include adaptor_dep.txt
-include mymalloc_dep.txt


all : $(UNIT_TEST) $(ADAPTOR) $(RBTREE_TEST) $(MYMALLOC)
	./$(RBTREE_TEST)
	./$(UNIT_TEST)

# Building unit-test
${UNIT_TEST_SRCS:%.cxx=%.o} : %.o : %.cxx
	$(CXX) $(CXXFLAGS) $< -c

$(UNIT_TEST) : ${UNIT_TEST_SRCS:%.cxx=%.o} ../libljmm.so
	$(CXX) $(filter %.o, $^) -Wl,-rpath=.. -L.. -lljmm -o $@

# Building libadpator.so.
#
${ADAPTOR_SRCS:%.c=%.o} : %.o :  %.c
	$(CC) $(CFLAGS) -fvisibility=default -DFOR_ADAPTOR -fPIC -I.. -I. -c $<

$(ADAPTOR) : ${ADAPTOR_SRCS:%.c=%.o} ../libljmm4adaptor.so
	ln -fs ../libljmm4adaptor.so .
	$(CC) $(CFLAGS) -fvisibility=default -shared $(filter %.o, $^) -L.. -lljmm4adaptor \
        -Wl,-rpath=.. -o $@
	cat ${ADAPTOR_SRCS:%.c=%.d} > adaptor_dep.txt

# Building RB-tree unit-test

${RB_TEST_SRCS:%.cxx=%.o} : %.o : %.cxx
	$(CXX) $(CXXFLAGS) -I.. $< -c

${RBTREE_TEST} : ${RB_TEST_SRCS:%.cxx=%.o} ../rbtree.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# Building mymalloc.so
${MYMALLOC_SRCS:%.c=my_%.o} : my_%.o : %.c
	$(CC) $(CFLAGS) -fvisibility=default -fPIC -c $< -o $@

$(MYMALLOC) : ${MYMALLOC_SRCS:%.c=my_%.o}
	$(CC) $+ $(CFLAGS) -fvisibility=default -shared -o $@
	cat ${MYMALLOC_SRCS:%.c=%.d} > mymalloc_dep.txt

clean:
	rm -rf *.o *.d *_dep.txt $(UNIT_TEST) $(ADAPTOR) $(RBTREE_TEST) $(MYMALLOC) *.so
