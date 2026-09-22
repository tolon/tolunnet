# TOLUNNET — bsdsocket / Roadshow COMPATIBILITY SPECIFICATION
Goal (user directive): **100% compatible** — every app written for
Roadshow / AmiTCP V4 bsdsocket.library runs unmodified on tolunnet.
This document is the authoritative implementation spec. All reference
values below are quoted from **Roadshow SDK 1.8** on the dev bench
(`netinclude/`, `doc/bsdsocket.doc`, `sfd/bsdsocket_lib.sfd`) — cite these,
never memory.

Researched 2026-08-14 against the real SDK headers.

═══════════════════════════════════════════════════════════════════════
0. WHAT "100% COMPATIBLE" MEANS — two tiers
═══════════════════════════════════════════════════════════════════════
TIER 1 — the socket-application API (MANDATORY for 100%).
Everything an app links against and calls: the BSD socket calls, name/
service resolution, WaitSelect, IoctlSocket, and **SocketBaseTagList**
(how apps configure the base). If Tier 1 is exact, AmiSSL, amiget,
Amelinium, IBrowse, smbfs, mail/ftp clients run. This is the real target.

TIER 2 — the Roadshow control/monitor API (OPTIONAL, decide explicitly).
AddInterfaceTagList, ConfigureInterfaceTagList, ObtainInterfaceList,
AddRouteTagList, GetNetworkStatistics, AddDomainNameServer, bpf_*,
ObtainRoadshowData… These drive Roadshow's OWN config/monitor tools, not
socket apps. Full bit-for-bit "replace Roadshow including its utilities"
needs them; "run every socket app" does not. Recommendation: Tier 1 =
100% now; Tier 2 = answer the SBTC_HAVE_*_API capability queries honestly
(say "not supported") and add pieces later if a wanted tool needs them.

The capability tags exist precisely so an app can ASK what the stack does
(SBTC_HAVE_ROUTING_API, _INTERFACE_API, _MONITORING_API, _STATUS_API,
_DNS_API, _LOCAL_DATABASE_API, _ADDRESS_CONVERSION_API). A compliant
stack answers them; tolunnet must answer, not ignore.

═══════════════════════════════════════════════════════════════════════
1. AUTHORITATIVE REFERENCE DATA (from Roadshow SDK 1.8 — use verbatim)
═══════════════════════════════════════════════════════════════════════

## 1.1 errno values — Amiga numbers (netinclude/sys/errno.h). MUST match.
These are NOT the same as lwIP's or host gcc's. tolunnet MUST return
these exact numbers:
  EINTR 4 · EBADF 9 · ENOMEM 12 · EACCES 13 · EFAULT 14 · EINVAL 22 ·
  ENFILE 23 · EMFILE 24 · EPIPE 32 · EAGAIN 35 · EWOULDBLOCK=EAGAIN(35) ·
  EINPROGRESS 36 · EALREADY 37 · ENOTSOCK 38 · EDESTADDRREQ 39 ·
  EMSGSIZE 40 · EADDRINUSE 48 · EADDRNOTAVAIL 49 · ENETDOWN 50 ·
  ENETUNREACH 51 · ECONNABORTED 53 · ECONNRESET 54 · ENOBUFS 55 ·
  EISCONN 56 · ENOTCONN 57 · ETIMEDOUT 60(verify) · ECONNREFUSED(verify) ·
  EHOSTUNREACH(verify).
ACTION: the code `#include <sys/errno.h>` MUST resolve to the Roadshow/
NDK **netinclude/sys/errno.h**, never lwIP's `lwip/errno.h`. Verify the
include path order puts netinclude first. The lwIP-err_t→errno map
(AUDIT §5.1) must land on THESE numbers.

## 1.2 struct hostent (netinclude/netdb.h) — MUST match layout exactly
```
struct hostent {
    STRPTR   h_name;        /* official name            */
    STRPTR  *h_aliases;     /* NULL-terminated alias vec */
    LONG     h_addrtype;    /* AF_INET (=2)             */
    LONG     h_length;      /* 4 for IPv4               */
    BYTE   **h_addr_list;   /* NULL-terminated addr vec  */
};  /* h_addr == h_addr_list[0] */
```
gethostbyname MUST return a pointer to this exact structure, in a
PER-OPENER buffer that stays valid until the opener's next resolver call
(Roadshow semantics). Addresses in NETWORK byte order.

## 1.3 struct servent (netdb.h)
```
struct servent { STRPTR s_name; STRPTR *s_aliases; LONG s_port; STRPTR s_proto; };
```
s_port in NETWORK byte order.

## 1.4 struct sockaddr_in (netinet/in.h) — note sin_len + sin_family byte
```
struct sockaddr_in {
    UBYTE       sin_len;      /* 16 */
    UBYTE       sin_family;   /* AF_INET = 2 */
    UWORD       sin_port;     /* network order */
    struct in_addr sin_addr;  /* network order */
    UBYTE       sin_zero[8];
};
```
The BSD44 `sin_len`/1-byte-family layout is what Roadshow uses — a
sockaddr with a 2-byte family (old style) will misparse. Match this.

## 1.5 IoctlSocket commands (netinclude/sys/filio.h)
```
FIONREAD = _IOR('f',127,long)   /* bytes available to read */
FIONBIO  = _IOW('f',126,long)   /* set/clear non-blocking   */
FIOASYNC = _IOW('f',125,long)   /* set/clear async (SIGIO)  */
```
Use the `_IOR/_IOW` macro values from the SDK header — do not hardcode a
guessed number. IoctlSocket must recognise all three.

## 1.6 SocketBaseTagList tag codes (netinclude/libraries/bsdsocket.h)
Tag value = SBTM_{GET,SET}{VAL,REF}(SBTC_code). Decode with:
  SBTF_SET (bit0) → set vs get; SBTF_REF (0x8000) → ti_Data is a POINTER
  to the value vs the value itself; SBTM_CODE(td) = (td>>1)&0x3FFF.
SBTC codes tolunnet MUST handle (exact numbers):
  SBTC_BREAKMASK 1 · SBTC_SIGIOMASK 2 · SBTC_SIGURGMASK 3 ·
  SBTC_SIGEVENTMASK 4 · SBTC_ERRNO 6 · SBTC_HERRNO 7 · SBTC_DTABLESIZE 8 ·
  SBTC_FDCALLBACK 9 · SBTC_LOGSTAT 10 · SBTC_LOGTAGPTR 11 ·
  SBTC_LOGFACILITY 12 · SBTC_LOGMASK 13 · SBTC_ERRNOSTRPTR 14 ·
  SBTC_HERRNOSTRPTR 15 · SBTC_ERRNOBYTEPTR 21 · SBTC_ERRNOWORDPTR 22 ·
  SBTC_ERRNOLONGPTR 24 · SBTC_HERRNOLONGPTR 25 · SBTC_RELEASESTRPTR 29.
Capability queries (answer on GET):
  SBTC_HAVE_ROUTING_API 41 · SBTC_HAVE_INTERFACE_API 47 ·
  SBTC_HAVE_MONITORING_API 50 · SBTC_CAN_SHARE_LIBRARY_BASES 51 ·
  SBTC_HAVE_STATUS_API 53 · SBTC_HAVE_DNS_API 54 ·
  SBTC_HAVE_LOCAL_DATABASE_API 59 · SBTC_HAVE_ADDRESS_CONVERSION_API 60.
The one apps set FIRST and depend on: **SBTC_ERRNOLONGPTR / SBTC_ERRNOPTR
family** (where to write errno) and **SBTC_HERRNOLONGPTR** (h_errno).

=======================================================================
2. API COVERAGE & LVO JUMP TABLE
=======================================================================
The vector-by-vector table is GENERATED — never hand-edited:
[src/lib/lib_compat_table.gen.md](src/lib/lib_compat_table.gen.md)

Sources: `sfd/bsdsocket_lib.sfd` (LVO offsets + prototypes),
`src/lib/lib_vectors.c` + `src/task/ipc_dispatch.c` (BUILT/BROKEN/STUB),
and the latest bench TAP (PASS column = green tc_* coverage).
Regenerate with `scripts/gen_lvo_table.py`; `make python-checks` fails
when the committed table is stale (ANX-02).

═══════════════════════════════════════════════════════════════════════
3. IMPLEMENTATION SPEC — the blockers, precisely
═══════════════════════════════════════════════════════════════════════

## COMPAT-1 — SocketBaseTagList (THE blocker; apps die in init without it)
```
LONG SocketBaseTagList(struct TagItem *tags, base):
  for each tag:
     code = SBTM_CODE(ti_Tag)                 /* (ti_Tag>>1)&0x3FFF */
     isSet = ti_Tag & SBTF_SET                /* bit 0 */
     isRef = ti_Tag & (SBTF_REF<<... )        /* per header: 0x8000 in code word */
     switch(code):
       ERRNOLONGPTR/ERRNOPTR/ERRNOBYTEPTR/ERRNOWORDPTR:
           set → base->errno_ptr = (LONG*)ti_Data (record width);
       HERRNOLONGPTR/HERRNO: set → base->herrno_ptr = ti_Data
       SIGIOMASK: set → base->sig_io = ti_Data ; get → ti_Data = base->sig_io
       SIGURGMASK: base->sig_urg ; BREAKMASK: base->sig_int
       DTABLESIZE: get → TN_MAX_FDS_PER_TASK
       HAVE_*_API: get → 0 (Tier2 not supported) or 1 where true
       RELEASESTRPTR / LOG*: accept, store or ignore safely
     return count of tags processed (Roadshow returns number handled;
     match its convention — see doc/bsdsocket.doc).
SocketBaseTags(tag,...) = build a TagItem[] from varargs and call the
above.
```
Wire both LVOs (-294, and the varargs front end) to this — remove the
tn_stub_neg1. Get the SBTC_/SBTM_ values from
`netinclude/libraries/bsdsocket.h` (ship a copy in include/ with a
provenance note, or include the SDK path in the build).

## COMPAT-2 — services / protocols tables (getservbyname etc.)
Static tables, network-byte-order ports:
  http 80, https 443, ftp 21, ftp-data 20, ssh 22, telnet 23, smtp 25,
  domain 53, ntp 123, pop3 110, imap 143, finger 79, time 37, echo 7.
  protocols: icmp 1, tcp 6, udp 17.
Return a per-opener struct servent/protoent. getservbyport is the reverse.

## COMPAT-3 — errno source + hostent layout (silent-breakage guards)
- Ensure `<sys/errno.h>` = netinclude's (values in §1.1). Add a build
  assert: `STATIC_ASSERT(EWOULDBLOCK==35 && EINPROGRESS==36)`.
- gethostbyname builds the §1.2 struct in a per-opener buffer; h_addrtype
  AF_INET, h_length 4, addresses network order, vectors NULL-terminated.

## COMPAT-4 — Tier-2 honesty
Leave the interface/route/monitor/bpf extensions unimplemented BUT answer
their SBTC_HAVE_*_API queries with 0, and return -1/ENOSYS (not a fake 0)
from the extension LVOs if ever called. Never advertise a capability you
lack (this is the §89 rule applied to the network API).

═══════════════════════════════════════════════════════════════════════
4. PROOF — 100% is a CLAIM until these pass (contract M6 gate)
═══════════════════════════════════════════════════════════════════════
Compatibility is DESIGNED, not proven, until real apps run AND behaviour
is compared to Roadshow. Required, each with pasted logs in STATUS.md:
1. **Probe oracle** (contract §8): for socket(), connect(), WaitSelect(),
   SocketBaseTagList(errno ptr), gethostbyname(), getservbyname() — run a
   tiny probe under Roadshow demo AND under tolunnet; outputs must match.
   Archive in docs/probes/.
2. **AmiSSL over tolunnet** — the deepest bsdsocket user; if it completes
   a TLS handshake, SocketBaseTagList + WaitSelect + errno are right.
3. **amiget** — Aminet fetch (gethostbyname + connect + recv).
4. **Amelinium** — page load (getservbyname + full socket path).
5. **smbfs** — mount (long-lived sockets, IoctlSocket).
Only after 1–5 pass may README say "compatible with Roadshow apps."

═══════════════════════════════════════════════════════════════════════
5. VERDICT & ORDER
═══════════════════════════════════════════════════════════════════════
Today (2026-09-05): client-style socket apps work (socket/connect/send/
recv/sendto/recvfrom, DNS, tags, errno, services table). **Not 100%** —
steps 1–3 below are done in code, but:
  a. server-side calls (bind/listen/accept, shutdown/getsockname/getpeername)
     return ENOSYS from the daemon — TNET-077, found in the 2026-09-05 doc
     audit;
  b. nothing has been probe-verified against a Roadshow oracle yet (§4);
  c. ObtainSocket/ReleaseSocket, GetSocketEvents, gethostbyaddr (TNET-068),
     SOCK_RAW (TNET-070), getaddrinfo are still missing.
Order to reach 100% (Tier 1):
  1. ✅ COMPAT-1 SocketBaseTagList (done, TNET-036).
  2. ✅ COMPAT-3 errno source + hostent layout (done; probe still required).
  3. ✅ COMPAT-2 services/protocols tables; Inet_* helpers; gethostname/id;
     Dup2Socket.
  4. TNET-077: daemon-side bind/listen/accept/shutdown/getsockname/
     getpeername handlers (server apps!).
  5. Obtain/ReleaseSocket; gethostbyaddr (TNET-068); SOCK_RAW (TNET-070);
     getaddrinfo/freeaddrinfo/getnameinfo (Tier 1.5).
  6. COMPAT-4 Tier-2 capability queries answered honestly (done in
     SocketBaseTagList; keep aligned when new APIs land).
  7. §4 proof matrix — probe oracle + AmiSSL/amiget/Amelinium/smbfs, logs
     pasted. THEN and only then: "100% compatible" in the README.
Tier-2 control/monitor API: decide per wanted tool; not needed for app
compatibility.

═══════════════════════════════════════════════════════════════════════
6. MIAMI / AmiTCP / GENESIS — already covered by the same target
═══════════════════════════════════════════════════════════════════════
Miami and Miami Deluxe (Holger Kruse), AmiTCP, and Genesis all implement
the SAME `bsdsocket.library` API — the AmiTCP-derived standard Roadshow
also follows. Apps never call "Miami" or "Roadshow"; they
OpenLibrary("bsdsocket.library") and call the LVOs in §1–§3. Therefore:
- **Tier-1 compatibility in this spec = Miami/AmiTCP/Genesis app
  compatibility automatically.** Same SBTC_ tags, same struct hostent /
  sockaddr_in, same errno numbers, same LVO layout. No separate Miami
  work — do NOT write Miami-specific code.
- Miami's own config surface (MiamiInit, Miami.library / MiamiDx.library,
  its ARexx port) is Miami's equivalent of Roadshow's Tier-2 control
  extensions — used by Miami's configurator, not by socket apps. Out of
  scope, same as §0 Tier 2.
- BONUS TEST ORACLE: **CaffeineOS ships Miami DX** — the user already has
  a running Miami. Add it beside Roadshow demo in the §4 probe oracle:
  run the same probe under Roadshow demo AND Miami DX AND tolunnet and
  match all three. Two independent reference stacks are the strongest
  proof of standard-conformance.
- Version note: very old Miami predates a few late SBTC_ tags (e.g.
  SBTC_SIG_ADDRESS_CHANGE_MASK); apps that must run on old Miami use only
  the common subset, so implementing the full §1.6 set is a safe
  superset. Answer SBTC_HAVE_*_API honestly and both old and new apps
  cope.
Bottom line: aim at the bsdsocket standard (this spec) and Miami falls
out for free.

---

## 5. What the OS knows about the stack

(Moved verbatim from the retired `docs/compat.md` §3 when that hand-written
ledger was replaced by the generated table — ANX-02.)

When `tolunnet` is running and configured by `TolunnetSetup` (or `TolunnetPrefs`), the AmigaOS system environment reflects the stack through standard system interfaces:

1. **`bsdsocket.library` in Exec `LibList`**:
   - Registered dynamically in RAM via `AddLibrary()` with standard 139-vector LVO table.
   - Applications open it via standard `OpenLibrary("bsdsocket.library", 4)`.

2. **`HostName` & `Domain` Environment Variables**:
   - `ENV:HostName` and `ENVARC:HostName`: plain hostname string without newline. Many Amiga applications (e.g. AmiTCP utilities, mail clients, IRC clients) read this before or instead of calling `gethostname()`.
   - `ENV:Domain` and `ENVARC:Domain`: search domain string if configured.

3. **`S:User-Startup` Boot Block**:
   - Clean, isolated block delimited by `; BEGIN tolunnet` and `; END tolunnet`:
     ```amiga
     ; BEGIN tolunnet
     Run <NIL: >NIL: C:tolunnet
     ; END tolunnet
     ```
   - Legacy stack lines (Miami, AmiTCP, Genesis, Roadshow) are safely backed up to `S:User-Startup.tolunnet-bak` and disabled with `; tolunnet-disabled: <line>`.

4. **Optional `DEVS:Internet/*` and `DEVS:NetInterfaces/*` Mirror**:
   - If enabled in setup, `DEVS:NetInterfaces/Ethernet` (or `WiFiPi`) is written in Roadshow-compatible KEY=VALUE syntax for interoperability with third-party tools expecting Roadshow configuration files.

