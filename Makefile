CC     ?= gcc
CFLAGS := -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -g -O0 -Iinclude -pthread
BUILD  := build
SRCS   := src/main.c src/gtpu.c src/session.c src/config.c src/stats.c \
          src/log.c src/tun.c
HDRS   := $(wildcard include/*.h)

all: $(BUILD)/gtpu $(BUILD)/tun_probe $(BUILD)/test_gtpu

$(BUILD)/gtpu: $(SRCS) $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

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
