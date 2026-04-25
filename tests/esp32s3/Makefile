ROOT := ../..
CC ?= gcc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -pedantic
XTENSA_OBJDUMP ?= xtensa-esp32s3-elf-objdump
XTENSA_STUB_START ?= 0x100
XTENSA_STUB_STOP ?= 0x118

TOOL := emit_block_stub
NATIVE_TOOL := emit_native_arm_block
TEST := xtensa_codegen_tests
OUT_DIR := out
ARM_BIN := $(OUT_DIR)/arm_stub.bin
THUMB_BIN := $(OUT_DIR)/thumb_stub.bin
NATIVE_ARM_BIN := $(OUT_DIR)/native_arm_block.bin
XTENSA_NATIVE_STOP ?= 0x1b0

.PHONY: all clean disasm-arm disasm-thumb disasm-native-arm test

all: $(TOOL) $(NATIVE_TOOL) $(TEST)

$(TOOL): emit_block_stub.c $(ROOT)/esp32s3/xtensa_codegen.h
	$(CC) $(CFLAGS) -I$(ROOT) $< -o $@

$(NATIVE_TOOL): emit_native_arm_block.c $(ROOT)/esp32s3/xtensa_codegen.h \
	$(ROOT)/esp32s3/xtensa_native_emit.h $(ROOT)/esp32s3/xtensa_state.h
	$(CC) $(CFLAGS) -I$(ROOT) $< -o $@

$(TEST): xtensa_codegen_tests.c $(ROOT)/esp32s3/xtensa_codegen.h \
	$(ROOT)/esp32s3/xtensa_hle.h \
	$(ROOT)/esp32s3/xtensa_native_emit.h $(ROOT)/esp32s3/xtensa_state.h
	$(CC) $(CFLAGS) -I$(ROOT) $< -o $@

$(OUT_DIR):
	mkdir -p $@

$(ARM_BIN): $(TOOL) | $(OUT_DIR)
	./$(TOOL) arm $@

$(THUMB_BIN): $(TOOL) | $(OUT_DIR)
	./$(TOOL) thumb $@

$(NATIVE_ARM_BIN): $(NATIVE_TOOL) | $(OUT_DIR)
	./$(NATIVE_TOOL) $@

disasm-arm: $(ARM_BIN)
	$(XTENSA_OBJDUMP) -D -b binary -m xtensa --start-address=$(XTENSA_STUB_START) --stop-address=$(XTENSA_STUB_STOP) $<

disasm-thumb: $(THUMB_BIN)
	$(XTENSA_OBJDUMP) -D -b binary -m xtensa --start-address=$(XTENSA_STUB_START) --stop-address=$(XTENSA_STUB_STOP) $<

disasm-native-arm: $(NATIVE_ARM_BIN)
	$(XTENSA_OBJDUMP) -D -b binary -m xtensa --start-address=$(XTENSA_STUB_START) --stop-address=$(XTENSA_NATIVE_STOP) $<

test: $(TEST)
	./$(TEST)

clean:
	rm -f $(TOOL) $(NATIVE_TOOL) $(TEST)
	rm -rf $(OUT_DIR)
