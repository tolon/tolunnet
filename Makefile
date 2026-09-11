# tolunnet — top-level Makefile.
#
# Target OS: AmigaOS 3.0+, universal 68k (68000-68060), no FPU.
# Flags: -O2 -fomit-frame-pointer -m68000 -msoft-float -noixemul (TNET-074).

CROSS      ?= m68k-amigaos-
CC          = $(CROSS)gcc
STRIP       = $(CROSS)strip

# Automatic or overridable NDK include directory (TNET-017)
NDK_INC    ?= $(shell if [ -d "$$(dirname $$(which $(CC) 2>/dev/null))/../m68k-amigaos/ndk-include" ]; then echo "-I$$(dirname $$(which $(CC)))/../m68k-amigaos/ndk-include"; fi)

# TNET-139: -Werror=cast-align keeps the 68000 Address Error class out of
# the tree (a word/long access through a cast from a byte-typed pointer is
# exactly the #80000003 Guru). Vendored code builds with its own flags.
CFLAGS      = -O2 -fomit-frame-pointer -m68000 -msoft-float -noixemul -Wall -Wextra -Wshadow \
              -Wcast-align -Werror=cast-align \
              -Ilwipopts -Iinclude -Iinclude/netinclude -Ivendor/lwip/src/include -Isrc \
              $(NDK_INC) -std=c11 -MMD -MP
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
# inet_parse/config_text/sbtc_dispatch/fdset_util are the pure, host-testable
# units (Round 3 §B.1) shared by daemon, library and host tests.
COMMON_OBJS = $(BUILD)/src/common/log.o $(BUILD)/src/common/mem.o $(BUILD)/src/common/prefs.o \
              $(BUILD)/src/common/inet_parse.o $(BUILD)/src/common/config_text.o \
              $(BUILD)/src/common/sbtc_dispatch.o $(BUILD)/src/common/fdset_util.o \
              $(BUILD)/src/common/ipc_client.o $(BUILD)/src/common/http_url.o \
              $(BUILD)/src/common/errstr.o $(BUILD)/src/common/sockaddr_util.o \
              $(BUILD)/src/common/rawfmt.o \
              $(BUILD)/src/task/timers.o
SANA2_OBJS  = $(BUILD)/src/sana2/sana2_netif.o $(BUILD)/src/sana2/sana2_stubs.o $(BUILD)/src/sana2/buffers.o
LIB_OBJS    = $(BUILD)/src/lib/lib_init.o $(BUILD)/src/lib/lib_vectors.o \
              $(BUILD)/src/lib/lib_table.gen.o $(BUILD)/src/lib/lib_stubs.gen.o \
              $(BUILD)/src/lib/lib_unimpl.o
TASK_OBJS   = $(BUILD)/src/task/daemon_main.o \
              $(BUILD)/src/task/slot_table.o \
              $(BUILD)/src/task/netif_mgr.o \
              $(BUILD)/src/task/ipc_dispatch.o \
              $(BUILD)/src/task/ipc_socket.o \
              $(BUILD)/src/task/ipc_tcp.o \
              $(BUILD)/src/task/ipc_dgram.o \
              $(BUILD)/src/task/ipc_msg.o \
              $(BUILD)/src/task/ipc_select.o \
              $(BUILD)/src/task/ipc_netdb.o \
              $(BUILD)/src/task/ipc_status.o \
              $(BUILD)/src/task/syslog.o

# Targets
TOLUNNET_BIN = $(BUILD)/tolunnet
STATUS_BIN   = $(BUILD)/TolunnetStatus
TEST_BIN     = $(BUILD)/TestSocket
PING_BIN     = $(BUILD)/TolunnetPing
GET_BIN      = $(BUILD)/TolunnetGet
PREFS_BIN    = $(BUILD)/TolunnetPrefs
SETUP_BIN    = $(BUILD)/TolunnetSetup
CONF_BIN     = $(BUILD)/SocketConformance
BSDTEST_BIN  = $(BUILD)/bsdsocktest
TOGGLE_BIN   = $(BUILD)/S2Toggle
INSTALL_BIN  = $(BUILD)/Install_Tolunnet

.PHONY: all clean test-host package
all: $(TOLUNNET_BIN) $(STATUS_BIN) $(TEST_BIN) $(PING_BIN) $(GET_BIN) $(PREFS_BIN) $(SETUP_BIN) $(CONF_BIN) $(TOGGLE_BIN) $(BSDTEST_BIN)

# --- Host unit tests (Round 3 §B.1) -----------------------------------------
# Every tests/host/test_*.c runs under native gcc with sanitizers + Werror;
# exit code is the number of failed tests (TAP output on stdout).
HOSTCC      ?= cc
HOST_CFLAGS  = -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
               -Iinclude/netinclude -Iinclude -Itests/host -Isrc/common -Isrc/task -Isrc
HOST_UNITS   = src/common/inet_parse.c src/common/config_text.c \
               src/common/sbtc_dispatch.c src/common/fdset_util.c \
               src/common/ipc_client.c src/common/http_url.c \
               src/common/errstr.c src/common/sockaddr_util.c \
               src/setup/stack_detect.c src/setup/wifi_mgr.c \
               src/setup/net_test.c src/setup/hw_detect.c \
               tests/host/mock_lwip.c src/task/slot_table.c
HOST_TESTS   = $(wildcard tests/host/test_*.c)
HOST_BINS    = $(patsubst tests/host/%.c,$(BUILD)/host/%,$(HOST_TESTS))

test-host: $(HOST_BINS) python-checks
	@set -e; fails=0; total=0; \
	for t in $(HOST_BINS); do \
	  total=$$((total+1)); \
	  if ./$$t > $$t.tap 2>&1; then :; else fails=$$((fails+1)); fi; \
	  echo "--- $$t"; cat $$t.tap; \
	done; \
	echo "host tests: $$total binaries, $$fails failed (TAP above; TODO rows do not fail)"; \
	test $$fails -eq 0

$(BUILD)/host/%: tests/host/%.c $(HOST_UNITS) tests/host/tn_test.h
	@mkdir -p $(BUILD)/host
	$(HOSTCC) $(HOST_CFLAGS) $< $(HOST_UNITS) -o $@

.PHONY: python-checks
python-checks:
	python3 scripts/gen_lvo_table.py
	python3 scripts/verify_icons.py
	python3 scripts/check_md_links.py
	sh scripts/check-forbid.sh
	@git diff --exit-code -- src/lib/lib_table.gen.c src/lib/lib_stubs.gen.s \
		src/lib/lib_unimpl.c src/lib/lib_compat_table.gen.md README.md \
		|| (echo "FAIL: generated LVO/compat files are stale or hand-edited —"; \
		    echo "       run scripts/gen_lvo_table.py and commit the result (ANX-02)"; \
		    exit 1)

# TNET-139: host-gcc strict cast-alignment gate over src/ (zero-warning).
.PHONY: align-check
align-check:
	@sh scripts/check_cast_align.sh

$(BUILD):
	mkdir -p $(BUILD)

# Generic compilation rules
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Header dependency files (-MMD -MP): every object rebuilds when any header it
# includes changes. This is the structural fix for the TNET-096 / TNET-108
# incidents where task_ctx.h struct changes were linked against stale objects.
DEP_FILES := $(shell find $(BUILD) -name '*.d' 2>/dev/null)
-include $(DEP_FILES)

# Target: Network Task with embedded bsdsocket.library (M2/M3)
$(TOLUNNET_BIN): $(TASK_OBJS) $(LIB_OBJS) $(SANA2_OBJS) $(COMMON_OBJS) $(LWIP_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: TolunnetStatus Diagnostic Tool (M1/M2)
$(STATUS_BIN): $(BUILD)/src/cmds/TolunnetStatus.o $(BUILD)/src/common/prefs.o $(BUILD)/src/common/config_text.o $(BUILD)/src/common/log.o $(BUILD)/src/common/ipc_client.o $(BUILD)/src/common/rawfmt.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: TestSocket Client Binary (M3)
$(TEST_BIN): $(BUILD)/src/cmds/TestSocket.o $(BUILD)/src/common/log.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: TolunnetPing CLI Binary (M4)
$(PING_BIN): $(BUILD)/src/cmds/TolunnetPing.o $(BUILD)/src/common/log.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: TolunnetGet HTTP Client CLI Binary (M5)
$(GET_BIN): $(BUILD)/src/cmds/TolunnetGet.o $(BUILD)/src/common/log.o $(BUILD)/src/common/http_url.o $(BUILD)/src/common/rawfmt.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: TolunnetPrefs Native Workbench GadTools GUI Panel
$(PREFS_BIN): $(BUILD)/src/cmds/TolunnetPrefs.o $(BUILD)/src/common/prefs.o $(BUILD)/src/common/log.o $(BUILD)/src/common/config_text.o $(BUILD)/src/common/inet_parse.o $(BUILD)/src/common/sbtc_dispatch.o $(BUILD)/src/common/fdset_util.o $(BUILD)/src/common/ipc_client.o $(BUILD)/src/setup/stack_detect.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: TolunnetSetup First-Run Network Wizard
SETUP_OBJS = $(BUILD)/src/cmds/TolunnetSetup.o \
             $(BUILD)/src/setup/stack_detect.o \
             $(BUILD)/src/setup/hw_detect.o \
             $(BUILD)/src/setup/wifi_mgr.o \
             $(BUILD)/src/setup/net_test.o \
             $(BUILD)/src/setup/setup_rexx.o \
             $(BUILD)/src/common/inet_parse.o \
             $(BUILD)/src/common/log.o
$(SETUP_BIN): $(SETUP_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: SocketConformance Amiga-side TAP binary (Round 3 §B.2)
$(CONF_BIN): $(BUILD)/tests/amiga/SocketConformance.o $(BUILD)/src/common/log.o $(BUILD)/src/common/ipc_client.o $(BUILD)/src/setup/wifi_mgr.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: S2Toggle SANA-II link-flip bench helper (TNET-109)
$(TOGGLE_BIN): $(BUILD)/tests/amiga/S2Toggle.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: bsdsocktest third-party bsdsocket.library conformance suite
# (ANX-01, vendored from github.com/tbdye/bsdsocktest — GPL-3, see
# THIRD_PARTY_LICENSES.md; built with the project toolchain flags)
BSDTEST_SRCS = \
	vendor/bsdsocktest/src/main.c \
	vendor/bsdsocktest/src/tap.c \
	vendor/bsdsocktest/src/testutil.c \
	vendor/bsdsocktest/src/helper_proto.c \
	vendor/bsdsocktest/src/known_failures.c \
	vendor/bsdsocktest/src/test_socket.c \
	vendor/bsdsocktest/src/test_sendrecv.c \
	vendor/bsdsocktest/src/test_sockopt.c \
	vendor/bsdsocktest/src/test_waitselect.c \
	vendor/bsdsocktest/src/test_signals.c \
	vendor/bsdsocktest/src/test_dns.c \
	vendor/bsdsocktest/src/test_utility.c \
	vendor/bsdsocktest/src/test_transfer.c \
	vendor/bsdsocktest/src/test_errno.c \
	vendor/bsdsocktest/src/test_misc.c \
	vendor/bsdsocktest/src/test_icmp.c \
	vendor/bsdsocktest/src/test_throughput.c
BSDTEST_OBJS = $(BSDTEST_SRCS:.c=.o)
BSDTEST_OBJS := $(addprefix $(BUILD)/,$(BSDTEST_OBJS))
# Vendored suite: cast-align stays a visible warning but is not fatal there
# (upstream code, TNET-139 gate applies to src/ only).
BSDTEST_CFLAGS = $(filter-out -Werror=cast-align,$(CFLAGS))

$(BUILD)/vendor/bsdsocktest/src/%.o: vendor/bsdsocktest/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BSDTEST_CFLAGS) -c $< -o $@

$(BSDTEST_BIN): $(BSDTEST_OBJS)
	$(CC) $(BSDTEST_CFLAGS) $^ -o $@ $(LDFLAGS)

# Release Packaging Target (M7)
VERSION ?= 1.2.0-rc1
PACKAGE_DIR = $(BUILD)/release/tolunnet
LHA_ARCHIVE = $(BUILD)/tolunnet-$(VERSION).lha
ADF_IMAGE = $(BUILD)/tolunnet.adf
XDFTOOL ?= $(shell PATH="$$PATH:$$HOME/.local/bin" which xdftool 2>/dev/null || echo $$HOME/.local/bin/xdftool)

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
	cp $(SETUP_BIN) $(PACKAGE_DIR)/C/
	if [ -f Installer ]; then cp Installer $(PACKAGE_DIR)/C/Installer; fi
	$(STRIP) $(PACKAGE_DIR)/C/* || true
	if [ -f Installer ]; then cp Installer $(PACKAGE_DIR)/Installer; fi
	cp $(PREFS_BIN) $(PACKAGE_DIR)/
	cp $(SETUP_BIN) $(PACKAGE_DIR)/
	$(STRIP) $(PACKAGE_DIR)/TolunnetPrefs || true
	$(STRIP) $(PACKAGE_DIR)/TolunnetSetup || true
	cp TolunnetPrefs.info $(PACKAGE_DIR)/TolunnetPrefs.info
	cp TolunnetSetup.info $(PACKAGE_DIR)/TolunnetSetup.info
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
	@$(MAKE) adf

.PHONY: adf
adf:
	@echo "--- Building ADF Floppy Image ---"
	$(XDFTOOL) -f $(ADF_IMAGE) pack $(PACKAGE_DIR) tolunnet
	@echo "ADF successfully created: $(ADF_IMAGE)"

clean:
	rm -rf $(BUILD)
