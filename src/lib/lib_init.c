/*
 * tolunnet — bsdsocket.library constructor and registration.
 */

#include "lib_init.h"
#include "../../include/ipc.h"
#include "../common/log.h"

#include <stdint.h>
#include <proto/exec.h>
#include <exec/libraries.h>
#include <exec/initializers.h>

/* Forward declarations for 68k assembly dispatch stubs */
extern void tn_stub_open(void);
extern void tn_stub_close(void);
extern void tn_stub_expunge(void);
extern void tn_stub_reserved(void);

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
extern void tn_stub_errno(void);
extern void tn_stub_seterrnoptr(void);
extern void tn_stub_inet_ntoa(void);
extern void tn_stub_inet_addr(void);
extern void tn_stub_inet_lnaof(void);
extern void tn_stub_inet_netof(void);
extern void tn_stub_inet_makeaddr(void);
extern void tn_stub_inet_network(void);
extern void tn_stub_gethostbyname(void);
extern void tn_stub_getservbyname(void);
extern void tn_stub_getservbyport(void);
extern void tn_stub_getprotobyname(void);
extern void tn_stub_getprotobynumber(void);
extern void tn_stub_dup2socket(void);
extern void tn_stub_gethostname(void);
extern void tn_stub_gethostid(void);
extern void tn_stub_socketbasetaglist(void);

extern void tn_stub_neg1(void);
extern void tn_stub_null(void);
extern void tn_stub_void(void);

/* Function pointer table for MakeLibrary (order is -6, -12, -18, -24, -30, ...) */
static const APTR g_lib_vectors[] = {
    (APTR)tn_stub_open,                  /* -6   LIB_OPEN */
    (APTR)tn_stub_close,                 /* -12  LIB_CLOSE */
    (APTR)tn_stub_expunge,               /* -18  LIB_EXPUNGE */
    (APTR)tn_stub_reserved,              /* -24  LIB_RESERVED */

    (APTR)tn_stub_socket,                /* -30  socket */
    (APTR)tn_stub_bind,                  /* -36  bind */
    (APTR)tn_stub_listen,                /* -42  listen */
    (APTR)tn_stub_accept,                /* -48  accept */
    (APTR)tn_stub_connect,               /* -54  connect */
    (APTR)tn_stub_sendto,                /* -60  sendto */
    (APTR)tn_stub_send,                  /* -66  send */
    (APTR)tn_stub_recvfrom,              /* -72  recvfrom */
    (APTR)tn_stub_recv,                  /* -78  recv */
    (APTR)tn_stub_shutdown,              /* -84  shutdown */
    (APTR)tn_stub_setsockopt,            /* -90  setsockopt */
    (APTR)tn_stub_getsockopt,            /* -96  getsockopt */
    (APTR)tn_stub_getsockname,           /* -102 getsockname */
    (APTR)tn_stub_getpeername,           /* -108 getpeername */
    (APTR)tn_stub_ioctlsocket,           /* -114 IoctlSocket */
    (APTR)tn_stub_closesocket,           /* -120 CloseSocket */
    (APTR)tn_stub_waitselect,            /* -126 WaitSelect */
    (APTR)tn_stub_setsocketsignals,      /* -132 SetSocketSignals */
    (APTR)tn_stub_getdtablesize,         /* -138 getdtablesize */
    (APTR)tn_stub_neg1,                  /* -144 ObtainSocket */
    (APTR)tn_stub_neg1,                  /* -150 ReleaseSocket */
    (APTR)tn_stub_neg1,                  /* -156 ReleaseCopyOfSocket */
    (APTR)tn_stub_errno,                 /* -162 Errno */
    (APTR)tn_stub_seterrnoptr,           /* -168 SetErrnoPtr */
    (APTR)tn_stub_inet_ntoa,             /* -174 Inet_NtoA */
    (APTR)tn_stub_inet_addr,             /* -180 inet_addr */
    (APTR)tn_stub_inet_lnaof,            /* -186 Inet_LnaOf */
    (APTR)tn_stub_inet_netof,            /* -192 Inet_NetOf */
    (APTR)tn_stub_inet_makeaddr,         /* -198 Inet_MakeAddr */
    (APTR)tn_stub_inet_network,          /* -204 inet_network */
    (APTR)tn_stub_gethostbyname,         /* -210 gethostbyname */
    (APTR)tn_stub_null,                  /* -216 gethostbyaddr */
    (APTR)tn_stub_null,                  /* -222 getnetbyname */
    (APTR)tn_stub_null,                  /* -228 getnetbyaddr */
    (APTR)tn_stub_getservbyname,         /* -234 getservbyname */
    (APTR)tn_stub_getservbyport,         /* -240 getservbyport */
    (APTR)tn_stub_getprotobyname,        /* -246 getprotobyname */
    (APTR)tn_stub_getprotobynumber,      /* -252 getprotobynumber */
    (APTR)tn_stub_void,                  /* -258 vsyslog */
    (APTR)tn_stub_dup2socket,            /* -264 Dup2Socket */
    (APTR)tn_stub_neg1,                  /* -270 sendmsg */
    (APTR)tn_stub_neg1,                  /* -276 recvmsg */
    (APTR)tn_stub_gethostname,           /* -282 gethostname */
    (APTR)tn_stub_gethostid,             /* -288 gethostid */
    (APTR)tn_stub_socketbasetaglist,     /* -294 SocketBaseTagList */
    (APTR)tn_stub_neg1,                  /* -300 GetSocketEvents */
    (APTR)-1                             /* End of table marker */
};

struct Library *tn_lib_create(void)
{
    struct Library *lib;

    lib = MakeLibrary((APTR)g_lib_vectors, NULL, NULL, sizeof(struct Library), 0UL);
    if (lib == NULL) return NULL;

    /* Initialize Library fields */
    lib->lib_Node.ln_Type = NT_LIBRARY;
    lib->lib_Node.ln_Pri  = 0;
    lib->lib_Node.ln_Name = (STRPTR)BSDSOCKET_NAME;
    lib->lib_Flags        = LIBF_SUMUSED | LIBF_CHANGED;
    lib->lib_Version      = BSDSOCKET_VER;
    lib->lib_Revision     = BSDSOCKET_REV;
    lib->lib_IdString     = (STRPTR)"bsdsocket 4.1 (tolunnet)";

    Forbid();
    AddLibrary(lib);
    Permit();

    tn_logf(TN_LOG_BASIC, "tolunnet: bsdsocket.library v%d.%d registered into Exec LibList\n",
            BSDSOCKET_VER, BSDSOCKET_REV);

    return lib;
}

void tn_lib_destroy(struct Library *lib)
{
    struct ExecBase *SysBase = *(struct ExecBase **)4UL;

    if (lib == NULL) return;

    Forbid();
    if (lib->lib_OpenCnt > 0) {
        tn_logf(TN_LOG_BASIC, "tolunnet: cannot destroy bsdsocket.library (OpenCnt=%d)\n",
                lib->lib_OpenCnt);
        Permit();
        return;
    }

    Remove(&lib->lib_Node);
    Permit();

    FreeVec((UBYTE *)lib - lib->lib_NegSize);
    tn_logf(TN_LOG_BASIC, "tolunnet: bsdsocket.library removed and destroyed\n");
}
