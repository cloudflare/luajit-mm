# This Makefile is to build following building blocks
#
AR_NAME := libljmm.a
SO_NAME := libljmm.so
RBTREE_TEST := rbt_test  # test program for red-black tree
DEMO_NAME := demo        # A demo illustrating how to use the lib being built.

OPT_FLAGS = -O0 -g -DDEBUG
CFLAGS = -fvisibility=hidden -MMD -Wall $(OPT_FLAGS)
CXXFLAGS = $(CFLAGS)
AR_BUILD_CFLAGS = -DBUILDING_LIB
SO_BUILD_CFLAGS = -DBUILDING_LIB -fPIC

CC ?= gcc
CXX ?= g++

BUILD_AR_DIR = obj/lib
BUILD_SO_DIR = obj/so

RB_TREE_SRCS = rbtree.c
RB_TEST_SRCS = rb_test.cxx
ALLOC_SRCS = trunk.c page_alloc.c
DEMO_SRCS = demo.c

C_SRCS = $(RB_TREE_SRCS) $(ALLOC_SRCS)
C_OBJS = ${C_SRCS:%.c=%.o}

AR_OBJ = $(addprefix obj/lib/, $(C_OBJS))
SO_OBJ = $(addprefix obj/so/, $(C_OBJS))

.PHONY = all clean test

all: $(AR_NAME) $(SO_NAME) $(RBTREE_TEST) $(DEMO_NAME)

$(RBTREE_TEST) $(DEMO_NAME) : $(AR_NAME) $(SO_NAME)

-include ar_dep.txt
-include so_dep.txt

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

$(SO_OBJ) : $(BUILD_SO_DIR)/%.o : %.c
	$(CC) -c $(CFLAGS) $(SO_BUILD_CFLAGS) $< -o $@

#####################################################################
#
#  		Building demo program
#
#####################################################################
$(DEMO_NAME) : ${DEMO_SRCS:%.c=%.o} $(AR_NAME)
	$(CC) $(filter %.o, $+) -L. -Wl,-static -lljmm -Wl,-Bdynamic -o $@

$(RBTREE_TEST) : ${RB_TREE_SRCS:%.c=%.o} ${RB_TEST_SRCS:%.cxx=%.o}
	$(CXX) $(filter %.o, $+) -o $@

%.o : %.c
	$(CC) $(CFLAGS) -c $<

%.o : %.cxx
	$(CXX) $(CXXFLAGS) -c $<

clean:
	rm -f *.o *.d ar_dep.txt so_dep.txt $(BUILD_AR_DIR)/* $(BUILD_SO_DIR)/*
	rm -f $(AR_NAME) $(SO_NAME) $(RBTREE_TEST) $(DEMO_NAME)
