ROOT := ../..

CLANG ?= clang
QEMU_RISCV32 ?= qemu-riscv32

HARNESS := rv32im_harness
CODEGEN := rv32im_codegen
ABI_TEST := rv32im_abi_test
EXEC_TEST := rv32im_exec_test
RUNTIME_TEST := rv32im_runtime_test
OUT_DIR := out
SMOKE_PNG := $(OUT_DIR)/rv32im_harness.png
SMOKE_PNG_SIG := $(OUT_DIR)/png_signature.bin
SMOKE_LOAD := $(OUT_DIR)/rv32im_harness_load.bin
SMOKE_LOG := $(OUT_DIR)/rv32im_harness.log
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

all: $(HARNESS) $(CODEGEN) $(ABI_TEST) $(EXEC_TEST) $(RUNTIME_TEST)

$(HARNESS): rv32im_harness.c $(ROOT)/riscv/riscv_runtime.c \
		$(ROOT)/riscv/riscv_emit.h $(ROOT)/riscv/riscv_codegen.h \
		$(ROOT)/riscv/riscv_runtime_test_shim.h
	$(CLANG) $(RV32_CFLAGS) $(RV32_LDFLAGS) \
		-DRISCV_RUNTIME_STANDALONE_TEST -I$(ROOT) -I$(ROOT)/riscv \
		-o $@ rv32im_harness.c $(ROOT)/riscv/riscv_runtime.c

$(CODEGEN): rv32im_codegen.c $(ROOT)/riscv/riscv_codegen.h
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -I$(ROOT)/riscv -o $@ rv32im_codegen.c

$(ABI_TEST): rv32im_abi_test.c rv32im_abi_stub.S
	$(CLANG) $(RV32_CFLAGS) $(RV32_LDFLAGS) -o $@ rv32im_abi_test.c rv32im_abi_stub.S

$(EXEC_TEST): rv32im_exec_test.c $(ROOT)/riscv/riscv_codegen.h
	$(CLANG) $(RV32_CFLAGS) $(RV32_LDFLAGS) -I$(ROOT)/riscv -o $@ rv32im_exec_test.c

$(RUNTIME_TEST): rv32im_runtime_test.c $(ROOT)/riscv/riscv_runtime.c \
		$(ROOT)/riscv/riscv_emit.h $(ROOT)/riscv/riscv_codegen.h \
		$(ROOT)/riscv/riscv_runtime_test_shim.h
	$(CLANG) $(RV32_CFLAGS) $(RV32_LDFLAGS) \
		-DRISCV_RUNTIME_STANDALONE_TEST -I$(ROOT) -I$(ROOT)/riscv \
		-o $@ rv32im_runtime_test.c $(ROOT)/riscv/riscv_runtime.c

$(OUT_DIR):
	mkdir -p $@

$(SMOKE_LOAD): | $(OUT_DIR)
	printf 'rv32im-harness-load-v1\n' > $@

$(SMOKE_PNG_SIG): | $(OUT_DIR)
	printf '\211PNG\r\n\032\n' > $@

$(CODEGEN_REF_ELF): rv32im_codegen.S | $(OUT_DIR)
	$(CLANG) --target=riscv32 -march=rv32im -mabi=ilp32 -fuse-ld=lld -nostdlib -static -Wl,--no-relax -Wl,-e,_start -Wl,-Ttext=0 -o $@ $<

$(CODEGEN_REF_BIN): $(CODEGEN_REF_ELF)
	$(LLVM_OBJCOPY) -j .text -O binary $< $@

codegen: $(CODEGEN) $(CODEGEN_REF_BIN) | $(OUT_DIR)
	./$(CODEGEN) > $(CODEGEN_BIN)
	cmp $(CODEGEN_REF_BIN) $(CODEGEN_BIN)
	@echo "RV32IM codegen test passed"

smoke: $(HARNESS) $(SMOKE_LOAD) $(SMOKE_PNG_SIG) $(OUT_DIR)
	printf 'help\nload $(SMOKE_LOAD)\nbackend rv32im\nreset\nrun 2\ncont 16\nstepi 4\nstepb 3\nregs\nmem 0x02000000 16\ncounters\ntracepc 4\nframehash\ncompare\npng $(SMOKE_PNG)\nquit\n' | $(QEMU_RISCV32) ./$(HARNESS) | tee $(SMOKE_LOG)
	grep -q 'commands=load reset backend run cont stepi stepb regs mem counters tracepc framehash compare png quit' $(SMOKE_LOG)
	grep -q 'ok command=load bytes=23 hash=0x2b3be9fa' $(SMOKE_LOG)
	grep -q 'ok backend=rv32im' $(SMOKE_LOG)
	grep -q 'ok command=reset backend=rv32im loaded_bytes=23 loaded_hash=0x2b3be9fa' $(SMOKE_LOG)
	grep -q 'result=PASS command=run backend=rv32im' $(SMOKE_LOG)
	grep -q 'reason=synthetic_frame_workload' $(SMOKE_LOG)
	grep -q 'result=PASS command=cont backend=rv32im' $(SMOKE_LOG)
	grep -q 'reason=synthetic_scheduler_boundary' $(SMOKE_LOG)
	grep -q 'result=PASS command=stepi backend=rv32im' $(SMOKE_LOG)
	grep -q 'reason=synthetic_instruction_step' $(SMOKE_LOG)
	grep -q 'result=PASS command=stepb backend=rv32im' $(SMOKE_LOG)
	grep -q 'reason=synthetic_block_step' $(SMOKE_LOG)
	grep -q 'regs r0=.*harness_mode=synthetic' $(SMOKE_LOG)
	grep -q 'mem addr=0x02000000 len=16 data=.*harness_mode=synthetic' $(SMOKE_LOG)
	grep -q 'result=PASS command=counters backend=rv32im frames=2 cycles=561824 blocks=260 instructions=2052 loaded_bytes=23 loaded_hash=0x2b3be9fa' $(SMOKE_LOG)
	grep -q 'reason=synthetic_state_counters' $(SMOKE_LOG)
	grep -q 'tracepc backend=rv32im count=4 pcs=.*harness_mode=synthetic' $(SMOKE_LOG)
	grep -q 'reason=synthetic_pc_trace' $(SMOKE_LOG)
	grep -q 'framehash width=240 height=160 bytes=76800 hash=.*harness_mode=synthetic' $(SMOKE_LOG)
	grep -q 'result=PASS command=compare workload=arm_add_multiply_multiplylong_longmulflags_longmulacc_longmulaccflags_carrydata_carryflags_subflags_flags_psr_msr_load_store_regoff_halfreg_blockmem_blockpush_blockpc_blockspsr_hle_pcsrc_writeback_swp_alert_branch_patch_bl_bx_swi_cond_spsr_idle_thumb_fallback' $(SMOKE_LOG)
	grep -q 'interp_scheduler_hash=.*rv32im_scheduler_hash=' $(SMOKE_LOG)
	grep -q 'rv32im_blocks=55 rv32im_fallbacks=2 rv32im_native_data_proc=37 rv32im_native_branch=5 rv32im_native_load=16 rv32im_native_store=7 rv32im_native_psr=4' $(SMOKE_LOG)
	grep -q 'command=compare.*harness_mode=runtime_fixture frame_mode=synthetic mem_mode=runtime_stickybits' $(SMOKE_LOG)
	grep -q 'reason=runtime_state_synthetic_frame_equal' $(SMOKE_LOG)
	grep -q 'result=PASS command=png backend=rv32im width=240 height=160' $(SMOKE_LOG)
	grep -q 'command=png.*harness_mode=synthetic' $(SMOKE_LOG)
	grep -q 'result=PASS command=quit backend=rv32im' $(SMOKE_LOG)
	test -s $(SMOKE_PNG)
	cmp -n 8 $(SMOKE_PNG_SIG) $(SMOKE_PNG)

abi: $(ABI_TEST)
	$(QEMU_RISCV32) ./$(ABI_TEST)

exec: $(EXEC_TEST)
	$(QEMU_RISCV32) ./$(EXEC_TEST)

runtime: $(RUNTIME_TEST)
	$(QEMU_RISCV32) ./$(RUNTIME_TEST)

test: codegen abi exec runtime smoke

clean:
	rm -f $(HARNESS) $(CODEGEN) $(ABI_TEST) $(EXEC_TEST) $(RUNTIME_TEST)
	rm -rf $(OUT_DIR)

.PHONY: all codegen abi exec runtime smoke test clean
