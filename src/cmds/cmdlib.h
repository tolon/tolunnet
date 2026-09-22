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

#endif /* TOLUNNET_CMDLIB_H */
