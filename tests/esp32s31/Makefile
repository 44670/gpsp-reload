CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined
LDFLAGS += -fsanitize=address,undefined

BUILD_DIR := build
TEST_BIN := $(BUILD_DIR)/board_driver_test

.PHONY: all test clean

all: test

test: $(TEST_BIN)
	$(TEST_BIN)

$(TEST_BIN): board_driver_test.c ../../esp32s31/rgb565_scale3x.c \
             ../../esp32s31/gt1151_protocol.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I../../esp32s31 $^ -o $@ $(LDFLAGS)

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
