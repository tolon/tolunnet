/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Shared command-line client infrastructure (CMD-0).
 *
 * Every CLI tool in src/cmds/ links this + bsdsocket.library only.
 * No lwIP, no daemon internals, no -lsocket (noixemul-incompatible) —
 * all bsdsocket calls go through inline-asm LVO wrappers with SocketBase.
 */
#ifndef TOLUNNET_CMDLIB_H
#define TOLUNNET_CMDLIB_H

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <libraries/bsdsocket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "../../include/ipc.h"

/* Return codes (AmigaDOS convention) */
#define TN_CMD_OK    0
#define TN_CMD_WARN  5
#define TN_CMD_FAIL  10
#define TN_CMD_USAGE 20

#ifndef INADDR_NONE
#define INADDR_NONE 0xFFFFFFFFu
#endif

/* Global SocketBase (set by tn_cmd_init) */
extern struct Library *SocketBase;

/* --- bsdsocket LVO wrappers (inline asm, -noixemul compatible) --- */
LONG tn_call_socket(LONG d, LONG t, LONG p);
LONG tn_call_connect(LONG fd, const struct sockaddr *a, LONG len);
LONG tn_call_send(LONG fd, const void *buf, LONG len, LONG flags);
LONG tn_call_recv(LONG fd, void *buf, LONG len, LONG flags);
LONG tn_call_sendto(LONG fd, const void *buf, LONG len, LONG flags,
                    const struct sockaddr *to, LONG tolen);
LONG tn_call_recvfrom(LONG fd, void *buf, LONG len, LONG flags,
                      struct sockaddr *from, LONG *fromlen);

/* --- z.ai step 9a item 1: live daemon state over IPC only --- */
#define TN_SNAP_MAX_IFS 8
#define TN_SNAP_MAX_ROUTES 16
typedef struct TnSnapshot {
    TnStatusInfoV2 status;                  /* GETSTATUS v2 */
    TnIfInfo ifs[TN_SNAP_MAX_IFS];          /* IFCTL LIST */
    LONG if_count;                          /* -1 if the call failed */
    TnRouteInfo routes[TN_SNAP_MAX_ROUTES]; /* ROUTECTL LIST */
    LONG route_count;                       /* -1 if the call failed */
    LONG socket_count;                      /* status.active_sockets */
} TnSnapshot;

/* Fill snap from the daemon over the IPC port; opens no library.
 * Returns 0 when the daemon answered, -1 when it is not running. */
int tn_cmd_snapshot(TnSnapshot *snap);

/* Format a big-endian ULONG IPv4 as a.b.c.d into buf (>= 16 bytes). */
void tn_cmd_ip_to_str(ULONG ip, char *buf);

LONG tn_call_closesocket(LONG fd);
LONG tn_call_gethostname(STRPTR name, LONG len);
LONG tn_call_errno(void);
ULONG tn_call_inet_addr(const char *cp);
STRPTR tn_call_inet_ntoa(struct in_addr in);
struct hostent *tn_call_gethostbyname(const char *name);
struct hostent *tn_call_gethostbyaddr(const char *addr, LONG len, LONG type);
LONG tn_call_waitselect(LONG nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv, ULONG *sigmask);
LONG tn_call_ioctl(LONG fd, ULONG req, APTR argp);
LONG tn_call_bind(LONG fd, const struct sockaddr *a, LONG len);
LONG tn_call_listen(LONG fd, LONG backlog);
LONG tn_call_accept(LONG fd, struct sockaddr *a, LONG *len);
LONG tn_call_shutdown(LONG fd, LONG how);
LONG tn_call_setsockopt(LONG fd, LONG level, LONG optname, const void *optval, LONG optlen);
LONG tn_call_getsockopt(LONG fd, LONG level, LONG optname, void *optval, LONG *optlen);

/* --- Helpers --- */
int  tn_cmd_init(void);
void tn_cmd_fini(void);
void tn_cmd_printf(const char *fmt, ...);
BOOL tn_cmd_check_ctrlc(void);
ULONG tn_cmd_resolve(const char *host);

/* z.ai step 9a item 3: strict dotted-quad parser - a.b.c.d with each
 * part 0-255 and nothing trailing; unlike inet_addr it accepts
 * 255.255.255.255. Stores the address big-endian in *out. */
BOOL tn_parse_ipv4(const char *s, ULONG *out);

#endif /* TOLUNNET_CMDLIB_H */
