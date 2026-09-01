# Standalone llvm-libc hsearch benchmark for Cortex-M85. ATfE clang + lld.
#
# Each variant TU gets its OWN LIBC_NAMESPACE: the table code is all
# LIBC_INLINE (weak, identical mangled names), so with a shared namespace the
# linker deduplicates HashTable::find/insert across variants and every variant
# silently runs the same code (and crashes when layouts differ).
CC       := clang
CXX      := clang++
DEVICE   ?= R7KA8M2AF
LIBC_SRC ?= $(HOME)/Documents/llvm/llvm-project/libc

CPUFLAGS := --target=arm-none-eabi -mcpu=cortex-m85 -mfloat-abi=hard -mthumb
CFLAGS   := $(CPUFLAGS) -O2 -g -ffreestanding -Wall -Wextra -ffunction-sections -fdata-sections
CXXFLAGS := $(CPUFLAGS) -O2 -g -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -nostdinc++ \
            -I$(LIBC_SRC) -I$(LIBC_SRC)/include -I. -ffunction-sections -fdata-sections
LDFLAGS  := $(CPUFLAGS) -nostartfiles -fuse-ld=lld -T ../link.ld -Wl,--gc-sections -Wl,-Map=hbench.map

FLAGS_lh   := -DVARIANT=lh   -DLIBC_NAMESPACE=__llvm_lh
FLAGS_lh8  := -DVARIANT=lh8  -DLIBC_NAMESPACE=__llvm_lh8  -DHS_USE_GRP8
FLAGS_lhm  := -DVARIANT=lhm  -DLIBC_NAMESPACE=__llvm_lhm  -DHS_USE_MVE_GROUP
FLAGS_lhc  := -DVARIANT=lhc  -DLIBC_NAMESPACE=__llvm_lhc  -DHS_USE_CHEAP_HASH
FLAGS_lh8c := -DVARIANT=lh8c -DLIBC_NAMESPACE=__llvm_lh8c -DHS_USE_GRP8 -DHS_USE_CHEAP_HASH
FLAGS_lhmc := -DVARIANT=lhmc -DLIBC_NAMESPACE=__llvm_lhmc -DHS_USE_MVE_GROUP -DHS_USE_CHEAP_HASH

VARIANTS := lh lh8 lhm lhc lh8c lhmc
VOBJS := $(addprefix hs_,$(addsuffix .o,$(VARIANTS)))
OBJS := startup.o rtt.o shim.o hbench.o $(VOBJS)

all: hbench.elf
	llvm-size hbench.elf

startup.o: ../startup.c
	$(CC) $(CFLAGS) -c $< -o $@
rtt.o: ../rtt.c
	$(CC) $(CFLAGS) -fno-builtin -c $< -o $@
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

hs_%.o: llvm_hsearch.cpp mve_group.h grp8.h cheaphash.h
	$(CXX) $(CXXFLAGS) $(FLAGS_$*) -c $< -o $@

hbench.elf: $(OBJS) ../link.ld
	$(CC) $(LDFLAGS) $(OBJS) -lc -o $@

JL := JLinkExe -NoGui 1 -device $(DEVICE) -if SWD -speed 4000
flash: hbench.elf
	printf 'r\nh\nloadfile hbench.elf\nr\ng\nexit\n' > .flash.jlink
	$(JL) -CommandFile .flash.jlink

run: flash
	python3 ../rtt.py --device $(DEVICE) --elf hbench.elf --reset --timeout 20 | tee results.csv
	python3 report.py results.csv > RESULTS.md

clean:
	rm -f *.o hbench.elf hbench.map results.csv .flash.jlink

.PHONY: all flash run clean
