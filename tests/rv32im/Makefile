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
SMOKE_RUNTIME_PNG := $(OUT_DIR)/rv32im_harness_runtime.png
SMOKE_PNG_SIG := $(OUT_DIR)/png_signature.bin
SMOKE_LOAD := $(OUT_DIR)/rv32im_harness_load.bin
SMOKE_LOG := $(OUT_DIR)/rv32im_harness.log
COMPARE_LOG := $(OUT_DIR)/rv32im_harness_compare.log
CODEGEN_BIN := $(OUT_DIR)/rv32im_codegen.bin
CODEGEN_REF_ELF := $(OUT_DIR)/rv32im_codegen_ref.elf
CODEGEN_REF_BIN := $(OUT_DIR)/rv32im_codegen_ref.bin
FRONTEND_OBJ := $(OUT_DIR)/cpu_threaded_riscv.o

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

FRONTEND_WARNFLAGS := \
	-Wno-old-style-declaration \
	-Wno-unused-variable \
	-Wno-unused-label \
	-Wno-unused-but-set-variable \
	-Wno-sign-compare \
	-Wno-implicit-fallthrough

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

$(FRONTEND_OBJ): $(ROOT)/cpu_threaded.c $(ROOT)/riscv/riscv_emit.h \
		$(ROOT)/riscv/riscv_codegen.h | $(OUT_DIR)
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra $(FRONTEND_WARNFLAGS) \
		-DHAVE_DYNAREC -DRISCV_ARCH -D__LIBRETRO__ \
		-DHAVE_STRINGS_H -DHAVE_STDINT_H -DHAVE_INTTYPES_H -DINLINE=inline \
		-I$(ROOT) -I$(ROOT)/libretro \
		-I$(ROOT)/libretro/libretro-common/include \
		-o $@ -c $(ROOT)/cpu_threaded.c

codegen: $(CODEGEN) $(CODEGEN_REF_BIN) | $(OUT_DIR)
	./$(CODEGEN) > $(CODEGEN_BIN)
	cmp $(CODEGEN_REF_BIN) $(CODEGEN_BIN)
	@echo "RV32IM codegen test passed"

frontend: $(FRONTEND_OBJ)

smoke: $(HARNESS) $(SMOKE_LOAD) $(SMOKE_PNG_SIG) $(OUT_DIR)
	printf 'help\nload $(SMOKE_LOAD)\nbackend rv32im\nreset\nrun 2\nrun runtime\ncont 16\nstepi 4\nstepb 3\nstepb runtime 5\nregs\nregs runtime\nmem 0x02000000 16\nmem 0x02000000 4096 runtime\nmem 0x02000000 4096 runtime 8\nmem 0x02000120 32 runtime-bytes\nwatchio 0x04000000 64 runtime\ncounters\ncounters runtime\nfallbacks runtime\nfallbacks runtime 8\nsched runtime\nsched runtime 8\ntracepc 4\ntracepc runtime 8\ntracepc runtime 4 32\nbp 0x08000000 runtime\nframehash\ncompare\nframehash runtime\npng $(SMOKE_RUNTIME_PNG) runtime\npng $(SMOKE_PNG)\nquit\n' | $(QEMU_RISCV32) ./$(HARNESS) | tee $(SMOKE_LOG)
	grep -q 'result=PASS command=help commands=load reset backend run cont stepi stepb regs mem watchio counters fallbacks sched tracepc bp framehash compare png quit harness_mode=synthetic reason=command_list' $(SMOKE_LOG)
	grep -q 'result=PASS command=load backend=interp bytes=23 hash=0x2b3be9fa harness_mode=synthetic reason=file_loaded' $(SMOKE_LOG)
	grep -q 'result=PASS command=backend backend=rv32im harness_mode=synthetic reason=backend_selected' $(SMOKE_LOG)
	grep -q 'result=PASS command=reset backend=rv32im loaded_bytes=23 loaded_hash=0x2b3be9fa frame_hash=.* harness_mode=synthetic reason=state_reset' $(SMOKE_LOG)
	grep -q 'result=PASS command=run backend=rv32im' $(SMOKE_LOG)
	grep -q 'reason=synthetic_frame_workload' $(SMOKE_LOG)
	grep -q 'result=PASS command=run backend=rv32im blocks=193 fallbacks=55 initial_lookup_fallbacks=4 relookup_fallbacks=49 unsupported_fallbacks=2 native_data_proc=86 native_branch=7 native_load=26 native_store=19 native_psr=5 .* harness_mode=runtime_fixture run_mode=runtime_snapshot counter_source=runtime_rv32im reason=runtime_snapshot_run' $(SMOKE_LOG)
	grep -q 'result=PASS command=cont backend=rv32im' $(SMOKE_LOG)
	grep -q 'reason=synthetic_scheduler_boundary' $(SMOKE_LOG)
	grep -q 'result=PASS command=stepi backend=rv32im' $(SMOKE_LOG)
	grep -q 'reason=synthetic_instruction_step' $(SMOKE_LOG)
	grep -q 'result=PASS command=stepb backend=rv32im' $(SMOKE_LOG)
	grep -q 'reason=synthetic_block_step' $(SMOKE_LOG)
	grep -q 'result=PASS command=stepb backend=rv32im count=5 total=.* stored=.* pcs=.* last_pc=0x08000004 hash=.* harness_mode=runtime_fixture step_mode=runtime_lookup trace_encoding=thumb_lowbit reason=runtime_block_steps' $(SMOKE_LOG)
	grep -q 'result=PASS command=regs backend=rv32im r0=.*harness_mode=synthetic regs_mode=synthetic reason=synthetic_regs' $(SMOKE_LOG)
	grep -q 'result=PASS command=regs backend=rv32im r0=.* r15=.* cpsr=.* harness_mode=runtime_fixture regs_mode=runtime_snapshot reason=runtime_snapshot_regs' $(SMOKE_LOG)
	grep -q 'result=PASS command=mem backend=rv32im addr=0x02000000 len=16 data=.*harness_mode=synthetic mem_mode=synthetic reason=synthetic_memory' $(SMOKE_LOG)
	grep -q 'result=PASS command=mem backend=rv32im addr=0x02000000 len=4096 total=.* stored=.* offset=0 events=.* matched=.* printed=.* hash=.* harness_mode=runtime_fixture mem_mode=runtime_events event_encoding=kind@addr:pc:value reason=runtime_memory_events' $(SMOKE_LOG)
	grep -q 'result=PASS command=mem backend=rv32im addr=0x02000000 len=4096 total=.* stored=.* offset=8 events=.* matched=.* printed=.* hash=.* harness_mode=runtime_fixture mem_mode=runtime_events event_encoding=kind@addr:pc:value reason=runtime_memory_events' $(SMOKE_LOG)
	grep -q 'result=PASS command=mem backend=rv32im addr=0x02000120 len=32 data=.* shadow_base=0x02000000 shadow_size=65536 shadow_write_bytes=.* hash=.* harness_mode=runtime_fixture mem_mode=runtime_shadow reason=runtime_shadow_memory' $(SMOKE_LOG)
	grep -q 'result=PASS command=watchio backend=rv32im addr=0x04000000 len=64 total=.* stored=.* offset=0 events=w32@0x04000028:0x08000304:.* matched=1 printed=1 hash=.* harness_mode=runtime_fixture watch_mode=runtime_events event_encoding=kind@addr:pc:value reason=runtime_io_events' $(SMOKE_LOG)
	grep -q 'result=PASS command=counters backend=rv32im frames=2 cycles=561824 blocks=260 instructions=2052 loaded_bytes=23 loaded_hash=0x2b3be9fa' $(SMOKE_LOG)
	grep -q 'reason=synthetic_state_counters' $(SMOKE_LOG)
	grep -q 'result=PASS command=counters backend=rv32im blocks=193 fallbacks=55 initial_lookup_fallbacks=4 relookup_fallbacks=49 unsupported_fallbacks=2 native_data_proc=86 native_branch=7 native_load=26 native_store=19 native_psr=5' $(SMOKE_LOG)
	grep -q 'counters_mode=runtime_snapshot counter_source=runtime_rv32im reason=runtime_snapshot_counters' $(SMOKE_LOG)
	grep -q 'result=PASS command=fallbacks backend=rv32im total=55 stored=55 offset=0 initial_lookup=4 relookup=49 unsupported=2 events=.* harness_mode=runtime_fixture fallback_mode=runtime_events event_encoding=kind@pc:mode:result:c reason=runtime_fallback_events' $(SMOKE_LOG)
	grep -q 'result=PASS command=fallbacks backend=rv32im total=55 stored=55 offset=8 initial_lookup=4 relookup=49 unsupported=2 events=.* harness_mode=runtime_fixture fallback_mode=runtime_events event_encoding=kind@pc:mode:result:c reason=runtime_fallback_events' $(SMOKE_LOG)
	grep -q 'result=PASS command=sched backend=rv32im total=.* stored=.* offset=0 events=l.* printed=.* hash=.* harness_mode=runtime_fixture sched_mode=runtime_scheduler event_encoding=l@pc:t:u:uc:e@pc:ec:f:i reason=runtime_scheduler_events' $(SMOKE_LOG)
	grep -q 'result=PASS command=sched backend=rv32im total=.* stored=.* offset=8 events=l.* printed=.* hash=.* harness_mode=runtime_fixture sched_mode=runtime_scheduler event_encoding=l@pc:t:u:uc:e@pc:ec:f:i reason=runtime_scheduler_events' $(SMOKE_LOG)
	grep -q 'result=PASS command=tracepc backend=rv32im count=4 pcs=.*harness_mode=synthetic trace_mode=synthetic reason=synthetic_pc_trace' $(SMOKE_LOG)
	grep -q 'reason=synthetic_pc_trace' $(SMOKE_LOG)
	grep -q 'result=PASS command=tracepc backend=rv32im count=8 total=.* stored=.* offset=0 pcs=.* harness_mode=runtime_fixture trace_mode=runtime_lookup trace_encoding=thumb_lowbit reason=runtime_lookup_trace' $(SMOKE_LOG)
	grep -q 'result=PASS command=tracepc backend=rv32im count=4 total=.* stored=.* offset=32 pcs=.* hash=.* harness_mode=runtime_fixture trace_mode=runtime_lookup trace_encoding=thumb_lowbit reason=runtime_lookup_trace' $(SMOKE_LOG)
	grep -q 'result=PASS command=bp backend=rv32im pc=0x08000000 hit=1 index=0x00000000 total=.* stored=.* truncated=0 hash=.* harness_mode=runtime_fixture bp_mode=runtime_lookup trace_encoding=thumb_lowbit reason=runtime_lookup_breakpoint_hit' $(SMOKE_LOG)
	grep -q 'result=PASS command=framehash backend=rv32im width=240 height=160 bytes=76800 hash=.*harness_mode=synthetic frame_mode=synthetic reason=synthetic_frame' $(SMOKE_LOG)
	grep -q 'result=PASS command=compare workload=.*shiftstore_shiftlsr_shiftasr_shiftror_shiftstoreremain_shiftlsrstoreremain_shiftasrstoreremain_shiftrorstoreremain' $(SMOKE_LOG)
	grep -q 'interp_scheduler_hash=.*rv32im_scheduler_hash=' $(SMOKE_LOG)
	grep -q 'rv32im_blocks=193 rv32im_fallbacks=55 rv32im_initial_lookup_fallbacks=4 rv32im_relookup_fallbacks=49 rv32im_unsupported_fallbacks=2 rv32im_native_data_proc=86 rv32im_native_branch=7 rv32im_native_load=26 rv32im_native_store=19 rv32im_native_psr=5' $(SMOKE_LOG)
	grep -q 'command=compare.*harness_mode=runtime_fixture frame_mode=runtime_snapshot mem_mode=runtime_stickybits' $(SMOKE_LOG)
	grep -q 'reason=runtime_state_snapshot_frame_equal' $(SMOKE_LOG)
	grep -q 'result=PASS command=framehash backend=rv32im width=240 height=160 bytes=76800 hash=.* snapshot_frame_hash=.* harness_mode=runtime_fixture frame_mode=runtime_snapshot reason=runtime_snapshot_frame' $(SMOKE_LOG)
	grep -q 'result=PASS command=png backend=rv32im width=240 height=160 frame_hash=.* snapshot_frame_hash=.* harness_mode=runtime_fixture frame_mode=runtime_snapshot path=$(SMOKE_RUNTIME_PNG)' $(SMOKE_LOG)
	grep -q 'result=PASS command=png backend=rv32im width=240 height=160' $(SMOKE_LOG)
	grep -q 'command=png.*harness_mode=synthetic' $(SMOKE_LOG)
	grep -q 'result=PASS command=quit backend=rv32im' $(SMOKE_LOG)
	test -s $(SMOKE_PNG)
	test -s $(SMOKE_RUNTIME_PNG)
	cmp -n 8 $(SMOKE_PNG_SIG) $(SMOKE_PNG)
	cmp -n 8 $(SMOKE_PNG_SIG) $(SMOKE_RUNTIME_PNG)

compare: $(HARNESS) | $(OUT_DIR)
	printf 'compare\nquit\n' | $(QEMU_RISCV32) ./$(HARNESS) | tee $(COMPARE_LOG)
	grep -q 'result=PASS command=compare workload=.*shiftstore_shiftlsr_shiftasr_shiftror_shiftstoreremain_shiftlsrstoreremain_shiftasrstoreremain_shiftrorstoreremain' $(COMPARE_LOG)
	grep -q 'rv32im_blocks=193 rv32im_fallbacks=55 rv32im_initial_lookup_fallbacks=4 rv32im_relookup_fallbacks=49 rv32im_unsupported_fallbacks=2 rv32im_native_data_proc=86 rv32im_native_branch=7 rv32im_native_load=26 rv32im_native_store=19 rv32im_native_psr=5' $(COMPARE_LOG)
	grep -q 'reason=runtime_state_snapshot_frame_equal' $(COMPARE_LOG)
	grep -q 'result=PASS command=quit backend=interp' $(COMPARE_LOG)

abi: $(ABI_TEST)
	$(QEMU_RISCV32) ./$(ABI_TEST)

exec: $(EXEC_TEST)
	$(QEMU_RISCV32) ./$(EXEC_TEST)

runtime: $(RUNTIME_TEST)
	$(QEMU_RISCV32) ./$(RUNTIME_TEST)

test: codegen abi exec runtime frontend compare smoke

clean:
	rm -f $(HARNESS) $(CODEGEN) $(ABI_TEST) $(EXEC_TEST) $(RUNTIME_TEST)
	rm -rf $(OUT_DIR)

.PHONY: all codegen abi exec runtime frontend smoke compare test clean
