# tolunet — top-level Makefile.
#
# Binding source: TOLUNET-master-prompt.md §3 (TOOLCHAIN). Targets:
#   make all    # build everything currently wired (M0: hello-task)
#   make clean  # remove build artefacts
#
# Toolchain: amiga-gcc (bebbo), now hosted at AmigaPorts/m68k-amigaos-gcc
# (see QUESTIONS.md #9). If the compiler is not found, install it or set
# CROSS_PREFIX to a directory containing the m68k-amigaos-* binaries.
#
# Flags per §3: -O2 -fomit-frame-pointer -m68020 -noixemul. Target OS 3.1+,
# 68020+, no FPU. No libnix/ixemul/stdio in resident code (the hello-task is a
# CLI tool and uses DOS directly; the future .library uses libnix startup only).

CROSS      ?= m68k-amigaos-
CC          = $(CROSS)gcc
STRIP       = $(CROSS)strip

CFLAGS      = -O2 -fomit-frame-pointer -m68020 -noixemul -Wall -Wextra \
              -Iinclude -Isrc -std=c11
LDFLAGS     = -noixemul

BUILD      ?= build
OBJS        = $(BUILD)/main.o $(BUILD)/log.o $(BUILD)/mem.o
HELLO       = $(BUILD)/tolunet-hello

.PHONY: all clean lwip

all: $(HELLO)

# --- M0: hello-task -----------------------------------------------------
$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/task/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/common/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# main.c is the entry; log.c/mem.c are linked in for completeness even though
# the hello-task does not call them yet (M2+ wiring).
$(HELLO): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

# --- lwIP (M2 wiring placeholder) ---------------------------------------
# In M2 the build will compile the lwIP core (vendor/lwip/src/core/*,
# src/netif/ethernet.c) with lwipopts/ on the include path. Not built in M0.
lwip:
	@echo "lwIP core build is wired in M2 (see STATUS.md). Vendored tree is"
	@echo "already present under vendor/lwip/ and is not compiled yet."

clean:
	rm -rf $(BUILD)
