# RA8M2 feature-ID dump firmware — built with ATfE (clang/lld), no libc.
CC      := clang
DEVICE  ?= R7KA8M2AF
JLINK_SN?=
CPUFLAGS:= --target=arm-none-eabi -mcpu=cortex-m85 -mfloat-abi=hard -mthumb
CFLAGS  := $(CPUFLAGS) -O2 -g -std=c11 -ffreestanding -fno-builtin -nostdlib \
           -fno-exceptions -fno-unwind-tables -ffunction-sections -fdata-sections \
           -Wall -Wextra
LDFLAGS := $(CPUFLAGS) -nostdlib -fuse-ld=lld -T link.ld -Wl,--gc-sections -Wl,-Map=idreg.map

SRCS := startup.c rtt.c main.c
OBJS := $(SRCS:.c=.o)

all: idreg.elf idreg.bin idreg.hex
	llvm-size idreg.elf

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

idreg.elf: $(OBJS) link.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@

idreg.bin: idreg.elf
	llvm-objcopy -O binary $< $@
idreg.hex: idreg.elf
	llvm-objcopy -O ihex $< $@

# --- J-Link ---
JL := JLinkExe -NoGui 1 -device $(DEVICE) -if SWD -speed 4000 $(if $(JLINK_SN),-SelectEmuBySN $(JLINK_SN),)

flash: idreg.elf
	printf 'r\nh\nloadfile idreg.elf\nr\ng\nexit\n' > .flash.jlink
	$(JL) -CommandFile .flash.jlink

# Stream RTT channel 0 to the terminal via libjlinkarm (pylink). The RTT
# control-block address is taken from the ELF symbol table because J-Link's
# auto-search does not cover RA8M2 SRAM (0x22000000). Ctrl-C to stop;
# typed characters go to down-channel 0 (the firmware re-dumps on any byte).
rtt: idreg.elf
	python3 rtt.py --device $(DEVICE) --elf idreg.elf $(if $(JLINK_SN),--sn $(JLINK_SN),) --reset

# Interactive RTT terminal (needs a real tty): JLinkExe in the foreground
# starts the RTT telnet server; connect with `JLinkRTTClient` in a 2nd shell.
rtt-server:
	$(JL) -RTTTelnetPort 19021 -AutoConnect 1

clean:
	rm -f $(OBJS) idreg.elf idreg.bin idreg.hex idreg.map rtt.log dump.txt .flash.jlink .rtt.jlink

.PHONY: all flash rtt rtt-server clean
