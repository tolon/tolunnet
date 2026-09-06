## 2. API Coverage & LVO Jump Table (Generated from SFD)

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
