CC     ?= gcc
CFLAGS := -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -g -O0 -Iinclude -pthread
BUILD  := build
SRCS   := src/main.c src/gtpu.c src/session.c src/config.c src/stats.c \
          src/log.c src/tun.c
HDRS   := $(wildcard include/*.h)

all: $(BUILD)/gtpu $(BUILD)/tun_probe $(BUILD)/test_gtpu

$(BUILD)/gtpu: $(SRCS) $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

# AddressSanitizer + UBSan: memory errors, leaks, undefined behaviour
$(BUILD)/gtpu_asan: $(SRCS) $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -o $@ $(SRCS)

# ThreadSanitizer: data races (cannot be combined with ASan)
$(BUILD)/gtpu_tsan: $(SRCS) $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -fsanitize=thread -o $@ $(SRCS)

$(BUILD)/tun_probe: tests/tun_probe.c src/tun.c include/tun.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/tun_probe.c src/tun.c

$(BUILD)/test_gtpu: tests/test_gtpu.c src/gtpu.c include/gtpu.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_gtpu.c src/gtpu.c

test: $(BUILD)/test_gtpu
	./$(BUILD)/test_gtpu

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean test