|
| tolunnet — bsdsocket.library 68k LVO Assembly Dispatch Stubs
|
| Bridges standard AmigaOS register calling convention (a6, d0, d1, d2, a0, a1)
| to C implementation functions.
|

    .text
    .even

| --- Library Management Vectors ---

    .globl _tn_stub_open
_tn_stub_open:
    move.l  d0,-(sp)        | arg2: version
    move.l  a6,-(sp)        | arg1: LibraryBase
    jsr     _tn_lib_open
    addq.l  #8,sp
    move.l  d0,a0           | AmigaOS ABI returns pointer in both d0 and a0
    rts

    .globl _tn_stub_close
_tn_stub_close:
    move.l  a6,-(sp)        | arg1: LibraryBase / SocketBase
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

| --- BSD Socket LVO Vectors ---

| -30: socket(domain:d0, type:d1, protocol:d2, base:a6)
    .globl _tn_stub_socket
_tn_stub_socket:
    move.l  a6,-(sp)
    move.l  d2,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_socket
    lea     16(sp),sp
    rts

| -36: bind(sock:d0, name:a0, namelen:d1, base:a6)
    .globl _tn_stub_bind
_tn_stub_bind:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_bind
    lea     16(sp),sp
    rts

| -42: listen(sock:d0, backlog:d1, base:a6)
    .globl _tn_stub_listen
_tn_stub_listen:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_listen
    lea     12(sp),sp
    rts

| -48: accept(sock:d0, addr:a0, addrlen:a1, base:a6)
    .globl _tn_stub_accept
_tn_stub_accept:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_accept
    lea     16(sp),sp
    rts

| -54: connect(sock:d0, name:a0, namelen:d1, base:a6)
    .globl _tn_stub_connect
_tn_stub_connect:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_connect
    lea     16(sp),sp
    rts

| -60: sendto(sock:d0, buf:a0, len:d1, flags:d2, to:a1, tolen:d3, base:a6)
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

| -66: send(sock:d0, buf:a0, len:d1, flags:d2, base:a6)
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

| -72: recvfrom(sock:d0, buf:a0, len:d1, flags:d2, addr:a1, addrlen:a2, base:a6)
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

| -78: recv(sock:d0, buf:a0, len:d1, flags:d2, base:a6)
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

| -84: shutdown(sock:d0, how:d1, base:a6)
    .globl _tn_stub_shutdown
_tn_stub_shutdown:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_shutdown
    lea     12(sp),sp
    rts

| -90: setsockopt(sock:d0, level:d1, optname:d2, optval:a0, optlen:d3, base:a6)
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

| -96: getsockopt(sock:d0, level:d1, optname:d2, optval:a0, optlen:a1, base:a6)
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

| -102: getsockname(sock:d0, name:a0, namelen:a1, base:a6)
    .globl _tn_stub_getsockname
_tn_stub_getsockname:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_getsockname
    lea     16(sp),sp
    rts

| -108: getpeername(sock:d0, name:a0, namelen:a1, base:a6)
    .globl _tn_stub_getpeername
_tn_stub_getpeername:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_getpeername
    lea     16(sp),sp
    rts

| -114: IoctlSocket(sock:d0, req:d1, argp:a0, base:a6)
    .globl _tn_stub_ioctlsocket
_tn_stub_ioctlsocket:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_ioctlsocket
    lea     16(sp),sp
    rts

| -120: CloseSocket(sock:d0, base:a6)
    .globl _tn_stub_closesocket
_tn_stub_closesocket:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_closesocket
    addq.l  #8,sp
    rts

| -126: WaitSelect(nfds:d0, read_fds:a0, write_fds:a1, except_fds:a2, timeout:a3, signals:d1, base:a6)
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

| -132: SetSocketSignals(int_mask:d0, io_mask:d1, urgent_mask:d2, base:a6)
    .globl _tn_stub_setsocketsignals
_tn_stub_setsocketsignals:
    move.l  a6,-(sp)
    move.l  d2,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_setsocketsignals
    lea     16(sp),sp
    rts

| -138: getdtablesize(base:a6)
    .globl _tn_stub_getdtablesize
_tn_stub_getdtablesize:
    move.l  a6,-(sp)
    jsr     _tn_lvo_getdtablesize
    addq.l  #4,sp
    rts

| -162: Errno(base:a6)
    .globl _tn_stub_errno
_tn_stub_errno:
    move.l  a6,-(sp)
    jsr     _tn_lvo_errno
    addq.l  #4,sp
    rts

| -168: SetErrnoPtr(ptr:a0, size:d0, base:a6)
    .globl _tn_stub_seterrnoptr
_tn_stub_seterrnoptr:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_seterrnoptr
    lea     12(sp),sp
    rts

| -174: Inet_NtoA(ip:d0, base:a6)
    .globl _tn_stub_inet_ntoa
_tn_stub_inet_ntoa:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_inet_ntoa
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -180: inet_addr(cp:a0, base:a6)
    .globl _tn_stub_inet_addr
_tn_stub_inet_addr:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_inet_addr
    addq.l  #8,sp
    rts

| -186: Inet_LnaOf(in:d0, base:a6)
    .globl _tn_stub_inet_lnaof
_tn_stub_inet_lnaof:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_inet_lnaof
    addq.l  #8,sp
    rts

| -192: Inet_NetOf(in:d0, base:a6)
    .globl _tn_stub_inet_netof
_tn_stub_inet_netof:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_inet_netof
    addq.l  #8,sp
    rts

| -198: Inet_MakeAddr(net:d0, host:d1, base:a6)
    .globl _tn_stub_inet_makeaddr
_tn_stub_inet_makeaddr:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_inet_makeaddr
    lea     12(sp),sp
    rts

| -204: inet_network(cp:a0, base:a6)
    .globl _tn_stub_inet_network
_tn_stub_inet_network:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_inet_network
    addq.l  #8,sp
    rts

| -210: gethostbyname(name:a0, base:a6)
    .globl _tn_stub_gethostbyname
_tn_stub_gethostbyname:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_gethostbyname
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -234: getservbyname(name:a0, proto:a1, base:a6)
    .globl _tn_stub_getservbyname
_tn_stub_getservbyname:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_getservbyname
    lea     12(sp),sp
    move.l  d0,a0
    rts

| -240: getservbyport(port:d0, proto:a0, base:a6)
    .globl _tn_stub_getservbyport
_tn_stub_getservbyport:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_getservbyport
    lea     12(sp),sp
    move.l  d0,a0
    rts

| -246: getprotobyname(name:a0, base:a6)
    .globl _tn_stub_getprotobyname
_tn_stub_getprotobyname:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_getprotobyname
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -252: getprotobynumber(proto:d0, base:a6)
    .globl _tn_stub_getprotobynumber
_tn_stub_getprotobynumber:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_getprotobynumber
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -264: Dup2Socket(old_sock:d0, new_sock:d1, base:a6)
    .globl _tn_stub_dup2socket
_tn_stub_dup2socket:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _tn_lvo_dup2socket
    lea     12(sp),sp
    rts

| -282: gethostname(name:a0, namelen:d0, base:a6)
    .globl _tn_stub_gethostname
_tn_stub_gethostname:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_gethostname
    lea     12(sp),sp
    rts

| -288: gethostid(base:a6)
    .globl _tn_stub_gethostid
_tn_stub_gethostid:
    move.l  a6,-(sp)
    jsr     _tn_lvo_gethostid
    addq.l  #4,sp
    rts

| -294: SocketBaseTagList(tags:a0, base:a6)
    .globl _tn_stub_socketbasetaglist
_tn_stub_socketbasetaglist:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _tn_lvo_socketbasetaglist
    addq.l  #8,sp
    rts

| Generic default fallback stubs
    .globl _tn_stub_neg1
_tn_stub_neg1:
    moveq   #-1,d0
    rts

    .globl _tn_stub_null
_tn_stub_null:
    suba.l  a0,a0
    moveq   #0,d0
    rts

    .globl _tn_stub_void
_tn_stub_void:
    rts
