# tolunnet — top-level Makefile.
#
# Target OS: AmigaOS 3.0+, universal 68k (68000-68060), no FPU.
# Flags: -O2 -fomit-frame-pointer -m68000 -msoft-float -noixemul (TNET-074).

CROSS      ?= m68k-amigaos-
CC          = $(CROSS)gcc
STRIP       = $(CROSS)strip

# Automatic or overridable NDK include directory (TNET-017)
NDK_INC    ?= $(shell if [ -d "$$(dirname $$(which $(CC) 2>/dev/null))/../m68k-amigaos/ndk-include" ]; then echo "-I$$(dirname $$(which $(CC)))/../m68k-amigaos/ndk-include"; fi)

CFLAGS      = -O2 -fomit-frame-pointer -m68000 -msoft-float -noixemul -Wall -Wextra \
              -Ilwipopts -Iinclude -Ivendor/lwip/src/include -Isrc \
              $(NDK_INC) -std=c11
LDFLAGS     = -noixemul -msoft-float

DEBUG      ?= 0
ifeq ($(DEBUG),1)
CFLAGS     += -DTOLUNNET_DEBUG -g
endif

BUILD      ?= build

# lwIP Core Sources
LWIP_CORE_SRCS = \
	vendor/lwip/src/core/init.c \
	vendor/lwip/src/core/def.c \
	vendor/lwip/src/core/inet_chksum.c \
	vendor/lwip/src/core/ip.c \
	vendor/lwip/src/core/mem.c \
	vendor/lwip/src/core/memp.c \
	vendor/lwip/src/core/netif.c \
	vendor/lwip/src/core/pbuf.c \
	vendor/lwip/src/core/raw.c \
	vendor/lwip/src/core/stats.c \
	vendor/lwip/src/core/sys.c \
	vendor/lwip/src/core/tcp.c \
	vendor/lwip/src/core/tcp_in.c \
	vendor/lwip/src/core/tcp_out.c \
	vendor/lwip/src/core/timeouts.c \
	vendor/lwip/src/core/udp.c \
	vendor/lwip/src/core/dns.c \
	vendor/lwip/src/core/ipv4/acd.c \
	vendor/lwip/src/core/ipv4/autoip.c \
	vendor/lwip/src/core/ipv4/dhcp.c \
	vendor/lwip/src/core/ipv4/etharp.c \
	vendor/lwip/src/core/ipv4/icmp.c \
	vendor/lwip/src/core/ipv4/igmp.c \
	vendor/lwip/src/core/ipv4/ip4.c \
	vendor/lwip/src/core/ipv4/ip4_addr.c \
	vendor/lwip/src/core/ipv4/ip4_frag.c \
	vendor/lwip/src/netif/ethernet.c

LWIP_OBJS = $(patsubst %.c,$(BUILD)/%.o,$(LWIP_CORE_SRCS))

# Common & SANA-II Objects
COMMON_OBJS = $(BUILD)/src/common/log.o $(BUILD)/src/common/mem.o $(BUILD)/src/common/prefs.o $(BUILD)/src/task/timers.o
SANA2_OBJS  = $(BUILD)/src/sana2/sana2_netif.o $(BUILD)/src/sana2/sana2_stubs.o $(BUILD)/src/sana2/buffers.o
LIB_OBJS    = $(BUILD)/src/lib/lib_init.o $(BUILD)/src/lib/lib_vectors.o $(BUILD)/src/lib/lib_stubs.o
TASK_OBJS   = $(BUILD)/src/task/main.o

# Targets
TOLUNNET_BIN = $(BUILD)/tolunnet
STATUS_BIN   = $(BUILD)/TolunnetStatus
TEST_BIN     = $(BUILD)/TestSocket
PING_BIN     = $(BUILD)/TolunnetPing
GET_BIN      = $(BUILD)/TolunnetGet
PREFS_BIN    = $(BUILD)/TolunnetPrefs
INSTALL_BIN  = $(BUILD)/Install_Tolunnet

.PHONY: all clean test-host package
all: $(TOLUNNET_BIN) $(STATUS_BIN) $(TEST_BIN) $(PING_BIN) $(GET_BIN) $(PREFS_BIN)

test-host:
	python3 scripts/gen_lvo_table.py
	python3 scripts/verify_icons.py

$(BUILD):
	mkdir -p $(BUILD)

# Generic compilation rules
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Target: Network Task with embedded bsdsocket.library (M2/M3)
$(TOLUNNET_BIN): $(TASK_OBJS) $(LIB_OBJS) $(SANA2_OBJS) $(COMMON_OBJS) $(LWIP_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: TolunnetStatus Diagnostic Tool (M1/M2)
$(STATUS_BIN): $(BUILD)/src/cmds/TolunnetStatus.o $(BUILD)/src/common/prefs.o $(BUILD)/src/common/log.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: TestSocket Client Binary (M3)
$(TEST_BIN): $(BUILD)/src/cmds/TestSocket.o $(BUILD)/src/common/log.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: TolunnetPing CLI Binary (M4)
$(PING_BIN): $(BUILD)/src/cmds/TolunnetPing.o $(BUILD)/src/common/log.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: TolunnetGet HTTP Client CLI Binary (M5)
$(GET_BIN): $(BUILD)/src/cmds/TolunnetGet.o $(BUILD)/src/common/log.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: TolunnetPrefs Native Workbench GadTools GUI Panel
$(PREFS_BIN): $(BUILD)/src/cmds/TolunnetPrefs.o $(BUILD)/src/common/prefs.o $(BUILD)/src/common/log.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Release Packaging Target (M7)
PACKAGE_DIR = $(BUILD)/release/tolunnet
LHA_ARCHIVE = $(BUILD)/tolunnet-1.1.0.lha

package: all
	@echo "--- Creating Release Directory ---"
	rm -rf $(BUILD)/release
	mkdir -p $(PACKAGE_DIR)/C $(PACKAGE_DIR)/Docs
	cp $(TOLUNNET_BIN) $(PACKAGE_DIR)/C/
	cp $(STATUS_BIN) $(PACKAGE_DIR)/C/
	cp $(STATUS_BIN) $(PACKAGE_DIR)/C/ifconfig
	cp $(STATUS_BIN) $(PACKAGE_DIR)/C/netstat
	cp $(TEST_BIN) $(PACKAGE_DIR)/C/
	cp $(PING_BIN) $(PACKAGE_DIR)/C/
	cp $(PING_BIN) $(PACKAGE_DIR)/C/ping
	cp $(GET_BIN) $(PACKAGE_DIR)/C/
	cp $(GET_BIN) $(PACKAGE_DIR)/C/wget
	cp $(GET_BIN) $(PACKAGE_DIR)/C/curl
	if [ -f Installer ]; then cp Installer $(PACKAGE_DIR)/C/Installer; fi
	$(STRIP) $(PACKAGE_DIR)/C/* || true
	if [ -f Installer ]; then cp Installer $(PACKAGE_DIR)/Installer; fi
	cp $(PREFS_BIN) $(PACKAGE_DIR)/
	$(STRIP) $(PACKAGE_DIR)/TolunnetPrefs || true
	cp TolunnetPrefs.info $(PACKAGE_DIR)/TolunnetPrefs.info
	cp ci/tolunnet.info $(PACKAGE_DIR)/C/tolunnet.info || true
	cp Install_Tolunnet $(PACKAGE_DIR)/Install_Tolunnet
	cp Install_Tolunnet.info $(PACKAGE_DIR)/Install_Tolunnet.info
	cp README.guide $(PACKAGE_DIR)/
	cp README.guide.info $(PACKAGE_DIR)/
	cp tolunnet.readme $(PACKAGE_DIR)/
	cp LICENSE $(PACKAGE_DIR)/
	cp THIRD_PARTY_LICENSES.md $(PACKAGE_DIR)/
	cp docs/protocol.md $(PACKAGE_DIR)/Docs/
	cp assets/tolunnet_drawer.info $(BUILD)/release/tolunnet.info || true
	@echo "--- Building LhA Archive ---"
	python3 scripts/create_lha.py $(PACKAGE_DIR) $(LHA_ARCHIVE)
	@echo "Package successfully created: $(LHA_ARCHIVE)"

clean:
	rm -rf $(BUILD)/src $(BUILD)/release $(BUILD)/*.o $(BUILD)/*.lha $(BUILD)/tolunnet $(BUILD)/Tolunnet* $(BUILD)/TestSocket
