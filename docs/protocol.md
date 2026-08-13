# protocol.md — library ⇄ task wire protocol

> Master prompt §5. The wire format is defined in
> `include/tolunet/protocol.h`. This document describes each request kind's
> **union payload**. Rule (§5): every union field added is documented here in
> the same commit.

## Versioning

`TN_PROTO_VERSION` (currently 1). The task refuses requests whose
`proto_version` differs.

## Envelope (all requests)

```
struct TnRequest {
    struct Message msg;   /* Exec message header */
    uint16 proto_version; /* TN_PROTO_VERSION   */
    uint16 kind;          /* TnReqKind          */
    int32  sock;          /* task-side socket id */
    int32  result;        /* OUT                */
    int32  err;           /* OUT errno (§5.1)   */
    union { ... } u;      /* per-kind, see below */
};
```

Rules (§5):
- The task keeps **no pointers into library memory after ReplyMsg**. Anything
  needed beyond the reply is copied into task-owned storage (`buffers.c`).
- Buffers > 4 KB go through the task-owned copy ring.
- `result` / `err` are valid only after the reply arrives.

## Per-kind payloads

_(none yet — the union is empty in M0.)_

Payloads are added with their milestone. The first will be:

- **M3** — `TN_REQ_SOCKET` (domain/type/proto), `TN_REQ_CONNECT` (sockaddr),
  `TN_REQ_SEND`/`TN_REQ_RECV` (buffer descriptor + length).
- **M4** — `TN_REQ_SELECT_ARM`/`CANCEL` (fd-set + signal mask), `TN_REQ_IOCTL`
  (cmd + argp).
- **M5** — `TN_REQ_RESOLVE` (hostname → addr), `TN_REQ_UDP_SENDTO`/`RECVFROM`.

Each addition lands here with: field names, types, who owns each buffer, and
the `result`/`err` semantics for that kind.

## errno mapping

See master prompt §5.1 and (later) `src/bsdsocket/errno.c`. Values come from
`netinclude/sys/errno.h` (Roadshow SDK, §3.1 item 15) — no literals.
