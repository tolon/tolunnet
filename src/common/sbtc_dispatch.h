/*
 * tolunnet — portable SocketBaseTagList tag classifier (host-testable).
 *
 * Round 3 §B.1 / Round 4 §C.5: the per-tag logic of SocketBaseTagList
 * extracted from lib_vectors.c so its semantics are unit-tested with host
 * gcc. The dispatcher is PURE: it never dereferences pointers (ti_Data is a
 * 32-bit value on the real API and cannot carry host pointers in tests), it
 * only classifies the tag and resolves plain values; the caller (lib_vectors.c)
 * performs every actual memory access on the Amiga side.
 *
 * SBTC code values below are verbatim from the Roadshow SDK 1.8 header
 * (`include/libraries/bsdsocket.h` in this tree); lib_vectors.c statically
 * asserts they match the SDK header at build time.
 */
#ifndef TOLUNNET_SBTC_DISPATCH_H
#define TOLUNNET_SBTC_DISPATCH_H

#include <stdint.h>

/* Tag layout (SDK bsdsocket.h): bit0 = SET, 0x8000 = REF, code = (tag>>1)&0x3FFF */
#define TN_SBTF_SET 0x0001u
#define TN_SBTF_REF 0x8000u
#define TN_SBTM_CODE(tag) (((tag) >> 1) & 0x3FFFu)

/* SBTC codes handled by this dispatcher (verbatim from Roadshow SDK 1.8) */
enum {
    TN_SBTC_BREAKMASK                   = 1,
    TN_SBTC_SIGIOMASK                   = 2,
    TN_SBTC_SIGURGMASK                  = 3,
    TN_SBTC_SIGEVENTMASK                = 4,
    TN_SBTC_ERRNO                       = 6,
    TN_SBTC_HERRNO                      = 7,
    TN_SBTC_DTABLESIZE                  = 8,
    TN_SBTC_FDCALLBACK                  = 9,
    TN_SBTC_LOGSTAT                     = 10,
    TN_SBTC_LOGTAGPTR                   = 11,
    TN_SBTC_LOGFACILITY                 = 12,
    TN_SBTC_LOGMASK                     = 13,
    TN_SBTC_ERRNOSTRPTR                 = 14,
    TN_SBTC_HERRNOSTRPTR                = 15,
    TN_SBTC_IOERRNOSTRPTR               = 16,
    TN_SBTC_S2ERRNOSTRPTR               = 17,
    TN_SBTC_S2WERRNOSTRPTR              = 18,
    TN_SBTC_ERRNOBYTEPTR                = 21,
    TN_SBTC_ERRNOWORDPTR                = 22,
    TN_SBTC_ERRNOLONGPTR                = 24,
    TN_SBTC_HERRNOLONGPTR               = 25,
    TN_SBTC_RELEASESTRPTR               = 29,
    TN_SBTC_NUM_PACKET_FILTER_CHANNELS  = 40,
    TN_SBTC_HAVE_ROUTING_API            = 41,
    TN_SBTC_UDP_CHECKSUM                = 42,
    TN_SBTC_IP_FORWARDING               = 43,
    TN_SBTC_IP_DEFAULT_TTL              = 44,
    TN_SBTC_ICMP_MASK_REPLY             = 45,
    TN_SBTC_ICMP_SEND_REDIRECTS         = 46,
    TN_SBTC_HAVE_INTERFACE_API          = 47,
    TN_SBTC_ICMP_PROCESS_ECHO           = 48,
    TN_SBTC_ICMP_PROCESS_TSTAMP         = 49,
    TN_SBTC_HAVE_MONITORING_API         = 50,
    TN_SBTC_CAN_SHARE_LIBRARY_BASES     = 51,
    TN_SBTC_LOG_FILE_NAME               = 52,
    TN_SBTC_HAVE_STATUS_API             = 53,
    TN_SBTC_HAVE_DNS_API                = 54,
    TN_SBTC_LOG_HOOK                    = 55,
    TN_SBTC_SYSTEM_STATUS               = 56,
    TN_SBTC_SIG_ADDRESS_CHANGE_MASK     = 57,
    TN_SBTC_IPF_API_VERSION             = 58,
    TN_SBTC_HAVE_LOCAL_DATABASE_API     = 59,
    TN_SBTC_HAVE_ADDRESS_CONVERSION_API = 60,
    TN_SBTC_HAVE_KERNEL_MEMORY_API      = 61,
    TN_SBTC_IP_FILTER_HOOK              = 62,
    TN_SBTC_HAVE_SERVER_API             = 63,
    TN_SBTC_GET_BYTES_RECEIVED          = 64,
    TN_SBTC_GET_BYTES_SENT              = 65,
    TN_SBTC_IDN_DEFAULT_CHARACTER_SET   = 66,
    TN_SBTC_HAVE_ROADSHOWDATA_API       = 67,
    TN_SBTC_ERROR_HOOK                  = 68,
    TN_SBTC_HAVE_GETHOSTADDR_R_API      = 69
};

/* Capability bits reported on SBTC_HAVE_*_API GET queries. */
#define TN_SBTC_HAVE_DNS_API_BIT          0x01u
#define TN_SBTC_HAVE_LOCAL_DB_API_BIT     0x02u
#define TN_SBTC_HAVE_ADDR_CONV_API_BIT    0x04u
#define TN_SBTC_HAVE_GETHOSTADDR_R_BIT    0x08u
#define TN_SBTC_HAVE_SERVER_API_BIT       0x10u

/* What the caller must do for this tag. */
typedef enum {
    TN_SBTC_OP_NONE = 0,
    TN_SBTC_OP_GET,             /* write `value` back: REF -> *(ULONG *)ti_Data, VAL -> ti_Data */
    TN_SBTC_OP_SET_SIGINT,      /* new sig_int  = value (caller dereferences if is_ref) */
    TN_SBTC_OP_SET_SIGIO,
    TN_SBTC_OP_SET_SIGURG,
    TN_SBTC_OP_SET_SIGEVENT,
    TN_SBTC_OP_SET_ERRNO,       /* tn_set_errno_val(base, value) */
    TN_SBTC_OP_SET_HERRNO,      /* tn_set_herrno_val(base, value) */
    TN_SBTC_OP_SET_ERRNO_PTR,   /* errno_ptr = ti_Data, width = errno_ptr_width */
    TN_SBTC_OP_SET_HERRNO_PTR,  /* herrno_ptr = ti_Data */
    TN_SBTC_OP_SET_FDCALLBACK,  /* base->fd_callback = value */
    TN_SBTC_OP_SET_LOGSTAT,     /* base->log_stat = value */
    TN_SBTC_OP_SET_LOGTAGPTR,   /* base->log_tag_ptr = value */
    TN_SBTC_OP_SET_LOGFACILITY, /* base->log_facility = value */
    TN_SBTC_OP_SET_LOGMASK,     /* base->log_mask = value */
    TN_SBTC_OP_SET_UDPCHECKSUM, /* base->udp_checksum = value */
    TN_SBTC_OP_SET_IPDEFAULTTTL,/* base->ip_default_ttl = value */
    TN_SBTC_OP_GET_ERRNO_STR,   /* return string for errno */
    TN_SBTC_OP_GET_HERRNO_STR,  /* return string for h_errno */
    TN_SBTC_OP_GET_IOERRNO_STR, /* return string for io_Error */
    TN_SBTC_OP_GET_S2ERRNO_STR, /* return string for S2ERR_* */
    TN_SBTC_OP_GET_S2WERRNO_STR /* return string for S2WERR_* */
} TnSbtcOp;

typedef struct TnSbtcResult {
    int handled;         /* 0 = unknown tag (the LVO counts it, TNET-036) */
    TnSbtcOp op;
    int is_ref;          /* ti_Data is a pointer (SET: read through it; GET: write through it) */
    uint32_t value;      /* resolved value (GET) or raw ti_Data (SET with is_ref) */
    int errno_ptr_width; /* 1/2/4 for TN_SBTC_OP_SET_ERRNO_PTR */
} TnSbtcResult;

/* Plain per-opener state the dispatcher reads (writes surface as ops). */
typedef struct TnSbtcState {
    uint32_t sig_int;        /* SBTC_BREAKMASK value */
    uint32_t sig_io;         /* SBTC_SIGIOMASK value */
    uint32_t sig_urg;        /* SBTC_SIGURGMASK value */
    uint32_t sig_event;      /* SBTC_SIGEVENTMASK value */
    int32_t  errno_val;      /* SBTC_ERRNO value */
    int32_t  herrno_val;     /* SBTC_HERRNO value */
    uint32_t errno_ptr;      /* TNET-119: current errno pointer (GET on SBTC_ERRNO*PTR) */
    uint32_t errno_width;    /* TNET-119: 1/2/4 width of that pointer (GET context) */
    uint32_t herrno_ptr;     /* TNET-120: current h_errno pointer (GET on SBTC_HERRNO*PTR) */
    uint32_t dtablesize;     /* SBTC_DTABLESIZE (GET) */
    uint32_t fd_callback;    /* SBTC_FDCALLBACK (GET/SET) */
    uint32_t log_stat;       /* SBTC_LOGSTAT */
    uint32_t log_tag_ptr;    /* SBTC_LOGTAGPTR */
    uint32_t log_facility;   /* SBTC_LOGFACILITY */
    uint32_t log_mask;       /* SBTC_LOGMASK */
    uint32_t udp_checksum;   /* SBTC_UDP_CHECKSUM */
    uint32_t ip_default_ttl; /* SBTC_IP_DEFAULT_TTL */
    uint32_t have_bits;      /* TN_SBTC_HAVE_*_API_BIT mask */
    uint32_t release_str;    /* SBTC_RELEASESTRPTR (GET) — pointer-sized value on
                              * the real API; kept opaque here */
} TnSbtcState;

/*
 * Classify one raw SocketBaseTagList tag. `data` is ti_Data. Returns
 * res->handled (1 = handled, 0 = unknown tag). Never dereferences memory.
 */
int tn_sbtc_dispatch_tag(uint32_t raw_tag, uint32_t data,
                         const TnSbtcState *state, TnSbtcResult *res);

#endif /* TOLUNNET_SBTC_DISPATCH_H */
