CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -pthread \
           -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE

UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(OS),Windows_NT)
  PLATFORM := windows
else ifneq (,$(findstring MINGW,$(UNAME_S)))
  PLATFORM := windows
else ifneq (,$(findstring MSYS,$(UNAME_S)))
  PLATFORM := windows
else
  PLATFORM := unix
endif

ifeq ($(PLATFORM),windows)
  EXE     := .exe
  LDFLAGS ?= -pthread -static -lws2_32
else
  EXE     :=
  LDFLAGS ?= -pthread -ldl
endif

SRC_DIR := src
BUILD   := build
BIN     := $(BUILD)/j-mcp$(EXE)

SRCS := \
  $(SRC_DIR)/main.c \
  $(SRC_DIR)/io.c   \
  $(SRC_DIR)/json.c \
  $(SRC_DIR)/jlib.c \
  $(SRC_DIR)/log.c  \
  $(SRC_DIR)/mcp.c  \
  $(SRC_DIR)/session.c \
  $(SRC_DIR)/tools_j.c

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
