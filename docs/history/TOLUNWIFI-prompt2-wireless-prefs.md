# TOLUNWIFI — Prompt 2 (ADDENDUM): verified wireless.prefs + SANA-II wireless API
Companion to TOLUNWIFI master prompt. Do NOT restart; fold this into the
wifipi + prism2 adapters. Every value below was EXTRACTED FROM REAL BENCH
FILES (AmiKit `WiFi_WPA_for_AmiKit_PiStorm.lzx`, `zenPrismWifi_v0.4` GPL
source, live `wifipi.device`/`WirelessManager` binaries) on 2026-08-14, not
from memory. These CLOSE the master-prompt VERIFY items §2.1/§2.2/§Q4/§Q5.
This supersedes any earlier guess (`wireless.prefs` lowercase-only,
`wifipi.txt`, "WPA2-CCMP only", "no scan API"). Where this doc and the
master prompt disagree, THIS DOC WINS.

────────────────────────────────────────────────────────────────────────
# A. THE CREDENTIAL FILE — verified (fixes §2.1, §2.2, §Q4)

## A.1 Path (BOTH wifipi and prism2 use the SAME file)
- Persistent: `ENVARC:Sys/Wireless.prefs`
- Live copy:  `ENV:Sys/Wireless.prefs`
- WRITE BOTH, every time. ENVARC survives reboot; ENV is what a
  currently-running WirelessManager reads. AmiKit + zenPrismWifi both
  write the pair. (Amiga paths are case-insensitive: `Sys`=`sys`.)
- The `wifipi.txt` string seen in the driver binary is NOT the config
  path — ignore it. The real path is above.

## A.2 Format = wpa_supplicant `network={ }` blocks (verified)
WirelessManager is a wpa_supplicant port — `$VER: WirelessManager 1.3
(28.8.2013)`, SAME binary drives wifipi AND prism2. It parses standard
wpa_supplicant.conf keywords: ssid, psk, key_mgmt, proto, pairwise,
group, scan_ssid, priority, bssid, ap_scan. Whitespace-insensitive
(4-space OR tab indent both accepted).

## A.3 REAL shipped templates (AmiKit, byte-exact)
`ENVARC:Sys/Wireless.wifipi` (WPA/WPA2 skeleton, 27 bytes):
```
network={
    scan_ssid=1
}
```
`ENVARC:Sys/Wireless.nopass` (open skeleton, 45 bytes):
```
network={
    key_mgmt=NONE
    scan_ssid=1
}
```
These ship WITHOUT ssid/psk; the tool fills them. `scan_ssid=1` is always
present (lets it find hidden APs; harmless for visible ones).

## A.4 REAL filled blocks tolunwifi must emit (verified against AmiKit
##     NetworkWizard InsertText logic + zenPrismWifi writer)

WPA/WPA2, plaintext passphrase (primary, human-editable):
```
network={
    ssid="MyNetwork"
    psk="mypassword"
    scan_ssid=1
}
```
Field ORDER = ssid, psk, scan_ssid (NetworkWizard inserts ssid after the
`network={` line, then psk after the ssid line). NO explicit `key_mgmt`
when a psk is present — wpa_supplicant defaults to WPA-PSK. psk value: 8–63
ASCII chars in double quotes.

Open network (no password):
```
network={
    ssid="MyNetwork"
    key_mgmt=NONE
    scan_ssid=1
}
```

WPA/WPA2, HASHED psk (privacy option; hides passphrase on disk — this is
what zenPrismWifi and AmiKit's `wpa_passphrase` path write):
```
network={
    ssid="MyNetwork"
    psk=3543848bc38c4b0f... (64 lowercase hex = 32-byte PMK, NO quotes)
}
```
Quoted psk = passphrase (runtime-hashed). Unquoted 64-hex psk = precomputed
PMK. BOTH valid; NEVER quote the hex form, NEVER leave the passphrase form
unquoted. Multi-network file = multiple `network={}` blocks concatenated
(each separated by a blank line); `priority=N` (higher wins) for auto-pick.

## A.5 PMK generation (only if you offer the hashed form)
PMK = PBKDF2-HMAC-SHA1(passphrase, ssid, 4096 iters, dkLen=32) → 64 hex.
This is exactly `wpa_passphrase <ssid> <pass>`. zenPrismWifi ships a
self-contained GPL C implementation (`src/psk.c`, SHA1+HMAC+PBKDF2, no
deps) — REUSE IT, do not hand-roll crypto. If unsure, ship plaintext
(A.4 primary) only; it is fully valid and the AmiKit default.

────────────────────────────────────────────────────────────────────────
# B. APPLY / ASSOCIATE — verified boot flow (fixes §Q4 "apply mechanism")

## B.1 The 3-stage truth: associate is SEPARATE from IP
1. WirelessManager reads Wireless.prefs → associates + does the WPA
   4-way handshake with the AP (this is the driver's/associator's job).
2. Wait until associated.
3. THEN the TCP/IP stack adds the SANA-II interface and does DHCP.
The stack (tolunnet/Roadshow/Miami) NEVER touches SSID/psk. tolunwifi's
"Apply" = write the file + (re)run WirelessManager. IP came for free once
the driver is associated.

## B.2 wifipi — exact command line (from AmiKit `S/network.on`)
```
IF EXISTS ENV:Sys/Wireless.prefs
   IF EXISTS DEVS:Networks/wifipi.device
      Run C:WirelessManager DEVICE="wifipi.device" UNIT=0 CONFIG="ENVARC:Sys/Wireless.prefs"
   ENDIF
ENDIF
```
Then wait + hand to stack (AmiKit uses):
```
C:WaitUntilConnected DEVICE="DEVS:Networks/wifipi.device" UNIT=0 DELAY=100
IF NOT WARN
   C:AddNetInterface DEVS:NetInterfaces/WiFiPi
ENDIF
```
For tolunnet the last line is tolunnet's own interface bring-up (SANA-II
S2_ONLINE on wifipi.device); the associate half is IDENTICAL.

## B.3 prism2 — command line (from AmiKit `S/network.on`)
```
IF EXISTS DEVS:Networks/prism2.device
   C:WirelessManager prism2.device
   wait 5
ENDIF
```
Same binary, positional device arg, reads ENVARC:Sys/Wireless.prefs by
default (no CONFIG= given). zenPrismWifi is the GPL prism2 manager.

## B.4 Re-associate on a running system (from NetworkWizard)
To apply a changed profile without reboot: find + kill the old associator,
relaunch. AmiKit does exactly:
```
Set WirelessManagerPID `Status COM=C:WirelessManager`
IF VAL $WirelessManagerPID GT 0
   Break $WirelessManagerPID
ENDIF
```
then re-Run WirelessManager (B.2/B.3). If that path is fragile, tolunwifi
may honestly show "reboot to apply" instead — but the Break+relaunch is the
documented live path.

## B.5 Boot integration files (verified real content, for reference)
- `S:network.on` gates everything on `IF EXISTS ENV:Sys/Wireless.prefs`.
- `S:Startup-Network` then `Run C:AddNetInterface DEVS:NetInterfaces/WiFiPi`.
- If tolunwifi installs a boot hook, MATCH these gates/paths so it
  coexists with an existing AmiKit setup instead of fighting it.

────────────────────────────────────────────────────────────────────────
# C. THE ROADSHOW INTERFACE FILE — verified (wifipi)
`DEVS:NetInterfaces/WiFiPi` (real AmiKit file, key lines):
```
device=wifipi.device
#unit=0
#address=192.168.2.55
#netmask=255.255.255.0
configure=dhcp
#configure=auto
#configure=fastauto
iprequests=128
writerequests=128
requiresinitdelay=no
#copymode=fast
```
Notes: `configure=dhcp` default; static = uncomment address+netmask (then
comment configure=dhcp); `configure=fastauto` is the WIRELESS zeroconf
variant (use over `auto` on WiFi); `requiresinitdelay=no`. This is Roadshow
NetInterfaces grammar. tolunwifi writes this ONLY for a Roadshow/wifipi
target; for tolunnet it writes tolunnet's own DEVS:tolunnet.config
(master §6). DNS/routes live separately in `DEVS:Internet/name_resolution`
(`nameserver <ip>` / `prefer dynamic`) and `DEVS:Internet/routes`
(`default <gw>`), both DHCP-supplied by default — only touch on static.

────────────────────────────────────────────────────────────────────────
# D. SANA-II WIRELESS API — verified command codes (fixes §Q5 scan +
#    §4 has_signal). Source: `devices/sana2wireless.h` shipped in
#    zenPrismWifi GPL src. THESE ARE REAL — never invent wireless cmd codes.

## D.1 Command numbers (io_Command)
```
S2_GETSIGNALQUALITY  0xC010   /* -> struct Sana2SignalQuality */
S2_GETNETWORKS       0xC011   /* scan: list nearby APs        */
S2_SETOPTIONS        0xC012
S2_SETKEY            0xC013
S2_GETNETWORKINFO    0xC014
S2_READMGMT          0xC015
S2_WRITEMGMT         0xC016
S2_GETRADIOBANDS     0xC017
```
## D.2 Info tags (TAG_USER-based; used as TagItem query lists)
```
S2INFO_SSID (TAG_USER+0), S2INFO_BSSID (+1), S2INFO_AuthTypes (+2),
S2INFO_AssocID (+3), S2INFO_Encryption (+4), S2INFO_PortType (+5),
S2INFO_BeaconInterval (+6), S2INFO_Channel (+7), S2INFO_Signal (+8),
S2INFO_Noise (+9), S2INFO_Capabilities (+10), S2INFO_InfoElements (+11),
S2INFO_WPAInfo (+12), S2INFO_Band (+13), S2INFO_DefaultKeyNo (+14)
```
Encryption: S2ENC_NONE 0, S2ENC_WEP 1, S2ENC_TKIP 2, S2ENC_CCMP 3.
Band: S2BAND_A 0, _B 1, _G 2, _N 3. Port: S2PORT_MANAGED 7, S2PORT_ADHOC 8.
```
struct Sana2SignalQuality { LONG SignalLevel; LONG NoiseLevel; }; /* dBm */
```

## D.3 REAL scan call pattern (from zenPrismWifi wifi.c — copy this shape)
```c
static const struct TagItem apParams[] = {   /* which fields to return */
    {S2INFO_BSSID,0},{S2INFO_Channel,0},{S2INFO_BeaconInterval,0},
    {S2INFO_Capabilities,0},{S2INFO_Signal,0},{S2INFO_Noise,0},{TAG_END,0}
};
req->ios2_Req.io_Command = S2_GETNETWORKS;
req->ios2_StatData   = (APTR)apParams;      /* IN: query tag list      */
req->ios2_Data       = memPool;             /* OUT: driver fills here   */
req->ios2_DataLength  = 8192;               /* buffer size in          */
req->ios2_WireError   = 0;
DoIO(req);
/* OUT: ios2_DataLength = network count N;
   ios2_StatData -> struct TagItem *tagLists[N]  (one tag list per AP) */
for (i=0;i<N;i++){
    char *p   = (char*)GetTagData(S2INFO_BSSID,0,tagLists[i]);/*6B mac+ssid*/
    char *ssid= &p[8];                       /* SSID starts at byte 8   */
    short ch  = GetTagData(S2INFO_Channel,0,tagLists[i]);
    int  lvl  = GetTagData(S2INFO_Signal,0,tagLists[i]);
    int  nz   = GetTagData(S2INFO_Noise,0,tagLists[i]);
    unsigned short cap = GetTagData(S2INFO_Capabilities,0,tagLists[i]);
}
```
Ref: wiki.amigaos.net/wiki/SANA-II_Revision_7#S2_GETNETWORKS.
Adapter caps: set `can_scan=1` + `has_signal=1` for a driver ONLY after a
real S2_GETNETWORKS returns without S2ERR — otherwise fall back to manual
SSID entry (master §4). wifipi: probe S2_GETNETWORKS at detect; if the
alpha build rejects it, degrade to manual, do NOT fake a scan list.

## D.4 SANA-II bring-up sequence (from zenPrismWifi — the correct order)
S2_DEVICEQUERY → S2_GETSTATIONADDRESS → S2_CONFIGINTERFACE (set src addr)
→ S2_ONLINE. On errors read `ios2_WireError` (S2WERR_*) for the message.
NOTE the reference sets `ios2_BufferManagement` to a **static** TagItem
array that outlives the request (persists for device lifetime) — do the
SAME; a stack-local buffer-management array is the tolunnet TNET-023 bug.
Keep this associate/status code OUT of any TCP/IP path (tolunwifi only).

────────────────────────────────────────────────────────────────────────
# E. PRISM2 SPECIFICS — verified (fixes §2.2)
- Config: SAME `ENVARC:sys/Wireless.prefs` + `ENV:sys/Wireless.prefs`,
  same `network={}` format. zenPrismWifi writes tab-indent + hashed psk
  (A.4 hashed form). Fully interoperable with the wifipi/AmiKit files.
- zenPrismWifi (Author Zener, GPL, MUI v3.8+, `Requires: 68020+,
  prism2.device`) is a working open-source PRIOR-ART reference for the
  ENTIRE tolunwifi prism2 adapter: scan (S2_GETNETWORKS), known-network
  list, PBKDF2 psk, dual-file write. Study its src; do not clone blindly.
- Legacy `SetPrism2Defaults` must be OFF when Wireless.prefs is used
  (as before) — the prefs file is authoritative.
- Miami handoff: zenPrismWifi ships `S:StartWifi.miami`/`StopWifi.miami`
  (run WirelessManager on Miami start/stop). Mirror this for a Miami
  target: associate before Miami opens the interface.

────────────────────────────────────────────────────────────────────────
# F. NET EFFECT ON THE MASTER PROMPT (apply these edits)
1. §2.0/§2.1/§2.2 path: `ENVARC:Sys/Wireless.prefs` (+ ENV: twin), NOT
   `wireless.prefs`/`wifipi.txt`. Write BOTH files always.
2. §4 caps: wifipi + prism2 → `can_scan`/`has_signal` REAL via
   S2_GETNETWORKS/S2_GETSIGNALQUALITY (probe first, degrade honestly).
   Drop "WPA2-CCMP only, no scan" — that was the alpha; current binaries
   parse full wpa_supplicant (proto/pairwise/group, WPA-PSK; SAE strings
   present → gate wpa3 on a real associate, never hard-off).
3. Adapter `write_config`: emit the A.4 blocks verbatim; ssid,psk,scan_ssid
   order; quoted-passphrase default, unquoted-64hex option (A.5).
4. Adapter `apply`: write pair → Break old WirelessManager PID → Run
   WirelessManager (B.2 wifipi / B.3 prism2). No fake "connected".
5. Adapter `scan`/`status`: D.3 pattern for scan; S2_GETSIGNALQUALITY +
   association state for status. Never invent.
6. Cite in code comments: sana2wireless.h (cmd/tag numbers), AmiKit
   network.on (command line), zenPrismWifi src (writer + scan + psk).

# G. LAW (unchanged, restated)
Every wifipi/prism2 config path, keyword, SANA-II wireless cmd code, and
S2INFO tag in this doc is copied from a real bench file — keep it that way.
A feature is DONE only when it wrote Wireless.prefs, WirelessManager
associated a real Amiga to a real AP, and the proof is pasted in STATUS.md.
