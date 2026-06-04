ROOT := ../..

CLANG ?= clang
QEMU_RISCV32 ?= qemu-riscv32

HARNESS := rv32im_harness
CODEGEN := rv32im_codegen
ABI_TEST := rv32im_abi_test
EXEC_TEST := rv32im_exec_test
OUT_DIR := out
SMOKE_PNG := $(OUT_DIR)/rv32im_harness.png
CODEGEN_BIN := $(OUT_DIR)/rv32im_codegen.bin
CODEGEN_REF_ELF := $(OUT_DIR)/rv32im_codegen_ref.elf
CODEGEN_REF_BIN := $(OUT_DIR)/rv32im_codegen_ref.bin

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

HOST_CC ?= cc
LLVM_OBJCOPY ?= llvm-objcopy

all: $(HARNESS) $(CODEGEN) $(ABI_TEST) $(EXEC_TEST)

$(HARNESS): rv32im_harness.c
	$(CLANG) $(RV32_CFLAGS) $(RV32_LDFLAGS) -o $@ $<

$(CODEGEN): rv32im_codegen.c $(ROOT)/riscv/riscv_codegen.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -I$(ROOT)/riscv -o $@ rv32im_codegen.c

$(ABI_TEST): rv32im_abi_test.c rv32im_abi_stub.S
	$(CLANG) $(RV32_CFLAGS) $(RV32_LDFLAGS) -o $@ rv32im_abi_test.c rv32im_abi_stub.S

$(EXEC_TEST): rv32im_exec_test.c $(ROOT)/riscv/riscv_codegen.h
	$(CLANG) $(RV32_CFLAGS) $(RV32_LDFLAGS) -I$(ROOT)/riscv -o $@ rv32im_exec_test.c

$(OUT_DIR):
	mkdir -p $@

$(CODEGEN_REF_ELF): rv32im_codegen.S | $(OUT_DIR)
	$(CLANG) --target=riscv32 -march=rv32im -mabi=ilp32 -fuse-ld=lld -nostdlib -static -Wl,--no-relax -Wl,-e,_start -Wl,-Ttext=0 -o $@ $<

$(CODEGEN_REF_BIN): $(CODEGEN_REF_ELF)
	$(LLVM_OBJCOPY) -j .text -O binary $< $@

codegen: $(CODEGEN) $(CODEGEN_REF_BIN) | $(OUT_DIR)
	./$(CODEGEN) > $(CODEGEN_BIN)
	cmp $(CODEGEN_REF_BIN) $(CODEGEN_BIN)
	@echo "RV32IM codegen test passed"

smoke: $(HARNESS) $(OUT_DIR)
	printf 'help\nbackend rv32im\nreset\nrun 2\ncont 16\nstepi 4\nstepb 3\nregs\nmem 0x02000000 16\nframehash\ncompare\npng $(SMOKE_PNG)\nquit\n' | $(QEMU_RISCV32) ./$(HARNESS)
	test -s $(SMOKE_PNG)

abi: $(ABI_TEST)
	$(QEMU_RISCV32) ./$(ABI_TEST)

exec: $(EXEC_TEST)
	$(QEMU_RISCV32) ./$(EXEC_TEST)

test: codegen abi exec smoke

clean:
	rm -f $(HARNESS) $(CODEGEN) $(ABI_TEST) $(EXEC_TEST)
	rm -rf $(OUT_DIR)

.PHONY: all codegen abi exec smoke test clean
