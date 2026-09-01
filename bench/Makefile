# Cortex-M85 string-function benchmark. ATfE clang + lld.
CC       := clang
CXX      := clang++
DEVICE   ?= R7KA8M2AF
LIBC_SRC ?= $(HOME)/Documents/llvm/llvm-project/libc

CPUFLAGS := --target=arm-none-eabi -mcpu=cortex-m85 -mfloat-abi=hard -mthumb
CFLAGS   := $(CPUFLAGS) -O2 -g -std=c11 -ffreestanding -fno-builtin -Wall -Wextra \
            -ffunction-sections -fdata-sections
LDFLAGS  := $(CPUFLAGS) -nostartfiles -fuse-ld=lld -T link.ld -Wl,--gc-sections -Wl,-Map=bench.map

# llvm-libc tip: compile the real sources, then rename the mangled entry points.
LIBC_CXXFLAGS := $(CPUFLAGS) -O2 -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -nostdinc++ \
                 -DLIBC_NAMESPACE=__llvm_libc \
                 -I$(LIBC_SRC) -I$(LIBC_SRC)/include -ffunction-sections -fdata-sections
LIBC_FUNCS := memcpy memmove memset memcmp strlen memchr
LIBC_OBJS  := $(addprefix llvm_,$(addsuffix .o,$(LIBC_FUNCS))) llvm_bcmp.o llvmw_strlen.o llvmw_memchr.o

OBJS := startup.o rtt.o ref.o av.o impl_word.o impl_mve.o bench.o $(LIBC_OBJS)

all: bench.elf
	llvm-size bench.elf

ref.o: ref.c impls.h
	$(CC) $(CFLAGS) -fno-vectorize -fno-slp-vectorize -c $< -o $@
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# rename: every defined T symbol foo -> prefix_foo (demangled function name)
define RENAME
	@for s in $$(llvm-nm --defined-only $@ | awk '$$2=="T"{print $$3}'); do \
	   f=$$(llvm-cxxfilt $$s | sed -E 's/^__llvm_libc::([a-z_]+)\(.*/\1/'); \
	   llvm-objcopy --redefine-sym $$s=$(1)_$$f $@; done
endef

llvm_%.o: $(LIBC_SRC)/src/string/%.cpp
	$(CXX) $(LIBC_CXXFLAGS) -c $< -o $@
	$(call RENAME,llvm)
llvm_bcmp.o: $(LIBC_SRC)/src/strings/bcmp.cpp
	$(CXX) $(LIBC_CXXFLAGS) -c $< -o $@
	$(call RENAME,llvm)
llvmw_%.o: $(LIBC_SRC)/src/string/%.cpp
	$(CXX) $(LIBC_CXXFLAGS) -DLIBC_COPT_STRING_LENGTH_IMPL=word -DLIBC_COPT_FIND_FIRST_CHARACTER_IMPL=word -c $< -o $@
	$(call RENAME,llvmw)

bench.elf: $(OBJS) link.ld
	$(CC) $(LDFLAGS) $(OBJS) -lc -o $@

JL := JLinkExe -NoGui 1 -device $(DEVICE) -if SWD -speed 4000
flash: bench.elf
	printf 'r\nh\nloadfile bench.elf\nr\ng\nexit\n' > .flash.jlink
	$(JL) -CommandFile .flash.jlink

run: flash
	python3 rtt.py --device $(DEVICE) --elf bench.elf --reset --timeout 20 | tee results.csv
	python3 report.py results.csv > RESULTS.md

clean:
	rm -f *.o bench.elf bench.map results.csv .flash.jlink

.PHONY: all flash run clean
