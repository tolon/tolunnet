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
	vendor/lwip/src/netif/ethernet.c \
	vendor/lwip/src/apps/mdns/mdns.c \
	vendor/lwip/src/apps/mdns/mdns_domain.c \
	vendor/lwip/src/apps/mdns/mdns_out.c

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
              $(BUILD)/src/task/crash_log.o $(BUILD)/src/task/crash_trap.o \
              $(BUILD)/src/task/slot_table.o \
              $(BUILD)/src/task/netif_mgr.o \
              $(BUILD)/src/task/ipc_dispatch.o \
              $(BUILD)/src/task/ipc_socket.o \
              $(BUILD)/src/task/ipc_getsockopt.o \
              $(BUILD)/src/task/ipc_tcp.o \
              $(BUILD)/src/task/ipc_dgram.o \
              $(BUILD)/src/task/ipc_msg.o \
              $(BUILD)/src/task/ipc_select.o \
              $(BUILD)/src/task/ipc_netdb.o \
              $(BUILD)/src/task/ipc_status.o \
              $(BUILD)/src/task/ipc_route.o \
              $(BUILD)/src/task/ipc_ifctl.o \
              $(BUILD)/src/task/route.o \
              $(BUILD)/src/task/route_hook.o \
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
CMDLIB_OBJ   = $(BUILD)/src/cmds/cmdlib.o
HOSTNAME_BIN = $(BUILD)/hostname
NSLOOKUP_BIN = $(BUILD)/nslookup
WHOIS_BIN    = $(BUILD)/whois
TRACEROUTE_BIN = $(BUILD)/traceroute
NC_BIN       = $(BUILD)/nc
ARP_BIN      = $(BUILD)/arp
SHOWNETSTATUS_BIN = $(BUILD)/ShowNetStatus
SNTP_BIN      = $(BUILD)/sntp
TELNET_BIN    = $(BUILD)/telnet
TFTP_BIN      = $(BUILD)/tftp
CONTROL_BIN   = $(BUILD)/TolunnetControl
GETNETSTATUS_BIN = $(BUILD)/GetNetStatus
FTP_BIN       = $(BUILD)/ftp
ROUTE_BIN     = $(BUILD)/route
ADDNETROUTE_BIN = $(BUILD)/AddNetRoute
DELETENETROUTE_BIN = $(BUILD)/DeleteNetRoute
IPERF_BIN     = $(BUILD)/iperf
ADDNETIF_BIN  = $(BUILD)/AddNetInterface
CONFNETIF_BIN = $(BUILD)/ConfigureNetInterface
ONLINE_BIN    = $(BUILD)/Online
OFFLINE_BIN   = $(BUILD)/Offline
CHECKNETCONFIG_BIN = $(BUILD)/CheckNetConfig
NETSHUTDOWN_BIN    = $(BUILD)/NetShutdown
USERGROUP_LIB      = $(BUILD)/usergroup.library

UG_OBJS = $(BUILD)/src/usergroup/ug_init.o \
          $(BUILD)/src/usergroup/ug_db.o \
          $(BUILD)/src/usergroup/ug_context.o \
          $(BUILD)/src/usergroup/ug_crypt.o \
          $(BUILD)/src/usergroup/ug_table.gen.o \
          $(BUILD)/src/usergroup/ug_stubs.gen.o

.PHONY: all clean test-host package
all: $(TOLUNNET_BIN) $(USERGROUP_LIB) $(STATUS_BIN) $(TEST_BIN) $(PING_BIN) $(GET_BIN) $(PREFS_BIN) $(SETUP_BIN) $(CONF_BIN) $(TOGGLE_BIN) $(BSDTEST_BIN) $(FREEZEWATCH_BIN) $(HOSTNAME_BIN) $(NSLOOKUP_BIN) $(WHOIS_BIN) $(TRACEROUTE_BIN) $(NC_BIN) $(ARP_BIN) $(SHOWNETSTATUS_BIN) $(SNTP_BIN) $(TELNET_BIN) $(TFTP_BIN) $(CONTROL_BIN) $(GETNETSTATUS_BIN) $(FTP_BIN) $(ROUTE_BIN) $(ADDNETROUTE_BIN) $(DELETENETROUTE_BIN) $(IPERF_BIN) $(ADDNETIF_BIN) $(CONFNETIF_BIN) $(ONLINE_BIN) $(OFFLINE_BIN) $(CHECKNETCONFIG_BIN) $(NETSHUTDOWN_BIN)

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
               tests/host/mock_lwip.c src/task/slot_table.c \
               src/task/route.c src/common/ifreader.c
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
	python3 scripts/gen_usergroup_table.py
	python3 scripts/gen_pkg_docs.py
	python3 scripts/verify_icons.py
	python3 scripts/check_md_links.py
	sh scripts/check-forbid.sh
	@git --no-pager diff --exit-code -- src/lib/lib_table.gen.c src/lib/lib_stubs.gen.s \
		src/lib/lib_unimpl.c src/lib/lib_compat_table.gen.md \
		src/usergroup/ug_table.gen.c src/usergroup/ug_stubs.gen.s \
		src/usergroup/ug_compat_table.gen.md README.md README.guide tolunnet.readme \
		|| (echo "FAIL: generated LVO/compat/doc files are stale or hand-edited —"; \
		    echo "       run generators and commit the result"; \
		    exit 1)

# TNET-139: host-gcc strict cast-alignment gate over src/ (zero-warning).
.PHONY: align-check
align-check:
	@sh scripts/check_cast_align.sh

.PHONY: screenshots
screenshots:
	python3 scripts/convert_screenshots.py

$(BUILD):
	mkdir -p $(BUILD)

# Generic compilation rules
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/vendor/lwip/src/core/ipv4/autoip.o: CFLAGS += -DLWIP_AUTOIP_INTERNAL=1

# Header dependency files (-MMD -MP): every object rebuilds when any header it
# includes changes. This is the structural fix for the TNET-096 / TNET-108
# incidents where task_ctx.h struct changes were linked against stale objects.
DEP_FILES := $(shell find $(BUILD) -name '*.d' 2>/dev/null)
-include $(DEP_FILES)

# Target: Network Task with embedded bsdsocket.library (M2/M3)
$(TOLUNNET_BIN): $(TASK_OBJS) $(LIB_OBJS) $(SANA2_OBJS) $(COMMON_OBJS) $(LWIP_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: usergroup.library Amiga Shared Library (ANX-10)
$(USERGROUP_LIB): $(UG_OBJS)
	$(CC) $(LDFLAGS) -nostartfiles -o $@ $^

# Target: TolunnetStatus Diagnostic Tool (M1/M2)
$(STATUS_BIN): $(BUILD)/src/cmds/TolunnetStatus.o $(BUILD)/src/common/prefs.o $(BUILD)/src/common/config_text.o $(BUILD)/src/common/log.o $(BUILD)/src/common/ipc_client.o $(BUILD)/src/common/rawfmt.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: TestSocket Client Binary (M3)
$(TEST_BIN): $(BUILD)/src/cmds/TestSocket.o $(BUILD)/src/common/log.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: CLI commands (CMD-0/1/2)
$(HOSTNAME_BIN): $(BUILD)/src/cmds/hostname.o $(CMDLIB_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(NSLOOKUP_BIN): $(BUILD)/src/cmds/nslookup.o $(CMDLIB_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(WHOIS_BIN): $(BUILD)/src/cmds/whois.o $(CMDLIB_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TRACEROUTE_BIN): $(BUILD)/src/cmds/traceroute.o $(CMDLIB_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(NC_BIN): $(BUILD)/src/cmds/nc.o $(CMDLIB_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(ARP_BIN): $(BUILD)/src/cmds/arp.o $(CMDLIB_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(SHOWNETSTATUS_BIN): $(BUILD)/src/cmds/ShowNetStatus.o $(CMDLIB_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(SNTP_BIN): $(BUILD)/src/cmds/sntp.o $(CMDLIB_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TELNET_BIN): $(BUILD)/src/cmds/telnet.o $(CMDLIB_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TFTP_BIN): $(BUILD)/src/cmds/tftp.o $(CMDLIB_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(CONTROL_BIN): $(BUILD)/src/cmds/TolunnetControl.o $(CMDLIB_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(GETNETSTATUS_BIN): $(BUILD)/src/cmds/GetNetStatus.o $(CMDLIB_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(FTP_BIN): $(BUILD)/src/cmds/ftp.o $(CMDLIB_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(ROUTE_BIN): $(BUILD)/src/cmds/route.o $(CMDLIB_OBJ) $(BUILD)/src/common/ipc_client.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(ADDNETROUTE_BIN): $(BUILD)/src/cmds/AddNetRoute.o $(CMDLIB_OBJ) $(BUILD)/src/common/ipc_client.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(DELETENETROUTE_BIN): $(BUILD)/src/cmds/DeleteNetRoute.o $(CMDLIB_OBJ) $(BUILD)/src/common/ipc_client.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(IPERF_BIN): $(BUILD)/src/cmds/iperf.o $(CMDLIB_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(ADDNETIF_BIN): $(BUILD)/src/cmds/AddNetInterface.o $(CMDLIB_OBJ) $(BUILD)/src/common/ipc_client.o $(BUILD)/src/common/ifreader.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(CONFNETIF_BIN): $(BUILD)/src/cmds/ConfigureNetInterface.o $(CMDLIB_OBJ) $(BUILD)/src/common/ipc_client.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(ONLINE_BIN): $(BUILD)/src/cmds/Online.o $(CMDLIB_OBJ) $(BUILD)/src/common/ipc_client.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(OFFLINE_BIN): $(BUILD)/src/cmds/Offline.o $(CMDLIB_OBJ) $(BUILD)/src/common/ipc_client.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(CHECKNETCONFIG_BIN): $(BUILD)/src/cmds/CheckNetConfig.o $(CMDLIB_OBJ) $(BUILD)/src/common/config_text.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(NETSHUTDOWN_BIN): $(BUILD)/src/cmds/NetShutdown.o $(CMDLIB_OBJ) $(BUILD)/src/common/ipc_client.o
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
$(CONF_BIN): $(BUILD)/tests/amiga/SocketConformance.o $(BUILD)/src/common/log.o $(BUILD)/src/common/ipc_client.o $(BUILD)/src/setup/wifi_mgr.o $(BUILD)/src/setup/stack_detect.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Target: FreezeWatch TNET-115 capture helper (bench diagnostic only,
# never shipped in the package; TN-bugtrack-2 item 2)
FREEZEWATCH_BIN = $(BUILD)/FreezeWatch
$(FREEZEWATCH_BIN): $(BUILD)/src/cmds/FreezeWatch.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

bench-tools: $(FREEZEWATCH_BIN)
.PHONY: bench-tools

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
# single source: include/version.h (CLOSE S-D.14)
VERSION := $(shell sed -n 's/.*TOLUNNET_VERSION "\(.*\)".*/\1/p' include/version.h)
PACKAGE_DIR = $(BUILD)/release/tolunnet
LHA_ARCHIVE = $(BUILD)/tolunnet-$(VERSION).lha
ADF_IMAGE = $(BUILD)/tolunnet.adf
XDFTOOL ?= $(shell PATH="$$PATH:$$HOME/.local/bin" which xdftool 2>/dev/null || echo $$HOME/.local/bin/xdftool)

package: all
	@echo "--- Creating Release Directory ---"
	python3 scripts/gen_pkg_docs.py
	rm -rf $(BUILD)/release
	mkdir -p $(PACKAGE_DIR)/C
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
	cp $(HOSTNAME_BIN) $(PACKAGE_DIR)/C/
	cp $(NSLOOKUP_BIN) $(PACKAGE_DIR)/C/
	cp $(WHOIS_BIN) $(PACKAGE_DIR)/C/
	cp $(TRACEROUTE_BIN) $(PACKAGE_DIR)/C/
	cp $(NC_BIN) $(PACKAGE_DIR)/C/
	cp $(ARP_BIN) $(PACKAGE_DIR)/C/
	cp $(SHOWNETSTATUS_BIN) $(PACKAGE_DIR)/C/
	cp $(SNTP_BIN) $(PACKAGE_DIR)/C/
	cp $(TELNET_BIN) $(PACKAGE_DIR)/C/
	cp $(TFTP_BIN) $(PACKAGE_DIR)/C/
	cp $(CONTROL_BIN) $(PACKAGE_DIR)/C/
	cp $(GETNETSTATUS_BIN) $(PACKAGE_DIR)/C/
	cp $(FTP_BIN) $(PACKAGE_DIR)/C/
	cp $(ROUTE_BIN) $(PACKAGE_DIR)/C/
	cp $(ADDNETROUTE_BIN) $(PACKAGE_DIR)/C/
	cp $(DELETENETROUTE_BIN) $(PACKAGE_DIR)/C/
	cp $(IPERF_BIN) $(PACKAGE_DIR)/C/
	cp $(ADDNETIF_BIN) $(PACKAGE_DIR)/C/
	cp $(CONFNETIF_BIN) $(PACKAGE_DIR)/C/
	cp $(ONLINE_BIN) $(PACKAGE_DIR)/C/
	cp $(OFFLINE_BIN) $(PACKAGE_DIR)/C/
	cp $(CHECKNETCONFIG_BIN) $(PACKAGE_DIR)/C/
	cp $(NETSHUTDOWN_BIN) $(PACKAGE_DIR)/C/
	cp $(SETUP_BIN) $(PACKAGE_DIR)/C/
	if [ -f Installer ]; then cp Installer $(PACKAGE_DIR)/C/Installer; fi
	$(STRIP) $(PACKAGE_DIR)/C/* || true
	if [ -f Installer ]; then cp Installer $(PACKAGE_DIR)/Installer; fi
	cp $(PREFS_BIN) $(PACKAGE_DIR)/
	cp $(SETUP_BIN) $(PACKAGE_DIR)/
	mkdir -p $(PACKAGE_DIR)/Libs
	cp $(USERGROUP_LIB) $(PACKAGE_DIR)/Libs/
	$(STRIP) $(PACKAGE_DIR)/Libs/* || true
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
	cp assets/tolunnet_drawer.info $(BUILD)/release/tolunnet.info || true
	@echo "--- Building LhA Archive ---"
	python3 scripts/create_lha.py $(PACKAGE_DIR) $(LHA_ARCHIVE)
	@echo "Package successfully created: $(LHA_ARCHIVE)"
	@$(MAKE) adf

.PHONY: adf
adf:
	@echo "--- Building ADF Floppy Image (stripped binaries) ---"
	@rm -rf $(BUILD)/adf-pkg
	@mkdir -p $(BUILD)/adf-pkg/tolunnet/C
	@for f in $(PACKAGE_DIR)/C/*; do \
		$(STRIP) -o $(BUILD)/adf-pkg/tolunnet/C/$$(basename $$f) $$f 2>/dev/null \
		|| cp $$f $(BUILD)/adf-pkg/tolunnet/C/; \
	done
	@mkdir -p $(BUILD)/adf-pkg/tolunnet/Libs
	@$(STRIP) -o $(BUILD)/adf-pkg/tolunnet/Libs/usergroup.library $(USERGROUP_LIB) 2>/dev/null \
		|| cp $(USERGROUP_LIB) $(BUILD)/adf-pkg/tolunnet/Libs/
	@cp LICENSE $(BUILD)/adf-pkg/tolunnet/
	$(XDFTOOL) -f $(ADF_IMAGE) pack $(BUILD)/adf-pkg/tolunnet tolunnet
	@echo "ADF successfully created: $(ADF_IMAGE)"

clean:
	rm -rf $(BUILD)

distclean: clean
	rm -rf .testboot .zcode _shots
	rm -f .capture* .dbg-* .findsum* .hunt* .postkey* .tap .uae-* .wsl-* .zoom*

