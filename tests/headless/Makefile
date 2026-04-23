CORE_DIR := ../..

EMSDK ?= $(HOME)/emsdk
EMCC ?= $(EMSDK)/upstream/emscripten/emcc
EMXX ?= $(EMSDK)/upstream/emscripten/em++
EMAR ?= $(EMSDK)/upstream/emscripten/emar
NODE ?= node

CORE_BC := $(abspath $(CORE_DIR)/gpsp_libretro_emscripten.bc)
CORE_A := gpsp_libretro_emscripten.a
RUNNER_O := headless_runner.o
RUNNER_JS := gpsp_headless.js
VERIFY_JS := verify_capture.js
PRE_JS := gpsp_headless_pre.js

ROM ?=
SECONDS ?= 3
SCREENSHOT ?= capture.png
BACKEND ?= interp

CORE_EM_FLAGS := platform=emscripten HAVE_DYNAREC=0 CC=$(EMCC) CXX=$(EMXX) AR=$(EMAR)
JSJIT_EXPORTS := -sEXPORTED_RUNTIME_METHODS='["cwrap"]' \
	-sEXPORTED_FUNCTIONS='["_main","_jsjit_bridge_pointer_size","_jsjit_bridge_reg_ptr","_jsjit_bridge_spsr_ptr","_jsjit_bridge_reg_mode_ptr","_jsjit_bridge_memory_map_read_ptr","_jsjit_bridge_bios_rom_ptr","_jsjit_bridge_open_bios_rom_ptr","_jsjit_bridge_ewram_ptr","_jsjit_bridge_ewram_shadow_ptr","_jsjit_bridge_iwram_ptr","_jsjit_bridge_iwram_data_ptr","_jsjit_bridge_iwram_shadow_ptr","_jsjit_bridge_vram_ptr","_jsjit_bridge_palette_ram_ptr","_jsjit_bridge_oam_ram_ptr","_jsjit_bridge_io_registers_ptr","_jsjit_bridge_execute_cycles_ptr","_jsjit_bridge_cpu_ticks_ptr","_jsjit_bridge_frame_counter_ptr","_jsjit_bridge_ws_cyc_seq_ptr","_jsjit_bridge_ws_cyc_nseq_ptr","_jsjit_bridge_idle_loop_target_pc_ptr","_jsjit_bridge_translation_gate_targets_ptr","_jsjit_bridge_translation_gate_target_pc_ptr","_jsjit_bridge_reg_word_count","_jsjit_bridge_reg_arch_count","_jsjit_bridge_spsr_word_count","_jsjit_bridge_reg_mode_word_count","_jsjit_bridge_memory_map_read_count","_jsjit_bridge_bios_rom_bytes","_jsjit_bridge_open_bios_rom_bytes","_jsjit_bridge_ewram_bytes","_jsjit_bridge_ewram_data_bytes","_jsjit_bridge_ewram_shadow_offset","_jsjit_bridge_iwram_bytes","_jsjit_bridge_iwram_data_bytes","_jsjit_bridge_iwram_data_offset","_jsjit_bridge_vram_bytes","_jsjit_bridge_palette_ram_bytes","_jsjit_bridge_oam_ram_bytes","_jsjit_bridge_io_registers_bytes","_jsjit_bridge_ws_table_bytes","_jsjit_bridge_page_shift","_jsjit_bridge_page_size","_jsjit_bridge_translation_gate_capacity","_jsjit_bridge_read_memory8","_jsjit_bridge_read_memory16","_jsjit_bridge_read_memory32","_jsjit_bridge_write_memory8","_jsjit_bridge_write_memory16","_jsjit_bridge_write_memory32","_jsjit_bridge_update_gba","_jsjit_bridge_check_and_raise_interrupts","_jsjit_bridge_flag_interrupt","_jsjit_bridge_set_cpu_mode","_jsjit_bridge_execute_arm","_jsjit_bridge_load_gamepak_page"]'
RUNNER_CFLAGS := -O3
RUNNER_LDFLAGS := -O3 -sENVIRONMENT=node -sNODERAWFS=1 -sWASM=0 -sINITIAL_MEMORY=134217728 --pre-js $(PRE_JS) $(JSJIT_EXPORTS)
RUNNER_INC := -I$(CORE_DIR)/libretro -I$(CORE_DIR)/libretro/libretro-common/include

all: $(RUNNER_JS)

FORCE:

$(CORE_BC): FORCE
	$(MAKE) -C $(CORE_DIR) clean-objs
	$(MAKE) -C $(CORE_DIR) $(CORE_EM_FLAGS)

$(CORE_A): $(CORE_BC)
	cp $(CORE_BC) $(CORE_A)

$(RUNNER_O): headless_runner.c
	$(EMCC) $(RUNNER_CFLAGS) $(RUNNER_INC) -c -o $@ $<

$(RUNNER_JS): $(RUNNER_O) $(CORE_A)
	$(EMXX) $(RUNNER_LDFLAGS) -o $@ $(RUNNER_O) $(CORE_A)

capture: $(RUNNER_JS)
	@if [ -z "$(ROM)" ]; then echo "ROM=/path/to/game.gba is required"; exit 2; fi
	$(NODE) ./$(RUNNER_JS) --rom "$(ROM)" --seconds $(SECONDS) --screenshot capture.ppm --base-dir . --backend $(BACKEND)

verify: $(RUNNER_JS)
	@if [ -z "$(ROM)" ]; then echo "ROM=/path/to/game.gba is required"; exit 2; fi
	$(NODE) ./$(VERIFY_JS) --runner ./$(RUNNER_JS) --rom "$(ROM)" --seconds $(SECONDS) --screenshot "$(SCREENSHOT)" --backend $(BACKEND)

clean:
	rm -f $(RUNNER_O) $(CORE_A) $(RUNNER_JS) $(RUNNER_JS:.js=.wasm) $(RUNNER_JS:.js=.js.mem) capture.ppm capture.png

.PHONY: FORCE all capture verify clean
