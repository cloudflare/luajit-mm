.PHONY = all clean test
default : all

# This Makefile is to build following building blocks
#
AR_NAME := libljmm.a
SO_NAME := libljmm.so

OPT_FLAGS = -O3 -g -march=native -DDEBUG
CFLAGS = -DENABLE_TESTING -fvisibility=hidden -MMD -Wall $(OPT_FLAGS)
CXXFLAGS = $(CFLAGS)

# Addition flag for building libljmm.a and libljmm.so respectively.
# They are not necessarily applicable to other *.a and *.so.
#
AR_BUILD_CFLAGS = -DBUILDING_LIB
SO_BUILD_CFLAGS = -DBUILDING_LIB -fPIC

CC ?= gcc
CXX ?= g++

BUILD_AR_DIR = obj/lib
BUILD_SO_DIR = obj/so

RB_TREE_SRCS = rbtree.c
ALLOC_SRCS = chunk.c block_cache.c page_alloc.c mem_map.c

C_SRCS = $(RB_TREE_SRCS) $(ALLOC_SRCS)
C_OBJS = ${C_SRCS:%.c=%.o}

AR_OBJ = $(addprefix obj/lib/, $(C_OBJS))
SO_OBJ = $(addprefix obj/so/, $(C_OBJS))

# Testing targets and Misc
#
UNIT_TEST := unit_test
ADAPTOR := libadaptor.so
RBTREE_TEST := rbt_test
DEMO_NAME := demo

UNIT_TEST_SRCS = unit_test.cxx
ADAPTOR_SRCS = adaptor.c
RB_TEST_SRCS = rb_test.cxx
DEMO_SRCS = demo.c

# Highest level dependency
all: $(AR_NAME) $(SO_NAME) $(RBTREE_TEST) $(DEMO_NAME) $(UNIT_TEST) $(ADAPTOR)

$(RBTREE_TEST) $(DEMO_NAME) $(UNIT_TEST) $(ADAPTOR): $(AR_NAME) $(SO_NAME)

-include ar_dep.txt
-include so_dep.txt
-include adaptor_dep.txt

#####################################################################
#
#  		Building static lib
#
#####################################################################
#
$(AR_NAME) : $(AR_OBJ)
	$(AR) cru $@ $(AR_OBJ)
	cat $(BUILD_AR_DIR)/*.d > ar_dep.txt

$(AR_OBJ) : $(BUILD_AR_DIR)/%.o : %.c
	$(CC) -c $(CFLAGS) $(AR_BUILD_CFLAGS) $< -o $@

#####################################################################
#
#  		Building shared lib
#
#####################################################################
$(SO_NAME) : $(SO_OBJ)
	$(CC) $(CFLAGS) $(AR_BUILD_CFLAGS) $(SO_OBJ) -shared -o $@
	cat $(BUILD_SO_DIR)/*.d > so_dep.txt

$(SO_OBJ) : $(BUILD_SO_DIR)/%.o : %.c
	$(CC) -c $(CFLAGS) $(SO_BUILD_CFLAGS) $< -o $@

#####################################################################
#
#  		Building demo program
#
#####################################################################
$(DEMO_NAME) : ${DEMO_SRCS:%.c=%.o} $(AR_NAME)
	$(CC) $(filter %.o, $+) -L. -Wl,-static -lljmm -Wl,-Bdynamic -o $@

$(UNIT_TEST) : ${UNIT_TEST_SRCS:%.cxx=%.o} $(AR_NAME)
	$(CXX) $(filter %.o, $+) -L. -Wl,-static -lljmm -Wl,-Bdynamic -o $@

$(RBTREE_TEST) : ${RB_TREE_SRCS:%.c=%.o} ${RB_TEST_SRCS:%.cxx=%.o}
	$(CXX) $(filter %.o, $+) -o $@

%.o : %.c
	$(CC) $(CFLAGS) -c $<

%.o : %.cxx
	$(CXX) $(CXXFLAGS) -c $<

#####################################################################
#
#  		Building testing/benchmark stuff
#
#####################################################################
test : $(RBTREE_TEST) $(UNIT_TEST)
	@echo "RB-tree unit testing"
	./$(RBTREE_TEST)
	@echo ""
	@echo "Memory management unit testing"
	./$(UNIT_TEST)

${ADAPTOR_SRCS:%.c=%.o} : %.o :  %.c
	$(CC) $(CFLAGS) -fvisibility=default -MMD -Wall -fPIC -I. -c $<

$(ADAPTOR) : ${ADAPTOR_SRCS:%.c=%.o}
	$(CC) $(CFLAGS) -fvisibility=default -shared $(filter %.o, $+) -L. -lljmm -ldl -o $@
	cat ${ADAPTOR_SRCS:%.c=%.d} > adaptor_dep.txt

clean:
	rm -f *.o *.d *_dep.txt $(BUILD_AR_DIR)/* $(BUILD_SO_DIR)/*
	rm -f $(AR_NAME) $(SO_NAME) $(RBTREE_TEST) $(DEMO_NAME) $(ADAPTOR)
