.PHONY = all clean test
default : all

# This Makefile is to build following building blocks
#
AR_NAME := libljmm.a
SO_NAME := libljmm.so

OBJ_COMBINED := ljmm-combined.o
OBJ_COMBINED_PIC := ljmm-combined_dyn.o

# For testing and benchmarking, see details in adaptor.c
ADAPTOR_SO_NAME := libljmm4adaptor.so

OPT_FLAGS = -O3 -g -DDEBUG
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
ADAPTOR_SO_OBJ = $(addprefix obj/so/adaptor_, $(C_OBJS))

# Testing targets and Misc
#
DEMO_NAME := demo
DEMO_SRCS = demo.c

# Highest level dependency
all: $(AR_NAME) $(SO_NAME) $(ADAPTOR_SO_NAME) $(RBTREE_TEST) \
      $(DEMO_NAME) $(UNIT_TEST) $(OBJ_COMBINED) $(OBJ_COMBINED_PIC)

test $(DEMO_NAME): $(AR_NAME) $(SO_NAME) $(SO_4_ADAPTOR_NAME)

-include ar_dep.txt
-include so_dep.txt
-include adaptor_so_dep.txt
-include demo_dep.txt

#####################################################################
#
#  		Building static lib
#
#####################################################################
#
$(AR_NAME) $(OBJ_COMBINED) : $(AR_OBJ)
	$(AR) cru $@ $(AR_OBJ)
	ld -r $(AR_OBJ) -o $(OBJ_COMBINED)
	cat $(BUILD_AR_DIR)/*.d > ar_dep.txt

$(AR_OBJ) : $(BUILD_AR_DIR)/%.o : %.c
	$(CC) -c $(CFLAGS) $(AR_BUILD_CFLAGS) $< -o $@

#####################################################################
#
#  		Building shared lib
#
#####################################################################
$(SO_OBJ) : $(BUILD_SO_DIR)/%.o : %.c
	$(CC) -c $(CFLAGS) $(SO_BUILD_CFLAGS) $< -o $@

$(SO_NAME) $(OBJ_COMBINED_PIC): $(SO_OBJ)
	$(CC) $(CFLAGS) $(SO_BUILD_CFLAGS) $(SO_OBJ) -shared -o $(SO_NAME)
	ld -r $(SO_OBJ) -o $(OBJ_COMBINED_PIC)

$(ADAPTOR_SO_OBJ) : $(BUILD_SO_DIR)/adaptor_%.o : %.c
	$(CC) -c $(CFLAGS) $(SO_BUILD_CFLAGS) -DFOR_ADAPTOR $< -o $@

$(ADAPTOR_SO_NAME) :  $(ADAPTOR_SO_OBJ)
	$(CC) $(CFLAGS) $(SO_BUILD_CFLAGS) $(ADAPTOR_SO_OBJ) -DFOR_ADAPTOR\
    -shared -o $@
	cat ${ADAPTOR_SO_OBJ:%.o=%.d} > adaptor_so_dep.txt

#####################################################################
#
#  		Building demo program
#
#####################################################################
$(DEMO_NAME) : ${DEMO_SRCS:%.c=%.o} $(AR_NAME)
	$(CC) $(filter %.o, $+) -L. -Wl,-static -lljmm -Wl,-Bdynamic -o $@
	cat ${DEMO_SRCS:%.c=%.d} > demo_dep.txt
%.o : %.c
	$(CC) $(CFLAGS) -c $<

%.o : %.cxx
	$(CXX) $(CXXFLAGS) -c $<

clean:
	rm -f *.o *.d *_dep.txt $(BUILD_AR_DIR)/*.[do] $(BUILD_SO_DIR)/*.[od]
	rm -f $(AR_NAME) $(SO_NAME) $(DEMO_NAME)
	make -C tests clean

test:
	make all -C tests
