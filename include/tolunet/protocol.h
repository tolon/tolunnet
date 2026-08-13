/*
 * tolunet — library <-> network task wire protocol.
 *
 * Binding source: TOLUNET-master-prompt.md §5 (Protocol). This header is a
 * verbatim transcription of the struct/enum defined there; do not improvise
 * fields. Every union member added in later milestones MUST be documented in
 * docs/protocol.md in the same commit (§5 rules).
 */
#ifndef TOLUNET_PROTOCOL_H
#define TOLUNET_PROTOCOL_H

#include <exec/types.h>   /* UWORD, ULONG, etc. — NDK 3.2 */
#include <exec/ports.h>   /* struct Message                — NDK 3.2 */

/*
 * Bumped only on an incompatible wire change. The task refuses requests whose
 * proto_version differs.
 */
#define TN_PROTO_VERSION 1

/*
 * Request kinds. The task dispatcher (src/task/dispatch.c, M2+) switches on
 * these. Values are explicit so the wire format is stable regardless of enum
 * ordering.
 */
typedef enum {
    TN_REQ_SOCKET         = 1,
    TN_REQ_CONNECT        = 2,
    TN_REQ_BIND           = 3,
    TN_REQ_LISTEN         = 4,
    TN_REQ_ACCEPT         = 5,
    TN_REQ_SEND           = 6,
    TN_REQ_RECV           = 7,
    TN_REQ_CLOSE          = 8,
    TN_REQ_SELECT_ARM     = 9,
    TN_REQ_SELECT_CANCEL  = 10,
    TN_REQ_IOCTL          = 11,
    TN_REQ_SETOPT         = 12,
    TN_REQ_GETOPT         = 13,
    TN_REQ_RESOLVE        = 14,
    TN_REQ_UDP_SENDTO     = 15,
    TN_REQ_UDP_RECVFROM   = 16,
    TN_REQ_STATUS         = 17,
    TN_REQ_APPLY_CONFIG   = 18,
    TN_REQ_SHUTDOWN       = 19
} TnReqKind;

/*
 * One request = one Exec message. The library fills the fields, PutMsg's the
 * request to the task's public port, and either waits on the reply signal
 * (blocking calls, M3+) or arms a signal (select/async, M4+).
 *
 * Rules carried over from §5:
 *   - The task keeps NO pointers into library memory after ReplyMsg. Anything
 *     the task needs beyond the reply must be copied into task-owned storage
 *     (buffers.c, >4 KB via the task-owned copy ring).
 *   - result/err are OUT fields, valid only after the reply arrives.
 *   - sock is the task-side socket id (not the library fd); the library maps.
 */
typedef struct TnRequest {
    struct Message msg;        /* mn_Node, mn_ReplyPort, mn_Length — Exec */
    UWORD  proto_version;      /* TN_PROTO_VERSION                      */
    UWORD  kind;               /* TnReqKind                             */
    LONG   sock;               /* task-side socket id                   */
    LONG   result;             /* OUT                                   */
    LONG   err;                /* OUT errno (see errno map, §5.1)       */
    /*
     * Per-kind payload. Fields are added ONLY when a milestone needs them, and
     * documented in docs/protocol.md the same commit. Empty for now (M0); M3
     * adds the socket/connect/recv payloads.
     */
    union {
        UBYTE _placeholder[32];   /* reserves headroom; never read */
    } u;
} TnRequest;

#endif /* TOLUNET_PROTOCOL_H */
