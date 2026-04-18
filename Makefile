CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -pthread \
           -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
LDFLAGS ?= -pthread -ldl

SRC_DIR := src
BUILD   := build
BIN     := $(BUILD)/j-mcp

SRCS := \
  $(SRC_DIR)/main.c \
  $(SRC_DIR)/io.c   \
  $(SRC_DIR)/json.c \
  $(SRC_DIR)/log.c  \
  $(SRC_DIR)/mcp.c

OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/%.o: $(SRC_DIR)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

-include $(DEPS)
