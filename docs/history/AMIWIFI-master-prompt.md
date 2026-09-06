# AMIWIFI — Master Prompt (WiFi manager, all Amiga wireless drivers)
Standalone AmigaOS 3.x program. GUI front + per-driver adapters. Manages
WiFi credentials for every Amiga wireless SANA-II driver. Not a TCP/IP
stack. Licence GPL-3.0-or-later, Copyright (C) 2026 tolon. Name TBD (§Q1).
Contract: build to this; every driver/OS constant from real headers/driver
files, never memory; a feature is done only when it configured a REAL
driver and the Amiga associated (pasted proof in STATUS.md).

# 1. SCOPE
IN: detect installed wireless drivers; read/write each driver's own config
(SSID/PSK/security); scan where the driver exposes it; saved profiles;
apply/reconnect; status (assoc/signal/IP); write stack DEVICE line;
MUI GUI + CLI. OUT: any TCP/IP stack code (that is tolunnet); RF/WPA
handling (the driver does it); no lwIP, no bsdsocket impl.

# 2. DRIVER LANDSCAPE (verified 2026-08-14 from wifipi.device binary +
#    Emu68 alpha announcement + prism2 docs; remaining VERIFY on bench)

## 2.0 KEY FINDING — the shared format is wpa_supplicant.conf
wifipi AND prism2 both use **wpa_supplicant-style config**: a text file of
`network={ ... }` blocks. This is a de-facto Amiga WiFi standard (ported
wpa_supplicant). So CORE writes ONE format; adapters differ only in FILE
PATH and quirks. Verified block syntax:
```
network={
    ssid="MyNetwork"      # required
    psk="mypassword"      # WPA/WPA2; omit for open
    key_mgmt=WPA-PSK      # or NONE for open
    scan_ssid=1           # only for hidden SSID
    priority=5            # higher = preferred (multi-profile = free)
}
```
Multiple `network={}` blocks = multiple saved profiles natively
(priority= gives auto-select). CORE emits this; adapters place it.

## 2.1 wifipi.device (PiStorm/Emu68) — PRIMARY
- Binary: `$VER: WiFiPi 0.1.0 (11.04.2024)`, 59 KB, in
  Devs/Networks/wifipi.device; needs Devs/Firmware/ blobs (brcm/cyfmac).
- Config file: **`ENVARC:SYS/wireless.prefs`** (wpa_supplicant format,
  §2.0). (Driver also references a `wifipi.txt` scratch name — VERIFY
  which the current build reads; wireless.prefs is the documented one.)
- Security: April-2024 alpha = OPEN only (key_mgmt=NONE). Current binary
  strings show WPA/WPA2 PSK code (wpa_auth, WPA2_AUTH_PSK, CCMP, TKIP)
  AND WPA3_AUTH_SAE_PSK. So WPA2-PSK works on current builds; WPA3-SAE
  code exists (Broadcom chip does it) — treat WPA3 as VERIFY, not "never".
- Scan: binary has `escan` + PacketGetVar/SetVar → scanning is likely
  exposed. VERIFY the mechanism (S2 vendor cmd / control API).
- Chips: brcmfmac43436/43455/43456, cyfmac43430/43455 (Pi3/Zero2/Pi4/CM4).

## 2.2 prism2.device (PCMCIA Prism2, A600/A1200)
- wpa_supplicant-based; config `ENVARC:Sys/wireless.prefs` (same format,
  path VERIFY per version); legacy `SetPrism2Defaults` must be commented
  out when the prefs file is used; no scan tool.

## 2.3 PaulaNET (Pico W, floppy port)
- Creds in Pico flash, written by its AmigaConfig tool. AmiWiFi cannot
  write flash → launch/guide that tool; read link status only.

## 2.4 generic SANA-II wireless — unknown driver → manual SSID + "use your
driver's own tool" fallback.

Adapter model mandatory (different paths/quirks), but §2.0 means the
WRITER is shared — big simplification.

# 3. ARCHITECTURE (fixed)
Layers: CORE (no Intuition; registry + Adapter API + profile store +
capability model) / ADAPTERS (one per driver) / GUI (MUI) / CLI (same CORE).
New driver = new adapter only.

## 3.1 Adapter API (C; every adapter implements)
```
typedef struct { UBYTE can_scan, has_signal, wpa2, wpa3, hidden_ssid,
  multi_profile, needs_reboot; char config_path[128]; } AwCaps;
typedef struct { char ssid[33]; UBYTE security; /*0 open,1 wpa,2 wpa2*/
  char psk[64]; UBYTE hidden; UBYTE ip_mode; /*0 dhcp,1 static*/
  char ip[16],mask[16],gw[16],dns[16]; UBYTE priority; } AwProfile;
typedef struct { char ssid[33]; BYTE signal; UBYTE security, channel; } AwScan;
typedef struct { UBYTE associated; char ssid[33]; BYTE signal;
  char ip[16]; } AwStatus;
typedef struct AwAdapter {
  const char *name;                         /* "wifipi" */
  BOOL  (*detect)(void);                     /* device in DEVS:, version */
  void  (*caps)(AwCaps*);
  LONG  (*scan)(AwScan *out, LONG max);      /* -1 if !can_scan */
  BOOL  (*read_config)(AwProfile*);          /* current from driver file */
  BOOL  (*write_config)(const AwProfile*);   /* preview→backup→write */
  BOOL  (*apply)(void);                      /* assoc/reconnect or reboot */
  BOOL  (*status)(AwStatus*);                /* real, never faked */
} AwAdapter;
```
CORE iterates a static AwAdapter* registry; GUI shows only caps() features.

## 4. CAPABILITY RULES (§89 — no fake controls)
- scan hidden unless caps.can_scan; else manual SSID field only.
- signal meter hidden unless caps.has_signal.
- security choices limited to what the adapter's caps set (open/wpa/wpa2;
  wpa3 only if VERIFY confirms it works on that driver — wifipi has SAE
  code, so do not hard-disable; gate on caps.wpa3). Emit key_mgmt=NONE|
  WPA-PSK|SAE per choice.
- apply: if caps.needs_reboot, show "reboot to apply"; do not claim live.
- psk stored plaintext (no Amiga keystore) — state once in UI, no crypto
  pretence.
- status()/signal read from driver/stack; never invent "connected".

# 5. GUI (MUI; keep CORE MUI-independent)
Widgets:
- Driver cycle: auto-detected adapters; single → auto-select; shows caps.
- Network list (MUI List/Listview): scan results (if can_scan) else saved
  profiles; cols SSID | signal bars (if has_signal) | lock (secured) |
  connected-marker. Doubleclick → connect.
- Profile editor: SSID (string or pick-from-scan), PSK (masked + show
  toggle), security cycle (adapter-limited), hidden checkbox, IP mode
  (DHCP/static → to stack config), priority integer.
- Saved profiles: MUI List, reorder priority, delete, import/export.
- Status strip: associated?, ssid, signal, IP (from stack), link up/down;
  Connect / Disconnect / Reconnect buttons.
- App: iconify/AppIcon, font-sensitive, keyboard nav, locale catalog
  (English builtin + Turkish first). muimaster.library dependency accepted
  (target distros ship it); §Q2.
Reference MUI classes/tags from the MUI 5 SDK headers (in DevPack), cited.

# 6. STACK HANDOFF (100% tolunnet-compatible)
On connect/apply, AmiWiFi writes the stack config (tolunnet §5.2 text
DEVS:tolunnet.config): DEVICE=<driver>, UNIT=<n>, DHCP=YES|NO, and
IP/MASK/GATEWAY/DNS if static. Same KEY=VALUE grammar; unknown keys
preserved. For Roadshow/Miami targets it writes their NetInterfaces
instead (§Q3; default: tolunnet only). AmiWiFi links no stack code.

# 7. THE LAW (anti-fabrication)
1. Driver config path/name/keywords/format ← real driver docs + a real
   config file inspected on the bench (Emu68-WiFi for wifipi; prism2
   manual + real wireless.prefs; PaulaNET AmigaConfig). `/* VERIFY */` +
   QUESTIONS until confirmed. Never invent.
2. MUI/Intuition/GadTools tags, SANA-II stat/vendor cmd codes ← NDK/SDK/
   driver headers, cited. No guessed numbers.
3. caps() honest; no fake scan/signal/wpa3.
4. Done = configured a real driver, Amiga associated, proof pasted.
5. Small commits, every commit builds, STATUS.md truth (proven/built-
   unproven/missing), defects AW-xxx in ISSUES.md, session ritual (read
   STATUS → build green → work current milestone → run → update STATUS →
   commit).

# 8. TOOLCHAIN/BUILD
bebbo amiga-gcc, -O2 -fomit-frame-pointer -m68020 -noixemul. NDK 3.2 +
MUI 5 SDK (DevPack) include paths overridable (no hardcoded personal
paths). CI: build in container every push/branch + host unit tests for
CORE (config parse/format, profile store). Thin binary; must fit a DD
floppy with room (no stack linked).

# 9. MILESTONES (strict; each exit test on a REAL driver)
- W0 Scaffold: repo, GPL, CORE + Adapter API, empty MUI window, CI green.
  Exit: window opens on WB, pasted.
- W1 wifipi adapter: detect + read/write real config (format from bench)
  + apply. Exit: on real PiStorm, set SSID/PSK in a CLI/test call →
  Amiga associates → tolunnet DHCP lease. Logged/photographed.
- W2 GUI core: driver cycle, network/profile list, editor, status,
  masked PSK, capability-driven UI. Exit: full round-trip on wifipi via
  GUI, screenshots.
- W3 prism2 adapter: real wireless.prefs read/write, WPA2 block, disable
  legacy SetPrism2Defaults. Exit: prism2 machine associates (or note if
  no hardware).
- W4 PaulaNET adapter: detect PaulaNET.device; launch AmigaConfig; show
  link status. Exit: status reflects real PaulaNET.
- W5 profiles+polish: multi-profile, priority auto-select, import/export,
  Turkish catalog, AppIcon. Exit: two profiles auto-pick, both connect.
- W6 stack handoff: write DEVS:tolunnet.config on connect; end-to-end
  (pick network → online) with tolunnet. Exit: pasted.
- W7 release: Aminet-shaped lha + guide; README states proven vs untested
  drivers honestly.

# 10. QUESTIONS.md SEEDS
Q1 name. Q2 MUI-only vs GadTools fallback for bare WB3.x. Q3 which stacks
to auto-write besides tolunnet. Q4 remaining VERIFY: current wifipi read
path (wireless.prefs vs wifipi.txt), prism2 exact path per version,
wifipi WPA3-SAE actually working. Q5 wifipi scan API (escan/PacketGetVar
exposed to Amiga side?) — manual entry default until confirmed. Q6 monorepo with
tolunnet vs separate (separate program; shares CORE config contract +
this law). Q7 signal-strength source per driver (SANA-II S2 stats?).
