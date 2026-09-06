/*
 * lib_table.gen.c — Generated bsdsocket.library jump table.
 * Generated automatically by scripts/gen_lvo_table.py from sfd/bsdsocket_lib.sfd.
 * DO NOT EDIT MANUALLY.
 */

#include <exec/types.h>

/* Forward declarations for 68k assembly dispatch stubs */
extern void tn_stub_open(void);
extern void tn_stub_close(void);
extern void tn_stub_expunge(void);
extern void tn_stub_reserved(void);
extern void tn_stub_neg1(void);

extern void tn_stub_socket(void);
extern void tn_stub_bind(void);
extern void tn_stub_listen(void);
extern void tn_stub_accept(void);
extern void tn_stub_connect(void);
extern void tn_stub_sendto(void);
extern void tn_stub_send(void);
extern void tn_stub_recvfrom(void);
extern void tn_stub_recv(void);
extern void tn_stub_shutdown(void);
extern void tn_stub_setsockopt(void);
extern void tn_stub_getsockopt(void);
extern void tn_stub_getsockname(void);
extern void tn_stub_getpeername(void);
extern void tn_stub_ioctlsocket(void);
extern void tn_stub_closesocket(void);
extern void tn_stub_waitselect(void);
extern void tn_stub_setsocketsignals(void);
extern void tn_stub_getdtablesize(void);
extern void tn_stub_obtainsocket(void);
extern void tn_stub_releasesocket(void);
extern void tn_stub_releasecopyofsocket(void);
extern void tn_stub_errno(void);
extern void tn_stub_seterrnoptr(void);
extern void tn_stub_inet_ntoa(void);
extern void tn_stub_inet_addr(void);
extern void tn_stub_inet_lnaof(void);
extern void tn_stub_inet_netof(void);
extern void tn_stub_inet_makeaddr(void);
extern void tn_stub_inet_network(void);
extern void tn_stub_gethostbyname(void);
extern void tn_stub_gethostbyaddr(void);
extern void tn_stub_getnetbyname(void);
extern void tn_stub_getnetbyaddr(void);
extern void tn_stub_getservbyname(void);
extern void tn_stub_getservbyport(void);
extern void tn_stub_getprotobyname(void);
extern void tn_stub_getprotobynumber(void);
extern void tn_stub_vsyslog(void);
extern void tn_stub_dup2socket(void);
extern void tn_stub_sendmsg(void);
extern void tn_stub_recvmsg(void);
extern void tn_stub_gethostname(void);
extern void tn_stub_gethostid(void);
extern void tn_stub_socketbasetaglist(void);
extern void tn_stub_getsocketevents(void);
extern void tn_stub_bpf_open(void);
extern void tn_stub_bpf_close(void);
extern void tn_stub_bpf_read(void);
extern void tn_stub_bpf_write(void);
extern void tn_stub_bpf_set_notify_mask(void);
extern void tn_stub_bpf_set_interrupt_mask(void);
extern void tn_stub_bpf_ioctl(void);
extern void tn_stub_bpf_data_waiting(void);
extern void tn_stub_addroutetaglist(void);
extern void tn_stub_deleteroutetaglist(void);
extern void tn_stub_changeroutetaglist(void);
extern void tn_stub_freerouteinfo(void);
extern void tn_stub_getrouteinfo(void);
extern void tn_stub_addinterfacetaglist(void);
extern void tn_stub_configureinterfacetaglist(void);
extern void tn_stub_releaseinterfacelist(void);
extern void tn_stub_obtaininterfacelist(void);
extern void tn_stub_queryinterfacetaglist(void);
extern void tn_stub_createaddrallocmessagea(void);
extern void tn_stub_deleteaddrallocmessage(void);
extern void tn_stub_begininterfaceconfig(void);
extern void tn_stub_abortinterfaceconfig(void);
extern void tn_stub_addnetmonitorhooktaglist(void);
extern void tn_stub_removenetmonitorhook(void);
extern void tn_stub_getnetworkstatistics(void);
extern void tn_stub_adddomainnameserver(void);
extern void tn_stub_removedomainnameserver(void);
extern void tn_stub_releasedomainnameserverlist(void);
extern void tn_stub_obtaindomainnameserverlist(void);
extern void tn_stub_setnetent(void);
extern void tn_stub_endnetent(void);
extern void tn_stub_getnetent(void);
extern void tn_stub_setprotoent(void);
extern void tn_stub_endprotoent(void);
extern void tn_stub_getprotoent(void);
extern void tn_stub_setservent(void);
extern void tn_stub_endservent(void);
extern void tn_stub_getservent(void);
extern void tn_stub_inet_aton(void);
extern void tn_stub_inet_ntop(void);
extern void tn_stub_inet_pton(void);
extern void tn_stub_in_localaddr(void);
extern void tn_stub_in_canforward(void);
extern void tn_stub_mbuf_copym(void);
extern void tn_stub_mbuf_copyback(void);
extern void tn_stub_mbuf_copydata(void);
extern void tn_stub_mbuf_free(void);
extern void tn_stub_mbuf_freem(void);
extern void tn_stub_mbuf_get(void);
extern void tn_stub_mbuf_gethdr(void);
extern void tn_stub_mbuf_prepend(void);
extern void tn_stub_mbuf_cat(void);
extern void tn_stub_mbuf_adj(void);
extern void tn_stub_mbuf_pullup(void);
extern void tn_stub_processisserver(void);
extern void tn_stub_obtainserversocket(void);
extern void tn_stub_getdefaultdomainname(void);
extern void tn_stub_setdefaultdomainname(void);
extern void tn_stub_obtainroadshowdata(void);
extern void tn_stub_releaseroadshowdata(void);
extern void tn_stub_changeroadshowdata(void);
extern void tn_stub_removeinterface(void);
extern void tn_stub_gethostbyname_r(void);
extern void tn_stub_gethostbyaddr_r(void);
extern void tn_stub_ipf_open(void);
extern void tn_stub_ipf_close(void);
extern void tn_stub_ipf_ioctl(void);
extern void tn_stub_ipf_log_read(void);
extern void tn_stub_ipf_log_data_waiting(void);
extern void tn_stub_ipf_set_notify_mask(void);
extern void tn_stub_ipf_set_interrupt_mask(void);
extern void tn_stub_freeaddrinfo(void);
extern void tn_stub_getaddrinfo(void);
extern void tn_stub_gai_strerror(void);
extern void tn_stub_getnameinfo(void);

const APTR g_lib_vectors[] = {
    (APTR)tn_stub_open,                  /* -6   LIB_OPEN */
    (APTR)tn_stub_close,                 /* -12  LIB_CLOSE */
    (APTR)tn_stub_expunge,               /* -18  LIB_EXPUNGE */
    (APTR)tn_stub_reserved,              /* -24  LIB_RESERVED */

    (APTR)tn_stub_socket                , /* -30 socket */
    (APTR)tn_stub_bind                  , /* -36 bind */
    (APTR)tn_stub_listen                , /* -42 listen */
    (APTR)tn_stub_accept                , /* -48 accept */
    (APTR)tn_stub_connect               , /* -54 connect */
    (APTR)tn_stub_sendto                , /* -60 sendto */
    (APTR)tn_stub_send                  , /* -66 send */
    (APTR)tn_stub_recvfrom              , /* -72 recvfrom */
    (APTR)tn_stub_recv                  , /* -78 recv */
    (APTR)tn_stub_shutdown              , /* -84 shutdown */
    (APTR)tn_stub_setsockopt            , /* -90 setsockopt */
    (APTR)tn_stub_getsockopt            , /* -96 getsockopt */
    (APTR)tn_stub_getsockname           , /* -102 getsockname */
    (APTR)tn_stub_getpeername           , /* -108 getpeername */
    (APTR)tn_stub_ioctlsocket           , /* -114 IoctlSocket */
    (APTR)tn_stub_closesocket           , /* -120 CloseSocket */
    (APTR)tn_stub_waitselect            , /* -126 WaitSelect */
    (APTR)tn_stub_setsocketsignals      , /* -132 SetSocketSignals */
    (APTR)tn_stub_getdtablesize         , /* -138 getdtablesize */
    (APTR)tn_stub_obtainsocket          , /* -144 ObtainSocket */
    (APTR)tn_stub_releasesocket         , /* -150 ReleaseSocket */
    (APTR)tn_stub_releasecopyofsocket   , /* -156 ReleaseCopyOfSocket */
    (APTR)tn_stub_errno                 , /* -162 Errno */
    (APTR)tn_stub_seterrnoptr           , /* -168 SetErrnoPtr */
    (APTR)tn_stub_inet_ntoa             , /* -174 Inet_NtoA */
    (APTR)tn_stub_inet_addr             , /* -180 inet_addr */
    (APTR)tn_stub_inet_lnaof            , /* -186 Inet_LnaOf */
    (APTR)tn_stub_inet_netof            , /* -192 Inet_NetOf */
    (APTR)tn_stub_inet_makeaddr         , /* -198 Inet_MakeAddr */
    (APTR)tn_stub_inet_network          , /* -204 inet_network */
    (APTR)tn_stub_gethostbyname         , /* -210 gethostbyname */
    (APTR)tn_stub_gethostbyaddr         , /* -216 gethostbyaddr */
    (APTR)tn_stub_getnetbyname          , /* -222 getnetbyname */
    (APTR)tn_stub_getnetbyaddr          , /* -228 getnetbyaddr */
    (APTR)tn_stub_getservbyname         , /* -234 getservbyname */
    (APTR)tn_stub_getservbyport         , /* -240 getservbyport */
    (APTR)tn_stub_getprotobyname        , /* -246 getprotobyname */
    (APTR)tn_stub_getprotobynumber      , /* -252 getprotobynumber */
    (APTR)tn_stub_vsyslog               , /* -258 vsyslog */
    (APTR)tn_stub_dup2socket            , /* -264 Dup2Socket */
    (APTR)tn_stub_sendmsg               , /* -270 sendmsg */
    (APTR)tn_stub_recvmsg               , /* -276 recvmsg */
    (APTR)tn_stub_gethostname           , /* -282 gethostname */
    (APTR)tn_stub_gethostid             , /* -288 gethostid */
    (APTR)tn_stub_socketbasetaglist     , /* -294 SocketBaseTagList */
    (APTR)tn_stub_getsocketevents       , /* -300 GetSocketEvents */
    (APTR)tn_stub_neg1                  , /* -306 RESERVED */
    (APTR)tn_stub_neg1                  , /* -312 RESERVED */
    (APTR)tn_stub_neg1                  , /* -318 RESERVED */
    (APTR)tn_stub_neg1                  , /* -324 RESERVED */
    (APTR)tn_stub_neg1                  , /* -330 RESERVED */
    (APTR)tn_stub_neg1                  , /* -336 RESERVED */
    (APTR)tn_stub_neg1                  , /* -342 RESERVED */
    (APTR)tn_stub_neg1                  , /* -348 RESERVED */
    (APTR)tn_stub_neg1                  , /* -354 RESERVED */
    (APTR)tn_stub_neg1                  , /* -360 RESERVED */
    (APTR)tn_stub_bpf_open              , /* -366 bpf_open */
    (APTR)tn_stub_bpf_close             , /* -372 bpf_close */
    (APTR)tn_stub_bpf_read              , /* -378 bpf_read */
    (APTR)tn_stub_bpf_write             , /* -384 bpf_write */
    (APTR)tn_stub_bpf_set_notify_mask   , /* -390 bpf_set_notify_mask */
    (APTR)tn_stub_bpf_set_interrupt_mask, /* -396 bpf_set_interrupt_mask */
    (APTR)tn_stub_bpf_ioctl             , /* -402 bpf_ioctl */
    (APTR)tn_stub_bpf_data_waiting      , /* -408 bpf_data_waiting */
    (APTR)tn_stub_addroutetaglist       , /* -414 AddRouteTagList */
    (APTR)tn_stub_deleteroutetaglist    , /* -420 DeleteRouteTagList */
    (APTR)tn_stub_changeroutetaglist    , /* -426 ChangeRouteTagList */
    (APTR)tn_stub_freerouteinfo         , /* -432 FreeRouteInfo */
    (APTR)tn_stub_getrouteinfo          , /* -438 GetRouteInfo */
    (APTR)tn_stub_addinterfacetaglist   , /* -444 AddInterfaceTagList */
    (APTR)tn_stub_configureinterfacetaglist, /* -450 ConfigureInterfaceTagList */
    (APTR)tn_stub_releaseinterfacelist  , /* -456 ReleaseInterfaceList */
    (APTR)tn_stub_obtaininterfacelist   , /* -462 ObtainInterfaceList */
    (APTR)tn_stub_queryinterfacetaglist , /* -468 QueryInterfaceTagList */
    (APTR)tn_stub_createaddrallocmessagea, /* -474 CreateAddrAllocMessageA */
    (APTR)tn_stub_deleteaddrallocmessage, /* -480 DeleteAddrAllocMessage */
    (APTR)tn_stub_begininterfaceconfig  , /* -486 BeginInterfaceConfig */
    (APTR)tn_stub_abortinterfaceconfig  , /* -492 AbortInterfaceConfig */
    (APTR)tn_stub_addnetmonitorhooktaglist, /* -498 AddNetMonitorHookTagList */
    (APTR)tn_stub_removenetmonitorhook  , /* -504 RemoveNetMonitorHook */
    (APTR)tn_stub_getnetworkstatistics  , /* -510 GetNetworkStatistics */
    (APTR)tn_stub_adddomainnameserver   , /* -516 AddDomainNameServer */
    (APTR)tn_stub_removedomainnameserver, /* -522 RemoveDomainNameServer */
    (APTR)tn_stub_releasedomainnameserverlist, /* -528 ReleaseDomainNameServerList */
    (APTR)tn_stub_obtaindomainnameserverlist, /* -534 ObtainDomainNameServerList */
    (APTR)tn_stub_setnetent             , /* -540 setnetent */
    (APTR)tn_stub_endnetent             , /* -546 endnetent */
    (APTR)tn_stub_getnetent             , /* -552 getnetent */
    (APTR)tn_stub_setprotoent           , /* -558 setprotoent */
    (APTR)tn_stub_endprotoent           , /* -564 endprotoent */
    (APTR)tn_stub_getprotoent           , /* -570 getprotoent */
    (APTR)tn_stub_setservent            , /* -576 setservent */
    (APTR)tn_stub_endservent            , /* -582 endservent */
    (APTR)tn_stub_getservent            , /* -588 getservent */
    (APTR)tn_stub_inet_aton             , /* -594 inet_aton */
    (APTR)tn_stub_inet_ntop             , /* -600 inet_ntop */
    (APTR)tn_stub_inet_pton             , /* -606 inet_pton */
    (APTR)tn_stub_in_localaddr          , /* -612 In_LocalAddr */
    (APTR)tn_stub_in_canforward         , /* -618 In_CanForward */
    (APTR)tn_stub_mbuf_copym            , /* -624 mbuf_copym */
    (APTR)tn_stub_mbuf_copyback         , /* -630 mbuf_copyback */
    (APTR)tn_stub_mbuf_copydata         , /* -636 mbuf_copydata */
    (APTR)tn_stub_mbuf_free             , /* -642 mbuf_free */
    (APTR)tn_stub_mbuf_freem            , /* -648 mbuf_freem */
    (APTR)tn_stub_mbuf_get              , /* -654 mbuf_get */
    (APTR)tn_stub_mbuf_gethdr           , /* -660 mbuf_gethdr */
    (APTR)tn_stub_mbuf_prepend          , /* -666 mbuf_prepend */
    (APTR)tn_stub_mbuf_cat              , /* -672 mbuf_cat */
    (APTR)tn_stub_mbuf_adj              , /* -678 mbuf_adj */
    (APTR)tn_stub_mbuf_pullup           , /* -684 mbuf_pullup */
    (APTR)tn_stub_processisserver       , /* -690 ProcessIsServer */
    (APTR)tn_stub_obtainserversocket    , /* -696 ObtainServerSocket */
    (APTR)tn_stub_getdefaultdomainname  , /* -702 GetDefaultDomainName */
    (APTR)tn_stub_setdefaultdomainname  , /* -708 SetDefaultDomainName */
    (APTR)tn_stub_obtainroadshowdata    , /* -714 ObtainRoadshowData */
    (APTR)tn_stub_releaseroadshowdata   , /* -720 ReleaseRoadshowData */
    (APTR)tn_stub_changeroadshowdata    , /* -726 ChangeRoadshowData */
    (APTR)tn_stub_removeinterface       , /* -732 RemoveInterface */
    (APTR)tn_stub_gethostbyname_r       , /* -738 gethostbyname_r */
    (APTR)tn_stub_gethostbyaddr_r       , /* -744 gethostbyaddr_r */
    (APTR)tn_stub_neg1                  , /* -750 RESERVED */
    (APTR)tn_stub_neg1                  , /* -756 RESERVED */
    (APTR)tn_stub_ipf_open              , /* -762 ipf_open */
    (APTR)tn_stub_ipf_close             , /* -768 ipf_close */
    (APTR)tn_stub_ipf_ioctl             , /* -774 ipf_ioctl */
    (APTR)tn_stub_ipf_log_read          , /* -780 ipf_log_read */
    (APTR)tn_stub_ipf_log_data_waiting  , /* -786 ipf_log_data_waiting */
    (APTR)tn_stub_ipf_set_notify_mask   , /* -792 ipf_set_notify_mask */
    (APTR)tn_stub_ipf_set_interrupt_mask, /* -798 ipf_set_interrupt_mask */
    (APTR)tn_stub_freeaddrinfo          , /* -804 freeaddrinfo */
    (APTR)tn_stub_getaddrinfo           , /* -810 getaddrinfo */
    (APTR)tn_stub_gai_strerror          , /* -816 gai_strerror */
    (APTR)tn_stub_getnameinfo           , /* -822 getnameinfo */
    (APTR)tn_stub_neg1                  , /* -828 RESERVED */
    (APTR)tn_stub_neg1                  , /* -834 RESERVED */
    (APTR)tn_stub_neg1                  , /* -840 RESERVED */
    (APTR)tn_stub_neg1                  , /* -846 RESERVED */
    (APTR)tn_stub_neg1                  , /* -852 RESERVED */
    (APTR)tn_stub_neg1                  , /* -858 RESERVED */
    (APTR)-1                             /* End of table marker */
};
