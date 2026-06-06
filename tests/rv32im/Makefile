ROOT := ../..

CLANG ?= clang
QEMU_RISCV32 ?= qemu-riscv32

HARNESS := rv32im_harness
CODEGEN := rv32im_codegen
ARMWRESTLER := rv32im_armwrestler
ARMWRESTLER_INTERP := rv32im_armwrestler_interp
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
MIXED_LOG := $(OUT_DIR)/rv32im_harness_mixed.log
REJECTS_LOG := $(OUT_DIR)/rv32im_harness_rejects.log
ARMWRESTLER_LOG := $(OUT_DIR)/rv32im_armwrestler.log
ARMWRESTLER_INTERP_LOG := $(OUT_DIR)/rv32im_armwrestler_interp.log
RUNTIME_LOG := $(OUT_DIR)/rv32im_runtime.log
ARMWRESTLER_ARM0_CODE_BYTES_MAX ?= 11732
ARMWRESTLER_ARM1_CODE_BYTES_MAX ?= 8684
ARMWRESTLER_ARM2_CODE_BYTES_MAX ?= 16688
ARMWRESTLER_ARM3_CODE_BYTES_MAX ?= 9048
ARMWRESTLER_ARM4_CODE_BYTES_MAX ?= 17960
ARMWRESTLER_THUMB0_CODE_BYTES_MAX ?= 11484
ARMWRESTLER_THUMB1_CODE_BYTES_MAX ?= 5784
ARMWRESTLER_THUMB2_CODE_BYTES_MAX ?= 4444
ARMWRESTLER_ARM_CODE_BYTES_MAX ?= 64112
ARMWRESTLER_THUMB_CODE_BYTES_MAX ?= 21712
ARMWRESTLER_THUMB_HELPERS_MAX ?= 0
CODEGEN_BIN := $(OUT_DIR)/rv32im_codegen.bin
CODEGEN_REF_ELF := $(OUT_DIR)/rv32im_codegen_ref.elf
CODEGEN_REF_BIN := $(OUT_DIR)/rv32im_codegen_ref.bin
FRONTEND_OBJ := $(OUT_DIR)/cpu_threaded_riscv.o
CORE_BUILD_TARGET := gpsp_libretro.so
CORE_BUILD_FLAGS := platform=unix CPU_ARCH=riscv HAVE_DYNAREC=1

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
HOST_CXX ?= c++
LLVM_OBJCOPY ?= llvm-objcopy

all: $(HARNESS) $(CODEGEN) $(ARMWRESTLER) $(ARMWRESTLER_INTERP) $(ABI_TEST) $(EXEC_TEST) $(RUNTIME_TEST)

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

$(ARMWRESTLER): rv32im_armwrestler.c rv32im_frontend_shim.h \
		$(ROOT)/cpu_threaded.c $(ROOT)/riscv/riscv_runtime.c \
		$(ROOT)/riscv/riscv_emit.h $(ROOT)/riscv/riscv_codegen.h \
		$(ROOT)/riscv/riscv_runtime_test_shim.h
	$(CLANG) $(RV32_CFLAGS) $(RV32_LDFLAGS) \
		-DRISCV_RUNTIME_STANDALONE_TEST -DHAVE_DYNAREC -DRISCV_ARCH \
		-DMMAP_JIT_CACHE -I. -I$(ROOT) -I$(ROOT)/riscv \
		-include rv32im_frontend_shim.h \
		-o $@ rv32im_armwrestler.c $(ROOT)/cpu_threaded.c \
		$(ROOT)/riscv/riscv_runtime.c

$(ARMWRESTLER_INTERP): rv32im_armwrestler_interp.cc $(ROOT)/cpu.cc \
		$(ROOT)/cpu.h $(ROOT)/gba_memory.h $(ROOT)/common.h
	$(HOST_CXX) -std=c++11 -O2 -Wall -Wextra \
		-DHAVE_STRINGS_H -DHAVE_STDINT_H -DHAVE_INTTYPES_H \
		-I$(ROOT) -I$(ROOT)/libretro \
		-I$(ROOT)/libretro/libretro-common/include \
		-o $@ rv32im_armwrestler_interp.cc $(ROOT)/cpu.cc

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

core-build:
	$(MAKE) -C $(ROOT) $(CORE_BUILD_FLAGS) $(CORE_BUILD_TARGET)

smoke: $(HARNESS) $(SMOKE_LOAD) $(SMOKE_PNG_SIG) $(OUT_DIR)
	printf 'help\nload $(SMOKE_LOAD)\nbackend rv32im\nrejects runtime\nreset\nrun 2\nrun runtime\ncont 16\ncont runtime\nstepi 4\nstepi runtime 8\nstepb 3\nstepb runtime 5\nregs\nregs runtime\nmem 0x02000000 16\nmem 0x02000000 4096 runtime\nmem 0x02000000 4096 runtime 8\nmem 0x02000120 32 runtime-bytes\nwatchio 0x04000000 64 runtime\ncounters\ncounters runtime\nfallbacks runtime\nfallbacks runtime 8\nfallbacks runtime 16\nfallbacks runtime 40\nfallbacks runtime 48\nsched runtime\nsched runtime 8\nsched runtime 48\nsched runtime 108\nsched runtime 120\ntracepc 4\ntracepc runtime 8\ntracepc runtime 4 32\nbp 0x08000000 runtime\nframehash\ncompare\nmixed\nframehash runtime\npng $(SMOKE_RUNTIME_PNG) runtime\npng $(SMOKE_PNG)\nquit\n' | $(QEMU_RISCV32) ./$(HARNESS) | tee $(SMOKE_LOG)
	grep -q 'result=PASS command=help commands=load reset backend run cont stepi stepb regs mem watchio counters fallbacks sched tracepc bp framehash compare mixed rejects png quit harness_mode=synthetic reason=command_list' $(SMOKE_LOG)
	grep -q 'result=PASS command=load backend=interp bytes=23 hash=0x2b3be9fa harness_mode=synthetic reason=file_loaded' $(SMOKE_LOG)
	grep -q 'result=PASS command=backend backend=rv32im harness_mode=synthetic reason=backend_selected' $(SMOKE_LOG)
	grep -q 'result=PASS command=rejects backend=rv32im total=26 psr_unsupported=2 conditional=13 load_pc=0 pc_base_wb=6 shifted_reg_offset=1 opcodes=mrs_r15_cpsr,msr_cpsr_r15,mul_r15,umull_r15,hle_swi,swp_r15 harness_mode=runtime_fixture reject_mode=emitter_contract reason=runtime_rejections_preserved' $(SMOKE_LOG)
	grep -q 'result=PASS command=reset backend=rv32im loaded_bytes=23 loaded_hash=0x2b3be9fa frame_hash=.* harness_mode=synthetic reason=state_reset' $(SMOKE_LOG)
	grep -q 'result=PASS command=run backend=rv32im' $(SMOKE_LOG)
	grep -q 'reason=synthetic_frame_workload' $(SMOKE_LOG)
	grep -q 'result=PASS command=run backend=rv32im blocks=270 fallbacks=85 initial_lookup_fallbacks=4 relookup_fallbacks=78 unsupported_fallbacks=3 native_data_proc=96 native_branch=7 native_load=49 native_store=33 native_psr=5 .* harness_mode=runtime_fixture run_mode=runtime_snapshot counter_source=runtime_rv32im reason=runtime_snapshot_run' $(SMOKE_LOG)
	grep -q 'result=PASS command=cont backend=rv32im' $(SMOKE_LOG)
	grep -q 'reason=synthetic_scheduler_boundary' $(SMOKE_LOG)
	grep -q 'result=PASS command=cont backend=rv32im total=.* stored=.* offset=0 events=l.* printed=.* hash=.* harness_mode=runtime_fixture cont_mode=runtime_scheduler event_encoding=l@pc:t:u:uc:uf:up:e@pc:ec:f:i:h reason=runtime_scheduler_continue' $(SMOKE_LOG)
	grep -q 'result=PASS command=stepi backend=rv32im' $(SMOKE_LOG)
	grep -q 'reason=synthetic_instruction_step' $(SMOKE_LOG)
	grep -q 'result=PASS command=stepi backend=rv32im count=8 total=.* stored=.* pcs=.* last_pc=.* hash=.* harness_mode=runtime_fixture step_mode=runtime_native_instruction trace_source=native_block_metadata trace_encoding=thumb_lowbit reason=runtime_instruction_steps' $(SMOKE_LOG)
	grep -q 'result=PASS command=stepb backend=rv32im' $(SMOKE_LOG)
	grep -q 'reason=synthetic_block_step' $(SMOKE_LOG)
	grep -q 'result=PASS command=stepb backend=rv32im count=5 total=.* stored=.* pcs=.* last_pc=0x08000004 hash=.* harness_mode=runtime_fixture step_mode=runtime_lookup trace_encoding=thumb_lowbit reason=runtime_block_steps' $(SMOKE_LOG)
	grep -q 'result=PASS command=regs backend=rv32im r0=.*harness_mode=synthetic regs_mode=synthetic reason=synthetic_regs' $(SMOKE_LOG)
	grep -q 'result=PASS command=regs backend=rv32im r0=.* r15=.* cpsr=.* harness_mode=runtime_fixture regs_mode=runtime_snapshot reason=runtime_snapshot_regs' $(SMOKE_LOG)
	grep -q 'result=PASS command=mem backend=rv32im addr=0x02000000 len=16 data=.*harness_mode=synthetic mem_mode=synthetic reason=synthetic_memory' $(SMOKE_LOG)
	grep -q 'result=PASS command=mem backend=rv32im addr=0x02000000 len=4096 total=.* stored=.* offset=0 events=.* matched=.* printed=.* hash=.* harness_mode=runtime_fixture mem_mode=runtime_events event_encoding=kind@addr:pc:value reason=runtime_memory_events' $(SMOKE_LOG)
	grep -q 'result=PASS command=mem backend=rv32im addr=0x02000000 len=4096 total=.* stored=.* offset=8 events=.* matched=.* printed=.* hash=.* harness_mode=runtime_fixture mem_mode=runtime_events event_encoding=kind@addr:pc:value reason=runtime_memory_events' $(SMOKE_LOG)
	grep -q 'offset=8 .*events=.*r8s@0x02000865:0x08001a80:0xfffffff0.*r16s@0x020008a6:0x08001aa0:0xffff8120' $(SMOKE_LOG)
	grep -q 'result=PASS command=mem backend=rv32im addr=0x02000120 len=32 data=.* shadow_base=0x02000000 shadow_size=65536 shadow_write_bytes=.* hash=.* harness_mode=runtime_fixture mem_mode=runtime_shadow reason=runtime_shadow_memory' $(SMOKE_LOG)
	grep -q 'result=PASS command=watchio backend=rv32im addr=0x04000000 len=64 total=.* stored=.* offset=0 events=w32@0x04000028:0x08000304:.* matched=1 printed=1 hash=.* harness_mode=runtime_fixture watch_mode=runtime_events event_encoding=kind@addr:pc:value reason=runtime_io_events' $(SMOKE_LOG)
	grep -q 'result=PASS command=counters backend=rv32im frames=2 cycles=561824 blocks=260 instructions=2052 loaded_bytes=23 loaded_hash=0x2b3be9fa' $(SMOKE_LOG)
	grep -q 'reason=synthetic_state_counters' $(SMOKE_LOG)
	grep -q 'result=PASS command=counters backend=rv32im blocks=270 fallbacks=85 initial_lookup_fallbacks=4 relookup_fallbacks=78 unsupported_fallbacks=3 native_data_proc=96 native_branch=7 native_load=49 native_store=33 native_psr=5' $(SMOKE_LOG)
	grep -q 'counters_mode=runtime_snapshot counter_source=runtime_rv32im reason=runtime_snapshot_counters' $(SMOKE_LOG)
	grep -q 'result=PASS command=fallbacks backend=rv32im total=85 stored=85 offset=0 initial_lookup=4 relookup=78 unsupported=3 events=.* harness_mode=runtime_fixture fallback_mode=runtime_events event_encoding=kind@pc:mode:result:c reason=runtime_fallback_events' $(SMOKE_LOG)
	grep -q 'result=PASS command=fallbacks backend=rv32im total=85 stored=85 offset=8 initial_lookup=4 relookup=78 unsupported=3 events=.* harness_mode=runtime_fixture fallback_mode=runtime_events event_encoding=kind@pc:mode:result:c reason=runtime_fallback_events' $(SMOKE_LOG)
	grep -q 'offset=8 .*events=.*r@0x00000080:a:m:c5,r@0x00000084:a:m:c4' $(SMOKE_LOG)
	grep -q 'offset=8 .*events=.*r@0x00000090:a:m:c5,r@0x00000094:a:m:c4.*r@0x00000090:a:m:c5,r@0x00000094:a:m:c4' $(SMOKE_LOG)
	grep -q 'offset=16 .*events=r@0xfffffff0:a:m:c5,r@0xfffffff4:a:m:c4,r@0xffff8120:a:m:c5,r@0xffff8124:a:m:c4' $(SMOKE_LOG)
	grep -q 'offset=40 .*events=r@0x08001104:a:m:c4,r@0x02009004:a:m:c4,r@0x08000ee4:a:m:c6,r@0x08000f1c:a:m:c5,r@0x08000f84:a:m:c4,r@0x08000fa4:a:m:c3,r@0x08001008:a:m:c3,r@0x08001728:a:m:c3' $(SMOKE_LOG)
	grep -q 'offset=48 .*events=r@0x08001784:a:m:c4,r@0x080017a4:a:m:c4,r@0x080014c4:a:m:c4,r@0x080014e4:a:m:c4,r@0x08001504:a:m:c3,r@0x08001064:a:m:c4,r@0x080016c4:a:m:c8,r@0x080016e4:a:m:c9' $(SMOKE_LOG)
	grep -q 'result=PASS command=sched backend=rv32im total=.* stored=.* offset=0 events=l.* printed=.* hash=.* harness_mode=runtime_fixture sched_mode=runtime_scheduler event_encoding=l@pc:t:u:uc:uf:up:e@pc:ec:f:i:h reason=runtime_scheduler_events' $(SMOKE_LOG)
	grep -q 'result=PASS command=sched backend=rv32im total=.* stored=.* offset=8 events=l.* printed=.* hash=.* harness_mode=runtime_fixture sched_mode=runtime_scheduler event_encoding=l@pc:t:u:uc:uf:up:e@pc:ec:f:i:h reason=runtime_scheduler_events' $(SMOKE_LOG)
	grep -q 'offset=48 events=.*l1@0x08001a60:t0:u1:uc0x00000000:uf0x80000000:up0x00000000:e0@0x00000000:ec0:f0:i0:h0.*l1@0x08001a80:t0:u1:uc0x00000000:uf0x80000000:up0x00000000:e0@0x00000000:ec0:f0:i0:h0.*l2@0xfffffff0:t0:u0:uc0x7fffffff:uf0x00000000:up0x00000000:e1@0xfffffff0:ec5:f0:i0:h0' $(SMOKE_LOG)
	grep -q 'offset=108 events=l1@0x08000ec0:t0:u1:uc0x00000000:uf0x80000000:up0x00000000:e0@0x00000000:ec0:f0:i0:h0,l1@0x08000ee0:t0:u1:uc0x00000000:uf0x80000000:up0x00000000:e0@0x00000000:ec0:f0:i0:h0,l2@0x08000ee4:t0:u0:uc0x7fffffff:uf0x00000000:up0x00000000:e1@0x08000ee4:ec6:f0:i0:h0,l1@0x08000f00:t0:u1:uc0x00000000:uf0x80000000:up0x00000000:e0@0x00000000:ec0:f0:i0:h0,l2@0x08000f1c:t0:u0:uc0x7fffffff:uf0x00000000:up0x00000000:e1@0x08000f1c:ec5:f0:i0:h0,l1@0x08000f80:t0:u1:uc0x00000000:uf0x80000000:up0x00000000:e0@0x00000000:ec0:f0:i0:h0,l2@0x08000f84:t0:u0:uc0x7fffffff:uf0x00000000:up0x00000000:e1@0x08000f84:ec4:f0:i0:h0,l1@0x08000fa0:t0:u1:uc0x00000000:uf0x80000000:up0x00000000:e0@0x00000000:ec0:f0:i0:h0' $(SMOKE_LOG)
	grep -q 'offset=120 events=l2@0x08001728:t0:u0:uc0x7fffffff:uf0x00000000:up0x00000000:e1@0x08001728:ec3:f0:i0:h0,l1@0x08001780:t0:u1:uc0x00000000:uf0x80000000:up0x00000000:e0@0x00000000:ec0:f0:i0:h0,l2@0x08001784:t0:u0:uc0x7fffffff:uf0x00000000:up0x00000000:e1@0x08001784:ec4:f0:i0:h0,l1@0x080017a0:t0:u1:uc0x00000000:uf0x80000000:up0x00000000:e0@0x00000000:ec0:f0:i0:h0,l2@0x080017a4:t0:u0:uc0x7fffffff:uf0x00000000:up0x00000000:e1@0x080017a4:ec4:f0:i0:h0,l1@0x080014c0:t0:u1:uc0x00000000:uf0x80000000:up0x00000000:e0@0x00000000:ec0:f0:i0:h0,l2@0x080014c4:t0:u0:uc0x7fffffff:uf0x00000000:up0x00000000:e1@0x080014c4:ec4:f0:i0:h0,l1@0x080014e0:t0:u1:uc0x00000000:uf0x80000000:up0x00000000:e0@0x00000000:ec0:f0:i0:h0' $(SMOKE_LOG)
	grep -q 'result=PASS command=tracepc backend=rv32im count=4 pcs=.*harness_mode=synthetic trace_mode=synthetic reason=synthetic_pc_trace' $(SMOKE_LOG)
	grep -q 'reason=synthetic_pc_trace' $(SMOKE_LOG)
	grep -q 'result=PASS command=tracepc backend=rv32im count=8 total=.* stored=.* offset=0 pcs=.* harness_mode=runtime_fixture trace_mode=runtime_lookup trace_encoding=thumb_lowbit reason=runtime_lookup_trace' $(SMOKE_LOG)
	grep -q 'result=PASS command=tracepc backend=rv32im count=4 total=.* stored=.* offset=32 pcs=.* hash=.* harness_mode=runtime_fixture trace_mode=runtime_lookup trace_encoding=thumb_lowbit reason=runtime_lookup_trace' $(SMOKE_LOG)
	grep -q 'result=PASS command=bp backend=rv32im pc=0x08000000 hit=1 index=0x00000000 total=.* stored=.* truncated=0 hash=.* harness_mode=runtime_fixture bp_mode=runtime_lookup trace_encoding=thumb_lowbit reason=runtime_lookup_breakpoint_hit' $(SMOKE_LOG)
	grep -q 'result=PASS command=framehash backend=rv32im width=240 height=160 bytes=76800 hash=.*harness_mode=synthetic frame_mode=synthetic reason=synthetic_frame' $(SMOKE_LOG)
	grep -q 'result=PASS command=compare workload=.*loadbytepcnative_loadhalfpc_loadhalfpcremain_loadhalfpcnative_loadhalfregpc_loadhalfregpcremain_loadhalfregpcnative_loadsignedbyteregpc_loadsignedbyteregpcremain_loadsignedbyteregpcnative_loadsignedhalfregpc_loadsignedhalfregpcremain_loadsignedhalfregpcnative_loadsignedbytepc_loadsignedbytepcremain_loadsignedbytepcnative_loadsignedhalfpc_loadsignedhalfpcremain_loadsignedhalfpcnative' $(SMOKE_LOG)
	grep -q 'storealert_storealertchain_storehalt.*halfpcstoreremain_halfpcnegstoreremain' $(SMOKE_LOG)
	grep -q 'shiftpcwlslload_shiftpcwlslstore_shiftpcwlslstoreremain_shiftpcwlsrload_shiftpcwlsrstore_shiftpcwlsrstoreremain_shiftpcwasrload_shiftpcwasrstore_shiftpcwasrstoreremain_shiftpcwrorload_shiftpcwrorstore_shiftpcwrorstoreremain_partialunsupported' $(SMOKE_LOG)
	grep -q 'interp_scheduler_hash=.*rv32im_scheduler_hash=' $(SMOKE_LOG)
	grep -q 'rv32im_blocks=270 rv32im_fallbacks=85 rv32im_initial_lookup_fallbacks=4 rv32im_relookup_fallbacks=78 rv32im_unsupported_fallbacks=3 rv32im_native_data_proc=96 rv32im_native_branch=7 rv32im_native_load=49 rv32im_native_store=33 rv32im_native_psr=5' $(SMOKE_LOG)
	grep -q 'command=compare.*harness_mode=runtime_fixture frame_mode=runtime_snapshot mem_mode=runtime_stickybits' $(SMOKE_LOG)
	grep -q 'reason=runtime_state_snapshot_frame_equal' $(SMOKE_LOG)
	grep -q 'result=PASS command=mixed workload=mixed_dataproc_load_pcload_alert_update_fallback' $(SMOKE_LOG)
	grep -q 'command=mixed .*rv32im_blocks=4 rv32im_fallbacks=1 rv32im_initial_lookup_fallbacks=0 rv32im_relookup_fallbacks=1 rv32im_unsupported_fallbacks=0 rv32im_native_data_proc=96 rv32im_native_branch=7 rv32im_native_load=49 rv32im_native_store=33 rv32im_native_psr=5 code_bytes=14328' $(SMOKE_LOG)
	grep -q 'command=mixed .*interp_trace_count=5 rv32im_trace_count=5 .*interp_stepi_count=5 rv32im_stepi_count=5 .*interp_mem_event_count=3 rv32im_mem_event_count=3 .*interp_sched_event_count=1 rv32im_sched_event_count=1 .*interp_fallback_event_count=1 rv32im_fallback_event_count=1 .*interp_shadow_bytes=4 rv32im_shadow_bytes=4' $(SMOKE_LOG)
	grep -q 'command=mixed .*harness_mode=runtime_fixture frame_mode=runtime_snapshot mem_mode=runtime_events scheduler_mode=runtime_events fallback_mode=runtime_events reason=runtime_mixed_contract_chain_equal' $(SMOKE_LOG)
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
	grep -q 'result=PASS command=compare workload=.*loadbytepcnative_loadhalfpc_loadhalfpcremain_loadhalfpcnative_loadhalfregpc_loadhalfregpcremain_loadhalfregpcnative_loadsignedbyteregpc_loadsignedbyteregpcremain_loadsignedbyteregpcnative_loadsignedhalfregpc_loadsignedhalfregpcremain_loadsignedhalfregpcnative_loadsignedbytepc_loadsignedbytepcremain_loadsignedbytepcnative_loadsignedhalfpc_loadsignedhalfpcremain_loadsignedhalfpcnative' $(COMPARE_LOG)
	grep -q 'storealert_storealertchain_storehalt.*halfpcstoreremain_halfpcnegstoreremain' $(COMPARE_LOG)
	grep -q 'shiftpcwlslload_shiftpcwlslstore_shiftpcwlslstoreremain_shiftpcwlsrload_shiftpcwlsrstore_shiftpcwlsrstoreremain_shiftpcwasrload_shiftpcwasrstore_shiftpcwasrstoreremain_shiftpcwrorload_shiftpcwrorstore_shiftpcwrorstoreremain_partialunsupported' $(COMPARE_LOG)
	grep -q 'rv32im_blocks=270 rv32im_fallbacks=85 rv32im_initial_lookup_fallbacks=4 rv32im_relookup_fallbacks=78 rv32im_unsupported_fallbacks=3 rv32im_native_data_proc=96 rv32im_native_branch=7 rv32im_native_load=49 rv32im_native_store=33 rv32im_native_psr=5' $(COMPARE_LOG)
	grep -q 'reason=runtime_state_snapshot_frame_equal' $(COMPARE_LOG)
	grep -q 'result=PASS command=quit backend=interp' $(COMPARE_LOG)

mixed: $(HARNESS) | $(OUT_DIR)
	printf 'mixed\nquit\n' | $(QEMU_RISCV32) ./$(HARNESS) | tee $(MIXED_LOG)
	grep -q 'result=PASS command=mixed workload=mixed_dataproc_load_pcload_alert_update_fallback' $(MIXED_LOG)
	grep -q 'interp_frame_hash=0xdd46b9ac rv32im_frame_hash=0xdd46b9ac interp_reg_hash=0x0d6b4707 rv32im_reg_hash=0x0d6b4707 interp_mem_hash=0x8a8fc055 rv32im_mem_hash=0x8a8fc055 interp_scheduler_hash=0x2af66ad5 rv32im_scheduler_hash=0x2af66ad5' $(MIXED_LOG)
	grep -q 'interp_trace_count=5 rv32im_trace_count=5 interp_trace_hash=0x5dea3b65 rv32im_trace_hash=0x5dea3b65 interp_stepi_count=5 rv32im_stepi_count=5 interp_stepi_hash=0xc1c4ce49 rv32im_stepi_hash=0xc1c4ce49' $(MIXED_LOG)
	grep -q 'interp_mem_event_count=3 rv32im_mem_event_count=3 interp_mem_event_hash=0x9abf465c rv32im_mem_event_hash=0x9abf465c interp_sched_event_count=1 rv32im_sched_event_count=1 interp_sched_event_hash=0x0792f634 rv32im_sched_event_hash=0x0792f634' $(MIXED_LOG)
	grep -q 'interp_fallback_event_count=1 rv32im_fallback_event_count=1 interp_fallback_event_hash=0xc4528507 rv32im_fallback_event_hash=0xc4528507 interp_shadow_bytes=4 rv32im_shadow_bytes=4 interp_shadow_hash=0x6c30726c rv32im_shadow_hash=0x6c30726c' $(MIXED_LOG)
	grep -q 'rv32im_blocks=4 rv32im_fallbacks=1 rv32im_initial_lookup_fallbacks=0 rv32im_relookup_fallbacks=1 rv32im_unsupported_fallbacks=0 rv32im_native_data_proc=96 rv32im_native_branch=7 rv32im_native_load=49 rv32im_native_store=33 rv32im_native_psr=5 code_bytes=14328' $(MIXED_LOG)
	grep -q 'harness_mode=runtime_fixture frame_mode=runtime_snapshot mem_mode=runtime_events scheduler_mode=runtime_events fallback_mode=runtime_events reason=runtime_mixed_contract_chain_equal' $(MIXED_LOG)
	grep -q 'result=PASS command=quit backend=interp' $(MIXED_LOG)

rejects: $(HARNESS) | $(OUT_DIR)
	printf 'backend rv32im\nrejects runtime\nquit\n' | $(QEMU_RISCV32) ./$(HARNESS) | tee $(REJECTS_LOG)
	grep -q 'result=PASS command=backend backend=rv32im harness_mode=synthetic reason=backend_selected' $(REJECTS_LOG)
	grep -q 'result=PASS command=rejects backend=rv32im total=26 psr_unsupported=2 conditional=13 load_pc=0 pc_base_wb=6 shifted_reg_offset=1 opcodes=mrs_r15_cpsr,msr_cpsr_r15,mul_r15,umull_r15,hle_swi,swp_r15 harness_mode=runtime_fixture reject_mode=emitter_contract reason=runtime_rejections_preserved' $(REJECTS_LOG)
	grep -q 'result=PASS command=quit backend=rv32im' $(REJECTS_LOG)

armwrestler-jit: $(ARMWRESTLER) | $(OUT_DIR)
	$(QEMU_RISCV32) ./$(ARMWRESTLER) | tee $(ARMWRESTLER_LOG)
	grep -q 'result=PASS command=armwrestler test=all' $(ARMWRESTLER_LOG)
	grep -q 'observed_results=79 failure_mask=0x00000000' $(ARMWRESTLER_LOG)
	grep -q 'fallbacks=0 fallback_events=0 execute_arm_calls=0' $(ARMWRESTLER_LOG)
	for test in arm0 arm1 arm2 arm3 arm4 thumb0 thumb1 thumb2; do grep -Eq "command=armwrestler-jit-test test=$$test .* code_bytes=[1-9][0-9]*" $(ARMWRESTLER_LOG); done
	awk -v arm0_max="$(ARMWRESTLER_ARM0_CODE_BYTES_MAX)" \
		-v arm1_max="$(ARMWRESTLER_ARM1_CODE_BYTES_MAX)" \
		-v arm2_max="$(ARMWRESTLER_ARM2_CODE_BYTES_MAX)" \
		-v arm3_max="$(ARMWRESTLER_ARM3_CODE_BYTES_MAX)" \
		-v arm4_max="$(ARMWRESTLER_ARM4_CODE_BYTES_MAX)" \
		-v thumb0_max="$(ARMWRESTLER_THUMB0_CODE_BYTES_MAX)" \
		-v thumb1_max="$(ARMWRESTLER_THUMB1_CODE_BYTES_MAX)" \
		-v thumb2_max="$(ARMWRESTLER_THUMB2_CODE_BYTES_MAX)" ' \
		function field(name, i, kv) { \
			for (i = 1; i <= NF; i++) { \
				split($$i, kv, "="); \
				if (kv[1] == name) \
					return kv[2]; \
			} \
			return ""; \
		} \
		function max_for(test) { \
			if (test == "arm0") return arm0_max; \
			if (test == "arm1") return arm1_max; \
			if (test == "arm2") return arm2_max; \
			if (test == "arm3") return arm3_max; \
			if (test == "arm4") return arm4_max; \
			if (test == "thumb0") return thumb0_max; \
			if (test == "thumb1") return thumb1_max; \
			if (test == "thumb2") return thumb2_max; \
			return 0; \
		} \
		/command=armwrestler-jit-test/ { \
			test = field("test"); \
			code = field("code_bytes") + 0; \
			max = max_for(test) + 0; \
			if (max <= 0 || code > max) { \
				printf("armwrestler per-test code size regression: test=%s code_bytes=%u max=%u\n", test, code, max); \
				exit 1; \
			} \
			seen++; \
		} \
		END { if (seen != 8) exit 1; }' $(ARMWRESTLER_LOG)
	grep -Eq 'command=armwrestler test=all .* arm_code_bytes_total=[1-9][0-9]* thumb_code_bytes_total=[1-9][0-9]*' $(ARMWRESTLER_LOG)
	arm_total=$$(sed -n 's/.* command=armwrestler test=all .* arm_code_bytes_total=\([0-9][0-9]*\) thumb_code_bytes_total=[0-9][0-9]*.*/\1/p' $(ARMWRESTLER_LOG)); \
	thumb_total=$$(sed -n 's/.* command=armwrestler test=all .* thumb_code_bytes_total=\([0-9][0-9]*\).*/\1/p' $(ARMWRESTLER_LOG)); \
	thumb_helpers=$$(sed -n 's/.* command=armwrestler test=all .* thumb_helpers=\([0-9][0-9]*\).*/\1/p' $(ARMWRESTLER_LOG)); \
	test -n "$$arm_total" && \
	test -n "$$thumb_total" && \
	test -n "$$thumb_helpers" && \
	test "$$arm_total" -le "$(ARMWRESTLER_ARM_CODE_BYTES_MAX)" && \
	test "$$thumb_total" -le "$(ARMWRESTLER_THUMB_CODE_BYTES_MAX)" && \
	test "$$thumb_helpers" -le "$(ARMWRESTLER_THUMB_HELPERS_MAX)"
	grep -q 'harness_mode=armwrestler_frontend_jit_only reason=armwrestler_all_jit_only_passed' $(ARMWRESTLER_LOG)

armwrestler-interp: $(ARMWRESTLER_INTERP) | $(OUT_DIR)
	./$(ARMWRESTLER_INTERP) | tee $(ARMWRESTLER_INTERP_LOG)
	grep -q 'result=PASS command=armwrestler-interp test=all' $(ARMWRESTLER_INTERP_LOG)
	grep -q 'observed_results=79 failure_mask=0x00000000' $(ARMWRESTLER_INTERP_LOG)
	grep -q 'harness_mode=armwrestler_interpreter reason=armwrestler_all_interpreter_passed' $(ARMWRESTLER_INTERP_LOG)

armwrestler: armwrestler-interp armwrestler-jit
	grep -q 'observed_results=79 failure_mask=0x00000000' $(ARMWRESTLER_INTERP_LOG)
	grep -q 'observed_results=79 failure_mask=0x00000000' $(ARMWRESTLER_LOG)
	grep -q 'native_blocks=[1-9][0-9]*' $(ARMWRESTLER_LOG)
	grep -q 'code_bytes=[1-9][0-9]*' $(ARMWRESTLER_LOG)
	grep -Eq 'command=armwrestler test=all .* arm_code_bytes_total=[1-9][0-9]* thumb_code_bytes_total=[1-9][0-9]*' $(ARMWRESTLER_LOG)

armwrestler-report: armwrestler
	@awk -v arm_max="$(ARMWRESTLER_ARM_CODE_BYTES_MAX)" \
		-v thumb_max="$(ARMWRESTLER_THUMB_CODE_BYTES_MAX)" \
		-v thumb_helpers_max="$(ARMWRESTLER_THUMB_HELPERS_MAX)" \
		-v arm0_max="$(ARMWRESTLER_ARM0_CODE_BYTES_MAX)" \
		-v arm1_max="$(ARMWRESTLER_ARM1_CODE_BYTES_MAX)" \
		-v arm2_max="$(ARMWRESTLER_ARM2_CODE_BYTES_MAX)" \
		-v arm3_max="$(ARMWRESTLER_ARM3_CODE_BYTES_MAX)" \
		-v arm4_max="$(ARMWRESTLER_ARM4_CODE_BYTES_MAX)" \
		-v thumb0_max="$(ARMWRESTLER_THUMB0_CODE_BYTES_MAX)" \
		-v thumb1_max="$(ARMWRESTLER_THUMB1_CODE_BYTES_MAX)" \
		-v thumb2_max="$(ARMWRESTLER_THUMB2_CODE_BYTES_MAX)" ' \
		function field(name, i, kv) { \
			for (i = 1; i <= NF; i++) { \
				split($$i, kv, "="); \
				if (kv[1] == name) \
					return kv[2]; \
			} \
			return ""; \
		} \
		function max_for(test) { \
			if (test == "arm0") return arm0_max; \
			if (test == "arm1") return arm1_max; \
			if (test == "arm2") return arm2_max; \
			if (test == "arm3") return arm3_max; \
			if (test == "arm4") return arm4_max; \
			if (test == "thumb0") return thumb0_max; \
			if (test == "thumb1") return thumb1_max; \
			if (test == "thumb2") return thumb2_max; \
			return ""; \
		} \
		/command=armwrestler-jit-test/ { \
			test = field("test"); \
			mode = (test ~ /^thumb/) ? "thumb" : "arm"; \
			printf("armwrestler_code_size test=%s mode=%s code_bytes=%s test_max=%s arm_cumulative=%s thumb_cumulative=%s thumb_helpers=%s\n", \
				test, mode, field("code_bytes"), max_for(test), field("arm_code_bytes_total"), \
				field("thumb_code_bytes_total"), field("thumb_helpers")); \
		} \
		/command=armwrestler test=all/ { \
			printf("armwrestler_code_size arm_total=%s thumb_total=%s arm_max=%s thumb_max=%s thumb_helpers=%s thumb_helpers_max=%s\n", \
				field("arm_code_bytes_total"), field("thumb_code_bytes_total"), \
				arm_max, thumb_max, field("thumb_helpers"), thumb_helpers_max); \
		}' $(ARMWRESTLER_LOG)

abi: $(ABI_TEST)
	$(QEMU_RISCV32) ./$(ABI_TEST)

exec: $(EXEC_TEST)
	$(QEMU_RISCV32) ./$(EXEC_TEST)

runtime: $(RUNTIME_TEST) | $(OUT_DIR)
	$(QEMU_RISCV32) ./$(RUNTIME_TEST) | tee $(RUNTIME_LOG)
	awk -f rv32im_runtime_size.awk $(RUNTIME_LOG)

test: codegen abi exec runtime frontend core-build rejects compare mixed smoke

clean:
	rm -f $(HARNESS) $(CODEGEN) $(ARMWRESTLER) $(ARMWRESTLER_INTERP) $(ABI_TEST) $(EXEC_TEST) $(RUNTIME_TEST)
	rm -rf $(OUT_DIR)

.PHONY: all codegen abi exec runtime frontend core-build smoke compare mixed rejects armwrestler-jit armwrestler-interp armwrestler armwrestler-report test clean
