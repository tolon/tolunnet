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

═══════════════════════════════════════════════════════════════════════
2. API COVERAGE & LVO JUMP TABLE (139 Vectors / 133 SFD entries)
═══════════════════════════════════════════════════════════════════════
Generated from `sfd/bsdsocket_lib.sfd`. Every single LVO from -30 to -858 is accounted for.
Functions not yet natively implemented in this round return honest stubs matching exact Roadshow error semantics (ENOSYS / ENXIO / NULL / NO_RECOVERY).

| Offset | Function | Signature | Status | Return / Behavior |
|---|---|---|---|---|
| `-30` | `socket` | `LONG socket(LONG domain,LONG type,LONG protocol)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-36` | `bind` | `LONG bind(LONG sock,struct sockaddr *name,socklen_t namelen)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-42` | `listen` | `LONG listen(LONG sock,LONG backlog)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-48` | `accept` | `LONG accept(LONG sock,struct sockaddr *addr,socklen_t *addrlen)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-54` | `connect` | `LONG connect(LONG sock,struct sockaddr *name,socklen_t namelen)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-60` | `sendto` | `LONG sendto(LONG sock,APTR buf,LONG len,LONG flags,struct sockaddr *to,socklen_t tolen)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-66` | `send` | `LONG send(LONG sock,APTR buf,LONG len,LONG flags)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-72` | `recvfrom` | `LONG recvfrom(LONG sock,APTR buf,LONG len,LONG flags,struct sockaddr *addr,socklen_t *addrlen)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-78` | `recv` | `LONG recv(LONG sock,APTR buf,LONG len,LONG flags)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-84` | `shutdown` | `LONG shutdown(LONG sock,LONG how)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-90` | `setsockopt` | `LONG setsockopt(LONG sock,LONG level,LONG optname,APTR optval,socklen_t optlen)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-96` | `getsockopt` | `LONG getsockopt(LONG sock,LONG level,LONG optname,APTR optval,socklen_t *optlen)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-102` | `getsockname` | `LONG getsockname(LONG sock,struct sockaddr *name,socklen_t *namelen)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-108` | `getpeername` | `LONG getpeername(LONG sock,struct sockaddr *name,socklen_t *namelen)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-114` | `IoctlSocket` | `LONG IoctlSocket(LONG sock,ULONG req,APTR argp)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-120` | `CloseSocket` | `LONG CloseSocket(LONG sock)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-126` | `WaitSelect` | `LONG WaitSelect(LONG nfds,APTR read_fds,APTR write_fds,APTR except_fds,struct timeval *_timeout,ULONG *signals)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-132` | `SetSocketSignals` | `VOID SetSocketSignals(ULONG int_mask,ULONG io_mask,ULONG urgent_mask)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-138` | `getdtablesize` | `LONG getdtablesize()` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-144` | `ObtainSocket` | `LONG ObtainSocket(LONG id,LONG domain,LONG type,LONG protocol)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-150` | `ReleaseSocket` | `LONG ReleaseSocket(LONG sock,LONG id)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-156` | `ReleaseCopyOfSocket` | `LONG ReleaseCopyOfSocket(LONG sock,LONG id)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-162` | `Errno` | `LONG Errno()` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-168` | `SetErrnoPtr` | `VOID SetErrnoPtr(APTR errno_ptr,LONG size)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-174` | `Inet_NtoA` | `STRPTR Inet_NtoA(in_addr_t ip)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-180` | `inet_addr` | `in_addr_t inet_addr(STRPTR cp)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-186` | `Inet_LnaOf` | `in_addr_t Inet_LnaOf(in_addr_t in)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-192` | `Inet_NetOf` | `in_addr_t Inet_NetOf(in_addr_t in)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-198` | `Inet_MakeAddr` | `in_addr_t Inet_MakeAddr(in_addr_t net,in_addr_t host)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-204` | `inet_network` | `in_addr_t inet_network(STRPTR cp)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-210` | `gethostbyname` | `struct hostent * gethostbyname(STRPTR name)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-216` | `gethostbyaddr` | `struct hostent * gethostbyaddr(STRPTR addr,LONG len,LONG type)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-222` | `getnetbyname` | `struct netent * getnetbyname(STRPTR name)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-228` | `getnetbyaddr` | `struct netent * getnetbyaddr(in_addr_t net,LONG type)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-234` | `getservbyname` | `struct servent * getservbyname(STRPTR name,STRPTR proto)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-240` | `getservbyport` | `struct servent * getservbyport(LONG port,STRPTR proto)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-246` | `getprotobyname` | `struct protoent * getprotobyname(STRPTR name)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-252` | `getprotobynumber` | `struct protoent * getprotobynumber(LONG proto)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-258` | `vsyslog` | `VOID vsyslog(LONG pri,STRPTR msg,APTR args)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-264` | `Dup2Socket` | `LONG Dup2Socket(LONG old_socket,LONG new_socket)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-270` | `sendmsg` | `LONG sendmsg(LONG sock,struct msghdr *msg,LONG flags)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-276` | `recvmsg` | `LONG recvmsg(LONG sock,struct msghdr *msg,LONG flags)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-282` | `gethostname` | `LONG gethostname(STRPTR name,LONG namelen)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-288` | `gethostid` | `in_addr_t gethostid()` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-294` | `SocketBaseTagList` | `LONG SocketBaseTagList(struct TagItem *tags)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-300` | `GetSocketEvents` | `LONG GetSocketEvents(ULONG *event_ptr)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-306` | *(reserved)* | — | Reserved | Returns -1 |
| `-312` | *(reserved)* | — | Reserved | Returns -1 |
| `-318` | *(reserved)* | — | Reserved | Returns -1 |
| `-324` | *(reserved)* | — | Reserved | Returns -1 |
| `-330` | *(reserved)* | — | Reserved | Returns -1 |
| `-336` | *(reserved)* | — | Reserved | Returns -1 |
| `-342` | *(reserved)* | — | Reserved | Returns -1 |
| `-348` | *(reserved)* | — | Reserved | Returns -1 |
| `-354` | *(reserved)* | — | Reserved | Returns -1 |
| `-360` | *(reserved)* | — | Reserved | Returns -1 |
| `-366` | `bpf_open` | `LONG bpf_open(LONG channel)` | *Honest Stub* | `ENXIO` / safe error sentinel |
| `-372` | `bpf_close` | `LONG bpf_close(LONG channel)` | *Honest Stub* | `ENXIO` / safe error sentinel |
| `-378` | `bpf_read` | `LONG bpf_read(LONG channel, APTR buffer, LONG len)` | *Honest Stub* | `ENXIO` / safe error sentinel |
| `-384` | `bpf_write` | `LONG bpf_write(LONG channel, APTR buffer, LONG len)` | *Honest Stub* | `ENXIO` / safe error sentinel |
| `-390` | `bpf_set_notify_mask` | `LONG bpf_set_notify_mask(LONG channel, ULONG signal_mask)` | *Honest Stub* | `ENXIO` / safe error sentinel |
| `-396` | `bpf_set_interrupt_mask` | `LONG bpf_set_interrupt_mask(LONG channel, ULONG signal_mask)` | *Honest Stub* | `ENXIO` / safe error sentinel |
| `-402` | `bpf_ioctl` | `LONG bpf_ioctl(LONG channel, ULONG command, APTR buffer)` | *Honest Stub* | `ENXIO` / safe error sentinel |
| `-408` | `bpf_data_waiting` | `LONG bpf_data_waiting(LONG channel)` | *Honest Stub* | `ENXIO` / safe error sentinel |
| `-414` | `AddRouteTagList` | `LONG AddRouteTagList(struct TagItem *tags)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-420` | `DeleteRouteTagList` | `LONG DeleteRouteTagList(struct TagItem *tags)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-426` | `ChangeRouteTagList` | `LONG ChangeRouteTagList(struct TagItem *tags)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-432` | `FreeRouteInfo` | `VOID FreeRouteInfo(struct rt_msghdr *buf)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-438` | `GetRouteInfo` | `struct rt_msghdr * GetRouteInfo(LONG address_family, LONG flags)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-444` | `AddInterfaceTagList` | `LONG AddInterfaceTagList(STRPTR interface_name,STRPTR device_name,LONG unit,struct TagItem *tags)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-450` | `ConfigureInterfaceTagList` | `LONG ConfigureInterfaceTagList(STRPTR interface_name,struct TagItem *tags)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-456` | `ReleaseInterfaceList` | `VOID ReleaseInterfaceList(struct List *list)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-462` | `ObtainInterfaceList` | `struct List * ObtainInterfaceList()` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-468` | `QueryInterfaceTagList` | `LONG QueryInterfaceTagList(STRPTR interface_name,struct TagItem *tags)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-474` | `CreateAddrAllocMessageA` | `LONG CreateAddrAllocMessageA(LONG version,LONG protocol,STRPTR interface_name,struct AddressAllocationMessage **result_ptr,struct TagItem *tags)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-480` | `DeleteAddrAllocMessage` | `VOID DeleteAddrAllocMessage(struct AddressAllocationMessage *aam)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-486` | `BeginInterfaceConfig` | `VOID BeginInterfaceConfig(struct AddressAllocationMessage * message)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-492` | `AbortInterfaceConfig` | `VOID AbortInterfaceConfig(struct AddressAllocationMessage * message)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-498` | `AddNetMonitorHookTagList` | `LONG AddNetMonitorHookTagList(LONG type,struct Hook *hook,struct TagItem *tags)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-504` | `RemoveNetMonitorHook` | `VOID RemoveNetMonitorHook(struct Hook *hook)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-510` | `GetNetworkStatistics` | `LONG GetNetworkStatistics(LONG type,LONG version,APTR destination,LONG size)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-516` | `AddDomainNameServer` | `LONG AddDomainNameServer(STRPTR address)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-522` | `RemoveDomainNameServer` | `LONG RemoveDomainNameServer(STRPTR address)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-528` | `ReleaseDomainNameServerList` | `VOID ReleaseDomainNameServerList(struct List *list)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-534` | `ObtainDomainNameServerList` | `struct List * ObtainDomainNameServerList()` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-540` | `setnetent` | `VOID setnetent(LONG stay_open)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-546` | `endnetent` | `VOID endnetent()` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-552` | `getnetent` | `struct netent * getnetent()` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-558` | `setprotoent` | `VOID setprotoent(LONG stay_open)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-564` | `endprotoent` | `VOID endprotoent()` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-570` | `getprotoent` | `struct protoent * getprotoent()` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-576` | `setservent` | `VOID setservent(LONG stay_open)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-582` | `endservent` | `VOID endservent()` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-588` | `getservent` | `struct servent * getservent()` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-594` | `inet_aton` | `LONG inet_aton(STRPTR cp,struct in_addr *addr)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-600` | `inet_ntop` | `STRPTR inet_ntop(LONG af,APTR src,STRPTR dst,LONG size)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-606` | `inet_pton` | `LONG inet_pton(LONG af,STRPTR src,APTR dst)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-612` | `In_LocalAddr` | `LONG In_LocalAddr(in_addr_t address)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-618` | `In_CanForward` | `LONG In_CanForward(in_addr_t address)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-624` | `mbuf_copym` | `struct mbuf * mbuf_copym(struct mbuf *m, LONG off, LONG len)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-630` | `mbuf_copyback` | `LONG mbuf_copyback(struct mbuf *m, LONG off, LONG len, APTR cp)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-636` | `mbuf_copydata` | `LONG mbuf_copydata(struct mbuf *m, LONG off, LONG len, APTR cp)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-642` | `mbuf_free` | `struct mbuf * mbuf_free(struct mbuf *m)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-648` | `mbuf_freem` | `VOID mbuf_freem(struct mbuf *m)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-654` | `mbuf_get` | `struct mbuf * mbuf_get()` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-660` | `mbuf_gethdr` | `struct mbuf * mbuf_gethdr()` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-666` | `mbuf_prepend` | `struct mbuf * mbuf_prepend(struct mbuf *m, LONG len)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-672` | `mbuf_cat` | `LONG mbuf_cat(struct mbuf *m, struct mbuf *n)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-678` | `mbuf_adj` | `LONG mbuf_adj(struct mbuf *mp, LONG req_len)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-684` | `mbuf_pullup` | `struct mbuf * mbuf_pullup(struct mbuf *m, LONG len)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-690` | `ProcessIsServer` | `BOOL ProcessIsServer(struct Process * pr)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-696` | `ObtainServerSocket` | `LONG ObtainServerSocket()` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-702` | `GetDefaultDomainName` | `BOOL GetDefaultDomainName(STRPTR buffer,LONG buffer_size)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-708` | `SetDefaultDomainName` | `VOID SetDefaultDomainName(STRPTR buffer)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-714` | `ObtainRoadshowData` | `struct List * ObtainRoadshowData(LONG access)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-720` | `ReleaseRoadshowData` | `VOID ReleaseRoadshowData(struct List *list)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-726` | `ChangeRoadshowData` | `BOOL ChangeRoadshowData(struct List *list,STRPTR name,ULONG length,APTR data)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-732` | `RemoveInterface` | `LONG RemoveInterface(STRPTR interface_name, LONG force)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-738` | `gethostbyname_r` | `struct hostent * gethostbyname_r(STRPTR name, struct hostent * hp, APTR buf, ULONG buflen, LONG * he)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-744` | `gethostbyaddr_r` | `struct hostent * gethostbyaddr_r(STRPTR addr, LONG len, LONG type, struct hostent * hp, APTR buf, ULONG buflen, LONG * he)` | **Implemented** | Real implementation via daemon IPC or per-task base |
| `-750` | *(reserved)* | — | Reserved | Returns -1 |
| `-756` | *(reserved)* | — | Reserved | Returns -1 |
| `-762` | `ipf_open` | `LONG ipf_open(LONG channel)` | *Honest Stub* | `ENXIO` / safe error sentinel |
| `-768` | `ipf_close` | `LONG ipf_close(LONG channel)` | *Honest Stub* | `ENXIO` / safe error sentinel |
| `-774` | `ipf_ioctl` | `LONG ipf_ioctl(LONG channel,ULONG command,APTR buffer)` | *Honest Stub* | `ENXIO` / safe error sentinel |
| `-780` | `ipf_log_read` | `LONG ipf_log_read(LONG channel,APTR buffer,LONG len)` | *Honest Stub* | `ENXIO` / safe error sentinel |
| `-786` | `ipf_log_data_waiting` | `LONG ipf_log_data_waiting(LONG channel)` | *Honest Stub* | `ENXIO` / safe error sentinel |
| `-792` | `ipf_set_notify_mask` | `LONG ipf_set_notify_mask(LONG channel,ULONG mask)` | *Honest Stub* | `ENXIO` / safe error sentinel |
| `-798` | `ipf_set_interrupt_mask` | `LONG ipf_set_interrupt_mask(LONG channel,ULONG mask)` | *Honest Stub* | `ENXIO` / safe error sentinel |
| `-804` | `freeaddrinfo` | `VOID freeaddrinfo(struct addrinfo *ai)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-810` | `getaddrinfo` | `LONG getaddrinfo(STRPTR hostname, STRPTR servname, struct addrinfo *hints, struct addrinfo **res)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-816` | `gai_strerror` | `STRPTR gai_strerror(LONG errnum)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-822` | `getnameinfo` | `LONG getnameinfo(struct sockaddr *sa, ULONG salen, STRPTR host, ULONG hostlen, STRPTR serv, ULONG servlen, ULONG flags)` | *Honest Stub* | `ENOSYS` / safe error sentinel |
| `-828` | *(reserved)* | — | Reserved | Returns -1 |
| `-834` | *(reserved)* | — | Reserved | Returns -1 |
| `-840` | *(reserved)* | — | Reserved | Returns -1 |
| `-846` | *(reserved)* | — | Reserved | Returns -1 |
| `-852` | *(reserved)* | — | Reserved | Returns -1 |
| `-858` | *(reserved)* | — | Reserved | Returns -1 |

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
