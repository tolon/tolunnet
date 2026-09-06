|
| lib_stubs.gen.s — Generated 68k LVO Assembly Dispatch Stubs
| Generated automatically by scripts/gen_lvo_table.py from sfd/bsdsocket_lib.sfd.
|

    .text
    .even

| --- Library Management Vectors ---
    .globl _tn_stub_open
_tn_stub_open:
    move.l  d0,-(sp)
    move.l  a6,-(sp)
    jsr     _tn_lib_open
    addq.l  #8,sp
    move.l  d0,a0
    rts

    .globl _tn_stub_close
_tn_stub_close:
    move.l  a6,-(sp)
    jsr     _tn_lib_close
    addq.l  #4,sp
    move.l  d0,a0
    rts

    .globl _tn_stub_expunge
_tn_stub_expunge:
    move.l  a6,-(sp)
    jsr     _tn_lib_expunge
    addq.l  #4,sp
    move.l  d0,a0
    rts

    .globl _tn_stub_reserved
_tn_stub_reserved:
    moveq   #0,d0
    suba.l  a0,a0
    rts

    .globl _tn_stub_neg1
_tn_stub_neg1:
    moveq   #-1,d0
    suba.l  a0,a0
    rts

| -30: socket(LONG domain,LONG type,LONG protocol)
    .globl _tn_stub_socket
_tn_stub_socket:
    move.l  a6,-(sp)
    move.l  d2,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_socket
    lea     16(sp),sp
    rts

| -36: bind(LONG sock,struct sockaddr *name,socklen_t namelen)
    .globl _tn_stub_bind
_tn_stub_bind:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_bind
    lea     16(sp),sp
    rts

| -42: listen(LONG sock,LONG backlog)
    .globl _tn_stub_listen
_tn_stub_listen:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_listen
    lea     12(sp),sp
    rts

| -48: accept(LONG sock,struct sockaddr *addr,socklen_t *addrlen)
    .globl _tn_stub_accept
_tn_stub_accept:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_accept
    lea     16(sp),sp
    rts

| -54: connect(LONG sock,struct sockaddr *name,socklen_t namelen)
    .globl _tn_stub_connect
_tn_stub_connect:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_connect
    lea     16(sp),sp
    rts

| -60: sendto(LONG sock,APTR buf,LONG len,LONG flags,struct sockaddr *to,socklen_t tolen)
    .globl _tn_stub_sendto
_tn_stub_sendto:
    move.l  a6,-(sp)
    move.l  d3,-(sp)
    move.l  a1,-(sp)
    move.l  d2,-(sp)
    move.l  d1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_sendto
    lea     28(sp),sp
    rts

| -66: send(LONG sock,APTR buf,LONG len,LONG flags)
    .globl _tn_stub_send
_tn_stub_send:
    move.l  a6,-(sp)
    move.l  d2,-(sp)
    move.l  d1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_send
    lea     20(sp),sp
    rts

| -72: recvfrom(LONG sock,APTR buf,LONG len,LONG flags,struct sockaddr *addr,socklen_t *addrlen)
    .globl _tn_stub_recvfrom
_tn_stub_recvfrom:
    move.l  a6,-(sp)
    move.l  a2,-(sp)
    move.l  a1,-(sp)
    move.l  d2,-(sp)
    move.l  d1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_recvfrom
    lea     28(sp),sp
    rts

| -78: recv(LONG sock,APTR buf,LONG len,LONG flags)
    .globl _tn_stub_recv
_tn_stub_recv:
    move.l  a6,-(sp)
    move.l  d2,-(sp)
    move.l  d1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_recv
    lea     20(sp),sp
    rts

| -84: shutdown(LONG sock,LONG how)
    .globl _tn_stub_shutdown
_tn_stub_shutdown:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_shutdown
    lea     12(sp),sp
    rts

| -90: setsockopt(LONG sock,LONG level,LONG optname,APTR optval,socklen_t optlen)
    .globl _tn_stub_setsockopt
_tn_stub_setsockopt:
    move.l  a6,-(sp)
    move.l  d3,-(sp)
    move.l  a0,-(sp)
    move.l  d2,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_setsockopt
    lea     24(sp),sp
    rts

| -96: getsockopt(LONG sock,LONG level,LONG optname,APTR optval,socklen_t *optlen)
    .globl _tn_stub_getsockopt
_tn_stub_getsockopt:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    move.l  d2,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_getsockopt
    lea     24(sp),sp
    rts

| -102: getsockname(LONG sock,struct sockaddr *name,socklen_t *namelen)
    .globl _tn_stub_getsockname
_tn_stub_getsockname:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_getsockname
    lea     16(sp),sp
    rts

| -108: getpeername(LONG sock,struct sockaddr *name,socklen_t *namelen)
    .globl _tn_stub_getpeername
_tn_stub_getpeername:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_getpeername
    lea     16(sp),sp
    rts

| -114: IoctlSocket(LONG sock,ULONG req,APTR argp)
    .globl _tn_stub_ioctlsocket
_tn_stub_ioctlsocket:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_ioctlsocket
    lea     16(sp),sp
    rts

| -120: CloseSocket(LONG sock)
    .globl _tn_stub_closesocket
_tn_stub_closesocket:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_closesocket
    addq.l  #8,sp
    rts

| -126: WaitSelect(LONG nfds,APTR read_fds,APTR write_fds,APTR except_fds,struct timeval *_timeout,ULONG *signals)
    .globl _tn_stub_waitselect
_tn_stub_waitselect:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  a3,-(sp)
    move.l  a2,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_waitselect
    lea     28(sp),sp
    rts

| -132: SetSocketSignals(ULONG int_mask,ULONG io_mask,ULONG urgent_mask)
    .globl _tn_stub_setsocketsignals
_tn_stub_setsocketsignals:
    move.l  a6,-(sp)
    move.l  d2,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_setsocketsignals
    lea     16(sp),sp
    rts

| -138: getdtablesize()
    .globl _tn_stub_getdtablesize
_tn_stub_getdtablesize:
    move.l  a6,-(sp)
    jsr     _tn_lvo_getdtablesize
    addq.l  #4,sp
    rts

| -144: ObtainSocket(LONG id,LONG domain,LONG type,LONG protocol)
    .globl _tn_stub_obtainsocket
_tn_stub_obtainsocket:
    move.l  a6,-(sp)
    move.l  d3,-(sp)
    move.l  d2,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_obtainsocket
    lea     20(sp),sp
    rts

| -150: ReleaseSocket(LONG sock,LONG id)
    .globl _tn_stub_releasesocket
_tn_stub_releasesocket:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_releasesocket
    lea     12(sp),sp
    rts

| -156: ReleaseCopyOfSocket(LONG sock,LONG id)
    .globl _tn_stub_releasecopyofsocket
_tn_stub_releasecopyofsocket:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_releasecopyofsocket
    lea     12(sp),sp
    rts

| -162: Errno()
    .globl _tn_stub_errno
_tn_stub_errno:
    move.l  a6,-(sp)
    jsr     _tn_lvo_errno
    addq.l  #4,sp
    rts

| -168: SetErrnoPtr(APTR errno_ptr,LONG size)
    .globl _tn_stub_seterrnoptr
_tn_stub_seterrnoptr:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_seterrnoptr
    lea     12(sp),sp
    rts

| -174: Inet_NtoA(in_addr_t ip)
    .globl _tn_stub_inet_ntoa
_tn_stub_inet_ntoa:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_inet_ntoa
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -180: inet_addr(STRPTR cp)
    .globl _tn_stub_inet_addr
_tn_stub_inet_addr:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_inet_addr
    addq.l  #8,sp
    rts

| -186: Inet_LnaOf(in_addr_t in)
    .globl _tn_stub_inet_lnaof
_tn_stub_inet_lnaof:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_inet_lnaof
    addq.l  #8,sp
    rts

| -192: Inet_NetOf(in_addr_t in)
    .globl _tn_stub_inet_netof
_tn_stub_inet_netof:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_inet_netof
    addq.l  #8,sp
    rts

| -198: Inet_MakeAddr(in_addr_t net,in_addr_t host)
    .globl _tn_stub_inet_makeaddr
_tn_stub_inet_makeaddr:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_inet_makeaddr
    lea     12(sp),sp
    rts

| -204: inet_network(STRPTR cp)
    .globl _tn_stub_inet_network
_tn_stub_inet_network:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_inet_network
    addq.l  #8,sp
    rts

| -210: gethostbyname(STRPTR name)
    .globl _tn_stub_gethostbyname
_tn_stub_gethostbyname:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_gethostbyname
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -216: gethostbyaddr(STRPTR addr,LONG len,LONG type)
    .globl _tn_stub_gethostbyaddr
_tn_stub_gethostbyaddr:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_gethostbyaddr
    lea     16(sp),sp
    move.l  d0,a0
    rts

| -222: getnetbyname(STRPTR name)
    .globl _tn_stub_getnetbyname
_tn_stub_getnetbyname:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_getnetbyname
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -228: getnetbyaddr(in_addr_t net,LONG type)
    .globl _tn_stub_getnetbyaddr
_tn_stub_getnetbyaddr:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_getnetbyaddr
    lea     12(sp),sp
    move.l  d0,a0
    rts

| -234: getservbyname(STRPTR name,STRPTR proto)
    .globl _tn_stub_getservbyname
_tn_stub_getservbyname:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_getservbyname
    lea     12(sp),sp
    move.l  d0,a0
    rts

| -240: getservbyport(LONG port,STRPTR proto)
    .globl _tn_stub_getservbyport
_tn_stub_getservbyport:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_getservbyport
    lea     12(sp),sp
    move.l  d0,a0
    rts

| -246: getprotobyname(STRPTR name)
    .globl _tn_stub_getprotobyname
_tn_stub_getprotobyname:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_getprotobyname
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -252: getprotobynumber(LONG proto)
    .globl _tn_stub_getprotobynumber
_tn_stub_getprotobynumber:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_getprotobynumber
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -258: vsyslog(LONG pri,STRPTR msg,APTR args)
    .globl _tn_stub_vsyslog
_tn_stub_vsyslog:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_vsyslog
    lea     16(sp),sp
    rts

| -264: Dup2Socket(LONG old_socket,LONG new_socket)
    .globl _tn_stub_dup2socket
_tn_stub_dup2socket:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_dup2socket
    lea     12(sp),sp
    rts

| -270: sendmsg(LONG sock,struct msghdr *msg,LONG flags)
    .globl _tn_stub_sendmsg
_tn_stub_sendmsg:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_sendmsg
    lea     16(sp),sp
    rts

| -276: recvmsg(LONG sock,struct msghdr *msg,LONG flags)
    .globl _tn_stub_recvmsg
_tn_stub_recvmsg:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_recvmsg
    lea     16(sp),sp
    rts

| -282: gethostname(STRPTR name,LONG namelen)
    .globl _tn_stub_gethostname
_tn_stub_gethostname:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_gethostname
    lea     12(sp),sp
    rts

| -288: gethostid()
    .globl _tn_stub_gethostid
_tn_stub_gethostid:
    move.l  a6,-(sp)
    jsr     _tn_lvo_gethostid
    addq.l  #4,sp
    rts

| -294: SocketBaseTagList(struct TagItem *tags)
    .globl _tn_stub_socketbasetaglist
_tn_stub_socketbasetaglist:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_socketbasetaglist
    addq.l  #8,sp
    rts

| -300: GetSocketEvents(ULONG *event_ptr)
    .globl _tn_stub_getsocketevents
_tn_stub_getsocketevents:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_getsocketevents
    addq.l  #8,sp
    rts

| -366: bpf_open(LONG channel)
    .globl _tn_stub_bpf_open
_tn_stub_bpf_open:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_bpf_open
    addq.l  #8,sp
    rts

| -372: bpf_close(LONG channel)
    .globl _tn_stub_bpf_close
_tn_stub_bpf_close:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_bpf_close
    addq.l  #8,sp
    rts

| -378: bpf_read(LONG channel, APTR buffer, LONG len)
    .globl _tn_stub_bpf_read
_tn_stub_bpf_read:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_bpf_read
    lea     16(sp),sp
    rts

| -384: bpf_write(LONG channel, APTR buffer, LONG len)
    .globl _tn_stub_bpf_write
_tn_stub_bpf_write:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_bpf_write
    lea     16(sp),sp
    rts

| -390: bpf_set_notify_mask(LONG channel, ULONG signal_mask)
    .globl _tn_stub_bpf_set_notify_mask
_tn_stub_bpf_set_notify_mask:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    move.l  d1,-(sp)
    jsr     _tn_unimpl_bpf_set_notify_mask
    lea     12(sp),sp
    rts

| -396: bpf_set_interrupt_mask(LONG channel, ULONG signal_mask)
    .globl _tn_stub_bpf_set_interrupt_mask
_tn_stub_bpf_set_interrupt_mask:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_bpf_set_interrupt_mask
    lea     12(sp),sp
    rts

| -402: bpf_ioctl(LONG channel, ULONG command, APTR buffer)
    .globl _tn_stub_bpf_ioctl
_tn_stub_bpf_ioctl:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_bpf_ioctl
    lea     16(sp),sp
    rts

| -408: bpf_data_waiting(LONG channel)
    .globl _tn_stub_bpf_data_waiting
_tn_stub_bpf_data_waiting:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_bpf_data_waiting
    addq.l  #8,sp
    rts

| -414: AddRouteTagList(struct TagItem *tags)
    .globl _tn_stub_addroutetaglist
_tn_stub_addroutetaglist:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_addroutetaglist
    addq.l  #8,sp
    rts

| -420: DeleteRouteTagList(struct TagItem *tags)
    .globl _tn_stub_deleteroutetaglist
_tn_stub_deleteroutetaglist:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_deleteroutetaglist
    addq.l  #8,sp
    rts

| -426: ChangeRouteTagList(struct TagItem *tags)
    .globl _tn_stub_changeroutetaglist
_tn_stub_changeroutetaglist:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_changeroutetaglist
    addq.l  #8,sp
    rts

| -432: FreeRouteInfo(struct rt_msghdr *buf)
    .globl _tn_stub_freerouteinfo
_tn_stub_freerouteinfo:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_freerouteinfo
    addq.l  #8,sp
    rts

| -438: GetRouteInfo(LONG address_family, LONG flags)
    .globl _tn_stub_getrouteinfo
_tn_stub_getrouteinfo:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_getrouteinfo
    lea     12(sp),sp
    move.l  d0,a0
    rts

| -444: AddInterfaceTagList(STRPTR interface_name,STRPTR device_name,LONG unit,struct TagItem *tags)
    .globl _tn_stub_addinterfacetaglist
_tn_stub_addinterfacetaglist:
    move.l  a6,-(sp)
    move.l  a2,-(sp)
    move.l  d0,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_addinterfacetaglist
    lea     20(sp),sp
    rts

| -450: ConfigureInterfaceTagList(STRPTR interface_name,struct TagItem *tags)
    .globl _tn_stub_configureinterfacetaglist
_tn_stub_configureinterfacetaglist:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_configureinterfacetaglist
    lea     12(sp),sp
    rts

| -456: ReleaseInterfaceList(struct List *list)
    .globl _tn_stub_releaseinterfacelist
_tn_stub_releaseinterfacelist:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_releaseinterfacelist
    addq.l  #8,sp
    rts

| -462: ObtainInterfaceList()
    .globl _tn_stub_obtaininterfacelist
_tn_stub_obtaininterfacelist:
    move.l  a6,-(sp)
    jsr     _tn_unimpl_obtaininterfacelist
    addq.l  #4,sp
    move.l  d0,a0
    rts

| -468: QueryInterfaceTagList(STRPTR interface_name,struct TagItem *tags)
    .globl _tn_stub_queryinterfacetaglist
_tn_stub_queryinterfacetaglist:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_queryinterfacetaglist
    lea     12(sp),sp
    rts

| -474: CreateAddrAllocMessageA(LONG version,LONG protocol,STRPTR interface_name,struct AddressAllocationMessage **result_ptr,struct TagItem *tags)
    .globl _tn_stub_createaddrallocmessagea
_tn_stub_createaddrallocmessagea:
    move.l  a6,-(sp)
    move.l  a2,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_createaddrallocmessagea
    lea     24(sp),sp
    rts

| -480: DeleteAddrAllocMessage(struct AddressAllocationMessage *aam)
    .globl _tn_stub_deleteaddrallocmessage
_tn_stub_deleteaddrallocmessage:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_deleteaddrallocmessage
    addq.l  #8,sp
    rts

| -486: BeginInterfaceConfig(struct AddressAllocationMessage * message)
    .globl _tn_stub_begininterfaceconfig
_tn_stub_begininterfaceconfig:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_begininterfaceconfig
    addq.l  #8,sp
    rts

| -492: AbortInterfaceConfig(struct AddressAllocationMessage * message)
    .globl _tn_stub_abortinterfaceconfig
_tn_stub_abortinterfaceconfig:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_abortinterfaceconfig
    addq.l  #8,sp
    rts

| -498: AddNetMonitorHookTagList(LONG type,struct Hook *hook,struct TagItem *tags)
    .globl _tn_stub_addnetmonitorhooktaglist
_tn_stub_addnetmonitorhooktaglist:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_addnetmonitorhooktaglist
    lea     16(sp),sp
    rts

| -504: RemoveNetMonitorHook(struct Hook *hook)
    .globl _tn_stub_removenetmonitorhook
_tn_stub_removenetmonitorhook:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_removenetmonitorhook
    addq.l  #8,sp
    rts

| -510: GetNetworkStatistics(LONG type,LONG version,APTR destination,LONG size)
    .globl _tn_stub_getnetworkstatistics
_tn_stub_getnetworkstatistics:
    move.l  a6,-(sp)
    move.l  d2,-(sp)
    move.l  a0,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_getnetworkstatistics
    lea     20(sp),sp
    rts

| -516: AddDomainNameServer(STRPTR address)
    .globl _tn_stub_adddomainnameserver
_tn_stub_adddomainnameserver:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_adddomainnameserver
    addq.l  #8,sp
    rts

| -522: RemoveDomainNameServer(STRPTR address)
    .globl _tn_stub_removedomainnameserver
_tn_stub_removedomainnameserver:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_removedomainnameserver
    addq.l  #8,sp
    rts

| -528: ReleaseDomainNameServerList(struct List *list)
    .globl _tn_stub_releasedomainnameserverlist
_tn_stub_releasedomainnameserverlist:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_releasedomainnameserverlist
    addq.l  #8,sp
    rts

| -534: ObtainDomainNameServerList()
    .globl _tn_stub_obtaindomainnameserverlist
_tn_stub_obtaindomainnameserverlist:
    move.l  a6,-(sp)
    jsr     _tn_unimpl_obtaindomainnameserverlist
    addq.l  #4,sp
    move.l  d0,a0
    rts

| -540: setnetent(LONG stay_open)
    .globl _tn_stub_setnetent
_tn_stub_setnetent:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_setnetent
    addq.l  #8,sp
    rts

| -546: endnetent()
    .globl _tn_stub_endnetent
_tn_stub_endnetent:
    move.l  a6,-(sp)
    jsr     _tn_lvo_endnetent
    addq.l  #4,sp
    rts

| -552: getnetent()
    .globl _tn_stub_getnetent
_tn_stub_getnetent:
    move.l  a6,-(sp)
    jsr     _tn_lvo_getnetent
    addq.l  #4,sp
    move.l  d0,a0
    rts

| -558: setprotoent(LONG stay_open)
    .globl _tn_stub_setprotoent
_tn_stub_setprotoent:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_setprotoent
    addq.l  #8,sp
    rts

| -564: endprotoent()
    .globl _tn_stub_endprotoent
_tn_stub_endprotoent:
    move.l  a6,-(sp)
    jsr     _tn_lvo_endprotoent
    addq.l  #4,sp
    rts

| -570: getprotoent()
    .globl _tn_stub_getprotoent
_tn_stub_getprotoent:
    move.l  a6,-(sp)
    jsr     _tn_lvo_getprotoent
    addq.l  #4,sp
    move.l  d0,a0
    rts

| -576: setservent(LONG stay_open)
    .globl _tn_stub_setservent
_tn_stub_setservent:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_setservent
    addq.l  #8,sp
    rts

| -582: endservent()
    .globl _tn_stub_endservent
_tn_stub_endservent:
    move.l  a6,-(sp)
    jsr     _tn_lvo_endservent
    addq.l  #4,sp
    rts

| -588: getservent()
    .globl _tn_stub_getservent
_tn_stub_getservent:
    move.l  a6,-(sp)
    jsr     _tn_lvo_getservent
    addq.l  #4,sp
    move.l  d0,a0
    rts

| -594: inet_aton(STRPTR cp,struct in_addr *addr)
    .globl _tn_stub_inet_aton
_tn_stub_inet_aton:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_inet_aton
    lea     12(sp),sp
    rts

| -600: inet_ntop(LONG af,APTR src,STRPTR dst,LONG size)
    .globl _tn_stub_inet_ntop
_tn_stub_inet_ntop:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_inet_ntop
    lea     20(sp),sp
    move.l  d0,a0
    rts

| -606: inet_pton(LONG af,STRPTR src,APTR dst)
    .globl _tn_stub_inet_pton
_tn_stub_inet_pton:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_inet_pton
    lea     16(sp),sp
    rts

| -612: In_LocalAddr(in_addr_t address)
    .globl _tn_stub_in_localaddr
_tn_stub_in_localaddr:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_in_localaddr
    addq.l  #8,sp
    rts

| -618: In_CanForward(in_addr_t address)
    .globl _tn_stub_in_canforward
_tn_stub_in_canforward:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_in_canforward
    addq.l  #8,sp
    rts

| -624: mbuf_copym(struct mbuf *m, LONG off, LONG len)
    .globl _tn_stub_mbuf_copym
_tn_stub_mbuf_copym:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_mbuf_copym
    lea     16(sp),sp
    move.l  d0,a0
    rts

| -630: mbuf_copyback(struct mbuf *m, LONG off, LONG len, APTR cp)
    .globl _tn_stub_mbuf_copyback
_tn_stub_mbuf_copyback:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_mbuf_copyback
    lea     20(sp),sp
    rts

| -636: mbuf_copydata(struct mbuf *m, LONG off, LONG len, APTR cp)
    .globl _tn_stub_mbuf_copydata
_tn_stub_mbuf_copydata:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_mbuf_copydata
    lea     20(sp),sp
    rts

| -642: mbuf_free(struct mbuf *m)
    .globl _tn_stub_mbuf_free
_tn_stub_mbuf_free:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_mbuf_free
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -648: mbuf_freem(struct mbuf *m)
    .globl _tn_stub_mbuf_freem
_tn_stub_mbuf_freem:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_mbuf_freem
    addq.l  #8,sp
    rts

| -654: mbuf_get()
    .globl _tn_stub_mbuf_get
_tn_stub_mbuf_get:
    move.l  a6,-(sp)
    jsr     _tn_unimpl_mbuf_get
    addq.l  #4,sp
    move.l  d0,a0
    rts

| -660: mbuf_gethdr()
    .globl _tn_stub_mbuf_gethdr
_tn_stub_mbuf_gethdr:
    move.l  a6,-(sp)
    jsr     _tn_unimpl_mbuf_gethdr
    addq.l  #4,sp
    move.l  d0,a0
    rts

| -666: mbuf_prepend(struct mbuf *m, LONG len)
    .globl _tn_stub_mbuf_prepend
_tn_stub_mbuf_prepend:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_mbuf_prepend
    lea     12(sp),sp
    move.l  d0,a0
    rts

| -672: mbuf_cat(struct mbuf *m, struct mbuf *n)
    .globl _tn_stub_mbuf_cat
_tn_stub_mbuf_cat:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_mbuf_cat
    lea     12(sp),sp
    rts

| -678: mbuf_adj(struct mbuf *mp, LONG req_len)
    .globl _tn_stub_mbuf_adj
_tn_stub_mbuf_adj:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_mbuf_adj
    lea     12(sp),sp
    rts

| -684: mbuf_pullup(struct mbuf *m, LONG len)
    .globl _tn_stub_mbuf_pullup
_tn_stub_mbuf_pullup:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_mbuf_pullup
    lea     12(sp),sp
    move.l  d0,a0
    rts

| -690: ProcessIsServer(struct Process * pr)
    .globl _tn_stub_processisserver
_tn_stub_processisserver:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_processisserver
    addq.l  #8,sp
    rts

| -696: ObtainServerSocket()
    .globl _tn_stub_obtainserversocket
_tn_stub_obtainserversocket:
    move.l  a6,-(sp)
    jsr     _tn_lvo_obtainserversocket
    addq.l  #4,sp
    rts

| -702: GetDefaultDomainName(STRPTR buffer,LONG buffer_size)
    .globl _tn_stub_getdefaultdomainname
_tn_stub_getdefaultdomainname:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_getdefaultdomainname
    lea     12(sp),sp
    rts

| -708: SetDefaultDomainName(STRPTR buffer)
    .globl _tn_stub_setdefaultdomainname
_tn_stub_setdefaultdomainname:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_setdefaultdomainname
    addq.l  #8,sp
    rts

| -714: ObtainRoadshowData(LONG access)
    .globl _tn_stub_obtainroadshowdata
_tn_stub_obtainroadshowdata:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_obtainroadshowdata
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -720: ReleaseRoadshowData(struct List *list)
    .globl _tn_stub_releaseroadshowdata
_tn_stub_releaseroadshowdata:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_releaseroadshowdata
    addq.l  #8,sp
    rts

| -726: ChangeRoadshowData(struct List *list,STRPTR name,ULONG length,APTR data)
    .globl _tn_stub_changeroadshowdata
_tn_stub_changeroadshowdata:
    move.l  a6,-(sp)
    move.l  a2,-(sp)
    move.l  d0,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_changeroadshowdata
    lea     20(sp),sp
    rts

| -732: RemoveInterface(STRPTR interface_name, LONG force)
    .globl _tn_stub_removeinterface
_tn_stub_removeinterface:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_removeinterface
    lea     12(sp),sp
    rts

| -738: gethostbyname_r(STRPTR name, struct hostent * hp, APTR buf, ULONG buflen, LONG * he)
    .globl _tn_stub_gethostbyname_r
_tn_stub_gethostbyname_r:
    move.l  a6,-(sp)
    move.l  a3,-(sp)
    move.l  d0,-(sp)
    move.l  a2,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_gethostbyname_r
    lea     24(sp),sp
    move.l  d0,a0
    rts

| -744: gethostbyaddr_r(STRPTR addr, LONG len, LONG type, struct hostent * hp, APTR buf, ULONG buflen, LONG * he)
    .globl _tn_stub_gethostbyaddr_r
_tn_stub_gethostbyaddr_r:
    move.l  a6,-(sp)
    move.l  a3,-(sp)
    move.l  d2,-(sp)
    move.l  a2,-(sp)
    move.l  a1,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_gethostbyaddr_r
    lea     32(sp),sp
    move.l  d0,a0
    rts

| -762: ipf_open(LONG channel)
    .globl _tn_stub_ipf_open
_tn_stub_ipf_open:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_ipf_open
    addq.l  #8,sp
    rts

| -768: ipf_close(LONG channel)
    .globl _tn_stub_ipf_close
_tn_stub_ipf_close:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_ipf_close
    addq.l  #8,sp
    rts

| -774: ipf_ioctl(LONG channel,ULONG command,APTR buffer)
    .globl _tn_stub_ipf_ioctl
_tn_stub_ipf_ioctl:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_ipf_ioctl
    lea     16(sp),sp
    rts

| -780: ipf_log_read(LONG channel,APTR buffer,LONG len)
    .globl _tn_stub_ipf_log_read
_tn_stub_ipf_log_read:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_ipf_log_read
    lea     16(sp),sp
    rts

| -786: ipf_log_data_waiting(LONG channel)
    .globl _tn_stub_ipf_log_data_waiting
_tn_stub_ipf_log_data_waiting:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_ipf_log_data_waiting
    addq.l  #8,sp
    rts

| -792: ipf_set_notify_mask(LONG channel,ULONG mask)
    .globl _tn_stub_ipf_set_notify_mask
_tn_stub_ipf_set_notify_mask:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_ipf_set_notify_mask
    lea     12(sp),sp
    rts

| -798: ipf_set_interrupt_mask(LONG channel,ULONG mask)
    .globl _tn_stub_ipf_set_interrupt_mask
_tn_stub_ipf_set_interrupt_mask:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_unimpl_ipf_set_interrupt_mask
    lea     12(sp),sp
    rts

| -804: freeaddrinfo(struct addrinfo *ai)
    .globl _tn_stub_freeaddrinfo
_tn_stub_freeaddrinfo:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_freeaddrinfo
    addq.l  #8,sp
    rts

| -810: getaddrinfo(STRPTR hostname, STRPTR servname, struct addrinfo *hints, struct addrinfo **res)
    .globl _tn_stub_getaddrinfo
_tn_stub_getaddrinfo:
    move.l  a6,-(sp)
    move.l  a3,-(sp)
    move.l  a2,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_getaddrinfo
    lea     20(sp),sp
    rts

| -816: gai_strerror(LONG errnum)
    .globl _tn_stub_gai_strerror
_tn_stub_gai_strerror:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_gai_strerror
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -822: getnameinfo(struct sockaddr *sa, ULONG salen, STRPTR host, ULONG hostlen, STRPTR serv, ULONG servlen, ULONG flags)
    .globl _tn_stub_getnameinfo
_tn_stub_getnameinfo:
    move.l  a6,-(sp)
    move.l  d3,-(sp)
    move.l  d2,-(sp)
    move.l  a2,-(sp)
    move.l  d1,-(sp)
    move.l  a1,-(sp)
    move.l  d0,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_unimpl_getnameinfo
    lea     32(sp),sp
    rts
