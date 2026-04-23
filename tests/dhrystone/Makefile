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
ROM_ELF := dhrystone.elf
ROM_BIN := dhrystone.gba
RUNNER := dhrystone_runner

HOST_INCFLAGS := -I$(CORE_DIR) -I$(CORE_DIR)/libretro -I$(CORE_DIR)/libretro/libretro-common/include -I.
HOST_CFLAGS := -O2 -Wall $(HOST_INCFLAGS)

ARM_CFLAGS := -mcpu=arm7tdmi -marm -O2 -ffreestanding -fno-builtin -nostdlib \
	-std=gnu89 -Wall -Wextra -Wno-implicit-int -Wno-return-type \
	-Wno-implicit-function-declaration -Wno-int-conversion \
	-Wno-builtin-declaration-mismatch -Wno-unused-parameter \
	-I. -I$(DHRY_DIR) -DDHRY_ITERS=$(DHRY_ITERS) -DTIME

DHRY_SRCS := $(DHRY_DIR)/dhry.h $(DHRY_DIR)/dhry_1.c $(DHRY_DIR)/dhry_2.c

all: run

$(DHRY_DIR):
	mkdir -p $@

$(DHRY_DIR)/dhry.h: | $(DHRY_DIR)
	$(CURL) -L --fail -o $@ $(DHRY_BASE_URL)/dhry.h

$(DHRY_DIR)/dhry_1.c: | $(DHRY_DIR)
	$(CURL) -L --fail -o $@ $(DHRY_BASE_URL)/dhry_1.c

$(DHRY_DIR)/dhry_2.c: | $(DHRY_DIR)
	$(CURL) -L --fail -o $@ $(DHRY_BASE_URL)/dhry_2.c

$(ROM_ELF): crt0.S gba.ld gba_main.c gba_port.c dhry_result.h $(DHRY_SRCS)
	$(ARM_GCC) $(ARM_CFLAGS) -T gba.ld -Wl,-Map,$(@:.elf=.map) -Wl,--gc-sections \
		-o $@ crt0.S gba_main.c gba_port.c $(DHRY_DIR)/dhry_1.c $(DHRY_DIR)/dhry_2.c -lgcc

$(ROM_BIN): $(ROM_ELF)
	$(ARM_OBJCOPY) -O binary $< $@

$(CORE_SO):
	$(MAKE) -C $(CORE_DIR) HAVE_DYNAREC=0 MMAP_JIT_CACHE=0 platform=unix TARGET=$(CORE_SO)

$(RUNNER): dhrystone_runner.c dhry_result.h $(CORE_SO)
	$(CC) $(HOST_CFLAGS) -o $@ dhrystone_runner.c $(CORE_SO) -Wl,-rpath,$(dir $(CORE_SO))

run: $(RUNNER) $(ROM_BIN)
	./$(RUNNER) --rom $(ROM_BIN) --max-frames $(MAX_FRAMES)

run-jsjit: $(ROM_BIN)
	node ./jsjit_runner.js --rom $(ROM_BIN)

clean:
	rm -f $(CORE_SO) $(ROM_ELF) $(ROM_BIN) $(ROM_ELF:.elf=.map) $(RUNNER)

distclean: clean
	rm -rf $(DHRY_DIR)

.PHONY: all run run-jsjit clean distclean
