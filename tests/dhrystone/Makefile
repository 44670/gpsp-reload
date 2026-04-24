CORE_DIR := ../..
DHRY_DIR := third_party
DHRY_BASE_URL := https://raw.githubusercontent.com/sifive/benchmark-dhrystone/master

ARM_GCC ?= arm-none-eabi-gcc
ARM_OBJCOPY ?= arm-none-eabi-objcopy
CC ?= gcc
CURL ?= curl

DHRY_ITERS ?= 2000
MAX_FRAMES ?= 300

CORE_SO := $(abspath ./gpsp_dhry_libretro.so)
ROM_ARM_ELF := dhrystone_arm.elf
ROM_ARM_BIN := dhrystone_arm.gba
ROM_ARM_MAP := $(ROM_ARM_ELF:.elf=.map)
ROM_ARM_OBJDUMP := $(ROM_ARM_ELF:.elf=.objdump)
ROM_THUMB_ELF := dhrystone_thumb.elf
ROM_THUMB_BIN := dhrystone_thumb.gba
ROM_THUMB_MAP := $(ROM_THUMB_ELF:.elf=.map)
ROM_THUMB_OBJDUMP := $(ROM_THUMB_ELF:.elf=.objdump)
ROM_ELF := $(ROM_ARM_ELF)
ROM_BIN := $(ROM_ARM_BIN)
RUNNER := dhrystone_runner

HOST_INCFLAGS := -I$(CORE_DIR) -I$(CORE_DIR)/libretro -I$(CORE_DIR)/libretro/libretro-common/include -I.
HOST_CFLAGS := -O2 -Wall $(HOST_INCFLAGS)

COMMON_GBA_CFLAGS := -mcpu=arm7tdmi -O2 -ffreestanding -fno-builtin -nostdlib \
	-std=gnu89 -Wall -Wextra -Wno-implicit-int -Wno-return-type \
	-Wno-implicit-function-declaration -Wno-int-conversion \
	-Wno-builtin-declaration-mismatch -Wno-unused-parameter \
	-I. -I$(DHRY_DIR) -DDHRY_ITERS=$(DHRY_ITERS) -DTIME
ARM_CFLAGS := $(COMMON_GBA_CFLAGS) -marm
THUMB_CFLAGS := $(COMMON_GBA_CFLAGS) -mthumb -mthumb-interwork

DHRY_SRCS := $(DHRY_DIR)/dhry.h $(DHRY_DIR)/dhry_1.c $(DHRY_DIR)/dhry_2.c

all: build

$(DHRY_DIR):
	mkdir -p $@

$(DHRY_DIR)/dhry.h: | $(DHRY_DIR)
	$(CURL) -L --fail -o $@ $(DHRY_BASE_URL)/dhry.h

$(DHRY_DIR)/dhry_1.c: | $(DHRY_DIR)
	$(CURL) -L --fail -o $@ $(DHRY_BASE_URL)/dhry_1.c

$(DHRY_DIR)/dhry_2.c: | $(DHRY_DIR)
	$(CURL) -L --fail -o $@ $(DHRY_BASE_URL)/dhry_2.c

$(ROM_ARM_ELF): crt0.S gba.ld gba_main.c gba_port.c dhry_result.h $(DHRY_SRCS)
	$(ARM_GCC) $(ARM_CFLAGS) -T gba.ld -Wl,-Map,$(@:.elf=.map) -Wl,--gc-sections \
		-o $@ crt0.S gba_main.c gba_port.c $(DHRY_DIR)/dhry_1.c $(DHRY_DIR)/dhry_2.c -lgcc

$(ROM_THUMB_ELF): crt0.S gba.ld gba_main.c gba_port.c dhry_result.h $(DHRY_SRCS)
	$(ARM_GCC) $(THUMB_CFLAGS) -T gba.ld -Wl,-Map,$(@:.elf=.map) -Wl,--gc-sections \
		-o $@ crt0.S gba_main.c gba_port.c $(DHRY_DIR)/dhry_1.c $(DHRY_DIR)/dhry_2.c -lgcc

$(ROM_ARM_BIN): $(ROM_ARM_ELF)
	$(ARM_OBJCOPY) -O binary $< $@

$(ROM_THUMB_BIN): $(ROM_THUMB_ELF)
	$(ARM_OBJCOPY) -O binary $< $@

$(ROM_ARM_OBJDUMP): $(ROM_ARM_ELF)
	arm-none-eabi-objdump -dr $< > $@

$(ROM_THUMB_OBJDUMP): $(ROM_THUMB_ELF)
	arm-none-eabi-objdump -dr $< > $@

build: $(ROM_ARM_BIN) $(ROM_THUMB_BIN) $(ROM_ARM_OBJDUMP) $(ROM_THUMB_OBJDUMP)

$(CORE_SO):
	$(MAKE) -C $(CORE_DIR) HAVE_DYNAREC=0 MMAP_JIT_CACHE=0 platform=unix TARGET=$(CORE_SO)

$(RUNNER): dhrystone_runner.c dhry_result.h $(CORE_SO)
	$(CC) $(HOST_CFLAGS) -o $@ dhrystone_runner.c $(CORE_SO) -Wl,-rpath,$(dir $(CORE_SO))

run: run-arm

run-arm: $(RUNNER) $(ROM_ARM_BIN)
	./$(RUNNER) --rom $(ROM_ARM_BIN) --max-frames $(MAX_FRAMES)

run-thumb: $(RUNNER) $(ROM_THUMB_BIN)
	./$(RUNNER) --rom $(ROM_THUMB_BIN) --max-frames $(MAX_FRAMES)

run-both: $(RUNNER) $(ROM_ARM_BIN) $(ROM_THUMB_BIN)
	./$(RUNNER) --rom $(ROM_ARM_BIN) --max-frames $(MAX_FRAMES)
	./$(RUNNER) --rom $(ROM_THUMB_BIN) --max-frames $(MAX_FRAMES)

run-jsjit: $(ROM_ARM_BIN)
	node ./jsjit_runner.js --rom $(ROM_ARM_BIN)

clean:
	rm -f $(CORE_SO) $(ROM_ARM_ELF) $(ROM_ARM_BIN) $(ROM_ARM_MAP) $(ROM_ARM_OBJDUMP) \
		$(ROM_THUMB_ELF) $(ROM_THUMB_BIN) $(ROM_THUMB_MAP) $(ROM_THUMB_OBJDUMP) \
		dhrystone.elf dhrystone.gba dhrystone.map dhrystone.objdump $(RUNNER)

distclean: clean
	rm -rf $(DHRY_DIR)

.PHONY: all build run run-arm run-thumb run-both run-jsjit clean distclean
