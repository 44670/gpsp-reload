ROOT := ../..

CLANG ?= clang
QEMU_RISCV32 ?= qemu-riscv32

HARNESS := rv32im_harness
OUT_DIR := out
SMOKE_PNG := $(OUT_DIR)/rv32im_harness.png

RV32_CFLAGS := \
	--target=riscv32 \
	-march=rv32im \
	-mabi=ilp32 \
	-std=c99 \
	-O2 \
	-g \
	-Wall \
	-Wextra \
	-ffreestanding \
	-fno-builtin \
	-fno-stack-protector \
	-fno-pic \
	-fno-unwind-tables \
	-fno-asynchronous-unwind-tables

RV32_LDFLAGS := \
	-fuse-ld=lld \
	-nostdlib \
	-static \
	-Wl,-e,_start

all: $(HARNESS)

$(HARNESS): rv32im_harness.c
	$(CLANG) $(RV32_CFLAGS) $(RV32_LDFLAGS) -o $@ $<

$(OUT_DIR):
	mkdir -p $@

smoke: $(HARNESS) $(OUT_DIR)
	printf 'help\nbackend rv32im\nreset\nrun 2\ncont 16\nstepi 4\nstepb 3\nregs\nmem 0x02000000 16\nframehash\npng $(SMOKE_PNG)\nquit\n' | $(QEMU_RISCV32) ./$(HARNESS)
	test -s $(SMOKE_PNG)

clean:
	rm -f $(HARNESS)
	rm -rf $(OUT_DIR)

.PHONY: all smoke clean
