/*
 * tolunnet — portable network and OS error string tables.
 */
#include "errstr.h"
#include <stddef.h>

static const char * const g_sys_errlist[] = {
    "Undefined error: 0",                     /* 0 */
    "Operation not permitted",                /* 1  EPERM */
    "No such file or directory",              /* 2  ENOENT */
    "No such process",                        /* 3  ESRCH */
    "Interrupted system call",                /* 4  EINTR */
    "Input/output error",                     /* 5  EIO */
    "Device not configured",                  /* 6  ENXIO */
    "Argument list too long",                 /* 7  E2BIG */
    "Exec format error",                      /* 8  ENOEXEC */
    "Bad file descriptor",                    /* 9  EBADF */
    "No child processes",                     /* 10 ECHILD */
    "Resource deadlock avoided",              /* 11 EDEADLK */
    "Cannot allocate memory",                 /* 12 ENOMEM */
    "Permission denied",                      /* 13 EACCES */
    "Bad address",                            /* 14 EFAULT */
    "Block device required",                  /* 15 ENOTBLK */
    "Device busy",                            /* 16 EBUSY */
    "File exists",                            /* 17 EEXIST */
    "Cross-device link",                      /* 18 EXDEV */
    "Operation not supported by device",      /* 19 ENODEV */
    "Not a directory",                        /* 20 ENOTDIR */
    "Is a directory",                         /* 21 EISDIR */
    "Invalid argument",                       /* 22 EINVAL */
    "Too many open files in system",          /* 23 ENFILE */
    "Too many open files",                    /* 24 EMFILE */
    "Inappropriate ioctl for device",         /* 25 ENOTTY */
    "Text file busy",                         /* 26 ETXTBSY */
    "File too large",                         /* 27 EFBIG */
    "No space left on device",                /* 28 ENOSPC */
    "Illegal seek",                           /* 29 ESPIPE */
    "Read-only file system",                  /* 30 EROFS */
    "Too many links",                         /* 31 EMLINK */
    "Broken pipe",                            /* 32 EPIPE */
    "Numerical argument out of domain",       /* 33 EDOM */
    "Result too large",                       /* 34 ERANGE */
    "Resource temporarily unavailable",       /* 35 EAGAIN / EWOULDBLOCK */
    "Operation now in progress",              /* 36 EINPROGRESS */
    "Operation already in progress",          /* 37 EALREADY */
    "Socket operation on non-socket",         /* 38 ENOTSOCK */
    "Destination address required",           /* 39 EDESTADDRREQ */
    "Message too long",                       /* 40 EMSGSIZE */
    "Protocol wrong type for socket",         /* 41 EPROTOTYPE */
    "Protocol not available",                 /* 42 ENOPROTOOPT */
    "Protocol not supported",                 /* 43 EPROTONOSUPPORT */
    "Socket type not supported",              /* 44 ESOCKTNOSUPPORT */
    "Operation not supported",                /* 45 EOPNOTSUPP */
    "Protocol family not supported",          /* 46 EPFNOSUPPORT */
    "Address family not supported by protocol family", /* 47 EAFNOSUPPORT */
    "Address already in use",                 /* 48 EADDRINUSE */
    "Can't assign requested address",         /* 49 EADDRNOTAVAIL */
    "Network is down",                        /* 50 ENETDOWN */
    "Network is unreachable",                 /* 51 ENETUNREACH */
    "Network dropped connection on reset",    /* 52 ENETRESET */
    "Software caused connection abort",       /* 53 ECONNABORTED */
    "Connection reset by peer",               /* 54 ECONNRESET */
    "No buffer space available",              /* 55 ENOBUFS */
    "Socket is already connected",            /* 56 EISCONN */
    "Socket is not connected",                /* 57 ENOTCONN */
    "Can't send after socket shutdown",       /* 58 ESHUTDOWN */
    "Too many references: can't splice",      /* 59 ETOOMANYREFS */
    "Operation timed out",                    /* 60 ETIMEDOUT */
    "Connection refused",                     /* 61 ECONNREFUSED */
    "Too many levels of symbolic links",      /* 62 ELOOP */
    "File name too long",                     /* 63 ENAMETOOLONG */
    "Host is down",                           /* 64 EHOSTDOWN */
    "No route to host",                       /* 65 EHOSTUNREACH */
    "Directory not empty",                    /* 66 ENOTEMPTY */
    "Too many processes",                     /* 67 EPROCLIM */
    "Too many users",                         /* 68 EUSERS */
    "Disc quota exceeded",                    /* 69 EDQUOT */
    "Stale NFS file handle",                  /* 70 ESTALE */
    "Too many levels of remote in path",      /* 71 EREMOTE */
    "RPC struct is bad",                      /* 72 EBADRPC */
    "RPC version wrong",                      /* 73 ERPCMISMATCH */
    "RPC prog. not avail",                    /* 74 EPROGUNAVAIL */
    "Program version wrong",                  /* 75 EPROGMISMATCH */
    "Bad procedure for program",              /* 76 EPROCUNAVAIL */
    "No locks available",                     /* 77 ENOLCK */
    "Function not implemented",               /* 78 ENOSYS */
    "Inappropriate file type or format",      /* 79 EFTYPE */
    "Authentication error",                   /* 80 EAUTH */
    "Need authenticator"                      /* 81 ENEEDAUTH */
};

#define SYS_NERR ((int)(sizeof(g_sys_errlist) / sizeof(g_sys_errlist[0])))

const char *tn_strerror(int err)
{
    if (err >= 0 && err < SYS_NERR) {
        return g_sys_errlist[err];
    }
    return "Unknown error";
}

const char *tn_hstrerror(int herr)
{
    switch (herr) {
    case 0:  return "Resolver Error 0 (no error)";
    case 1:  return "Unknown host";             /* HOST_NOT_FOUND */
    case 2:  return "Host name lookup failure"; /* TRY_AGAIN */
    case 3:  return "Unknown server error";     /* NO_RECOVERY */
    case 4:  return "No address associated with name"; /* NO_DATA */
    default: return "Unknown resolver error";
    }
}

const char *tn_ioerror(int ioerr)
{
    if (ioerr < 0) ioerr = -ioerr;
    switch (ioerr) {
    case 0: return "No error";
    case 1: return "Device or unit failed to open"; /* IOERR_OPENFAIL */
    case 2: return "Request terminated early";     /* IOERR_ABORTED */
    case 3: return "Command not supported by device"; /* IOERR_NOCMD */
    case 4: return "Invalid length";               /* IOERR_BADLENGTH */
    case 5: return "Invalid address";              /* IOERR_BADADDRESS */
    case 6: return "Device unit is busy";          /* IOERR_UNITBUSY */
    case 7: return "Hardware failed self-test";    /* IOERR_SELFTEST */
    default: return "Unknown I/O error";
    }
}

const char *tn_s2error(int s2err)
{
    switch (s2err) {
    case 0:  return "No error";
    case 1:  return "Resource allocation failure";   /* S2ERR_NO_RESOURCES */
    case 3:  return "Bad argument";                  /* S2ERR_BAD_ARGUMENT */
    case 4:  return "Inappropriate state";           /* S2ERR_BAD_STATE */
    case 5:  return "Bad address";                   /* S2ERR_BAD_ADDRESS */
    case 6:  return "Packet length exceeded MTU";    /* S2ERR_MTU_EXCEEDED */
    case 8:  return "Command not supported";         /* S2ERR_NOT_SUPPORTED */
    case 9:  return "Software error detected";       /* S2ERR_SOFTWARE */
    case 10: return "Driver is offline";             /* S2ERR_OUTOFSERVICE */
    case 11: return "Transmission attempt failed";   /* S2ERR_TX_FAILURE */
    default: return "Unknown SANA-II error";
    }
}

const char *tn_s2werror(int s2werr)
{
    switch (s2werr) {
    case 0:  return "Generic error";                         /* S2WERR_GENERIC_ERROR */
    case 1:  return "Unit not configured";                  /* S2WERR_NOT_CONFIGURED */
    case 2:  return "Unit is currently online";             /* S2WERR_UNIT_ONLINE */
    case 3:  return "Unit is currently offline";            /* S2WERR_UNIT_OFFLINE */
    case 4:  return "Protocol already tracked";             /* S2WERR_ALREADY_TRACKED */
    case 5:  return "Protocol not tracked";                 /* S2WERR_NOT_TRACKED */
    case 6:  return "Buffer management function returned error"; /* S2WERR_BUFF_ERROR */
    case 7:  return "Source address problem";               /* S2WERR_SRC_ADDRESS */
    case 8:  return "Destination address problem";          /* S2WERR_DST_ADDRESS */
    case 9:  return "Broadcast address problem";            /* S2WERR_BAD_BROADCAST */
    case 10: return "Multicast address problem";            /* S2WERR_BAD_MULTICAST */
    case 11: return "Multicast address list full";          /* S2WERR_MULTICAST_FULL */
    case 12: return "Unsupported event class";              /* S2WERR_BAD_EVENT */
    case 13: return "Statdata failed sanity check";         /* S2WERR_BAD_STATDATA */
    case 15: return "Attempt to configure twice";           /* S2WERR_IS_CONFIGURED */
    case 16: return "Null pointer detected";                /* S2WERR_NULL_POINTER */
    case 17: return "Transmission failed: too many retries"; /* S2WERR_TOO_MANY_RETRIES */
    case 18: return "Driver fixable hardware error";        /* S2WERR_RCVREL_HDW_ERR */
    case 19: return "Unit is currently not connected";      /* S2WERR_UNIT_DISCONNECTED */
    case 20: return "Unit is currently connected";          /* S2WERR_UNIT_CONNECTED */
    case 21: return "Invalid option rejected";              /* S2WERR_INVALID_OPTION */
    case 22: return "Mandatory option is missing";          /* S2WERR_MISSING_OPTION */
    case 23: return "Authentication failed";                /* S2WERR_AUTHENTICATION_FAILED */
    default: return "Unknown SANA-II wire error";
    }
}
