ROOT := ../..
CC ?= gcc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -pedantic
XTENSA_OBJDUMP ?= xtensa-esp32s3-elf-objdump
XTENSA_STUB_START ?= 8
XTENSA_STUB_STOP ?= 0x1c

TOOL := emit_block_stub
OUT_DIR := out
ARM_BIN := $(OUT_DIR)/arm_stub.bin
THUMB_BIN := $(OUT_DIR)/thumb_stub.bin

.PHONY: all clean disasm-arm disasm-thumb

all: $(TOOL)

$(TOOL): emit_block_stub.c $(ROOT)/esp32s3/xtensa_codegen.h
	$(CC) $(CFLAGS) -I$(ROOT) $< -o $@

$(OUT_DIR):
	mkdir -p $@

$(ARM_BIN): $(TOOL) | $(OUT_DIR)
	./$(TOOL) arm $@

$(THUMB_BIN): $(TOOL) | $(OUT_DIR)
	./$(TOOL) thumb $@

disasm-arm: $(ARM_BIN)
	$(XTENSA_OBJDUMP) -D -b binary -m xtensa --start-address=$(XTENSA_STUB_START) --stop-address=$(XTENSA_STUB_STOP) $<

disasm-thumb: $(THUMB_BIN)
	$(XTENSA_OBJDUMP) -D -b binary -m xtensa --start-address=$(XTENSA_STUB_START) --stop-address=$(XTENSA_STUB_STOP) $<

clean:
	rm -f $(TOOL)
	rm -rf $(OUT_DIR)
