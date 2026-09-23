/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — portable SocketBaseTagList tag classifier (host-testable).
 * Semantics mirror the SocketBaseTagList LVO (COMPAT-1, TNET-036): the LVO
 * returns the number of UNHANDLED tags; GET writes the current value back
 * into ti_Data (VAL) or *(ULONG *)ti_Data (REF); SET updates the base.
 * Pure: no pointer dereferences — the caller performs them on Amiga.
 */

#include "sbtc_dispatch.h"
#include <string.h>

static void get_plain(TnSbtcResult *res, int is_ref, uint32_t value)
{
    res->op     = TN_SBTC_OP_GET;
    res->is_ref = is_ref;
    res->value  = value;
}

int tn_sbtc_dispatch_tag(uint32_t raw_tag, uint32_t data,
                         const TnSbtcState *state, TnSbtcResult *res)
{
    uint32_t code   = (uint32_t)TN_SBTM_CODE(raw_tag);
    int      is_set = (raw_tag & TN_SBTF_SET) != 0;
    int      is_ref = (raw_tag & TN_SBTF_REF) != 0;

    memset(res, 0, sizeof(*res));
    res->handled = 1;

    switch (code) {
    case TN_SBTC_BREAKMASK:
        if (is_set) { res->op = TN_SBTC_OP_SET_SIGINT; res->is_ref = is_ref; res->value = data; }
        else get_plain(res, is_ref, state->sig_int);
        break;

    case TN_SBTC_SIGIOMASK:
        if (is_set) { res->op = TN_SBTC_OP_SET_SIGIO; res->is_ref = is_ref; res->value = data; }
        else get_plain(res, is_ref, state->sig_io);
        break;

    case TN_SBTC_SIGURGMASK:
        if (is_set) { res->op = TN_SBTC_OP_SET_SIGURG; res->is_ref = is_ref; res->value = data; }
        else get_plain(res, is_ref, state->sig_urg);
        break;

    case TN_SBTC_SIGEVENTMASK:
        if (is_set) { res->op = TN_SBTC_OP_SET_SIGEVENT; res->is_ref = is_ref; res->value = data; }
        else get_plain(res, is_ref, state->sig_event);
        break;

    case TN_SBTC_ERRNO:
        if (is_set) { res->op = TN_SBTC_OP_SET_ERRNO; res->is_ref = is_ref; res->value = data; }
        else get_plain(res, is_ref, (uint32_t)state->errno_val);
        break;

    case TN_SBTC_HERRNO:
        if (is_set) { res->op = TN_SBTC_OP_SET_HERRNO; res->is_ref = is_ref; res->value = data; }
        else get_plain(res, is_ref, (uint32_t)state->herrno_val);
        break;

    case TN_SBTC_DTABLESIZE:
        if (is_set) { res->op = TN_SBTC_OP_SET_DTABLESIZE; res->value = data; }
        else get_plain(res, is_ref, state->dtablesize);
        break;

    case TN_SBTC_FDCALLBACK:
        if (is_set) { res->op = TN_SBTC_OP_SET_FDCALLBACK; res->is_ref = is_ref; res->value = data; }
        else get_plain(res, is_ref, state->fd_callback);
        break;

    case TN_SBTC_LOGSTAT:
        if (is_set) { res->op = TN_SBTC_OP_SET_LOGSTAT; res->is_ref = is_ref; res->value = data; }
        else get_plain(res, is_ref, state->log_stat);
        break;

    case TN_SBTC_LOGTAGPTR:
        if (is_set) { res->op = TN_SBTC_OP_SET_LOGTAGPTR; res->is_ref = is_ref; res->value = data; }
        else get_plain(res, is_ref, state->log_tag_ptr);
        break;

    case TN_SBTC_LOGFACILITY:
        if (is_set) { res->op = TN_SBTC_OP_SET_LOGFACILITY; res->is_ref = is_ref; res->value = data; }
        else get_plain(res, is_ref, state->log_facility);
        break;

    case TN_SBTC_LOGMASK:
        if (is_set) { res->op = TN_SBTC_OP_SET_LOGMASK; res->is_ref = is_ref; res->value = data; }
        else get_plain(res, is_ref, state->log_mask);
        break;

    case TN_SBTC_ERRNOSTRPTR:
        if (!is_set) { res->op = TN_SBTC_OP_GET_ERRNO_STR; res->is_ref = is_ref; res->value = data; }
        break;

    case TN_SBTC_HERRNOSTRPTR:
        if (!is_set) { res->op = TN_SBTC_OP_GET_HERRNO_STR; res->is_ref = is_ref; res->value = data; }
        break;

    case TN_SBTC_IOERRNOSTRPTR:
        if (!is_set) { res->op = TN_SBTC_OP_GET_IOERRNO_STR; res->is_ref = is_ref; res->value = data; }
        break;

    case TN_SBTC_S2ERRNOSTRPTR:
        if (!is_set) { res->op = TN_SBTC_OP_GET_S2ERRNO_STR; res->is_ref = is_ref; res->value = data; }
        break;

    case TN_SBTC_S2WERRNOSTRPTR:
        if (!is_set) { res->op = TN_SBTC_OP_GET_S2WERRNO_STR; res->is_ref = is_ref; res->value = data; }
        break;

    case TN_SBTC_ERRNOBYTEPTR:
        if (is_set) { res->op = TN_SBTC_OP_SET_ERRNO_PTR; res->errno_ptr_width = 1; res->value = data; }
        else get_plain(res, is_ref, state->errno_ptr);
        break;

    case TN_SBTC_ERRNOWORDPTR:
        if (is_set) { res->op = TN_SBTC_OP_SET_ERRNO_PTR; res->errno_ptr_width = 2; res->value = data; }
        else get_plain(res, is_ref, state->errno_ptr);
        break;

    case TN_SBTC_ERRNOLONGPTR:
        if (is_set) { res->op = TN_SBTC_OP_SET_ERRNO_PTR; res->errno_ptr_width = 4; res->value = data; }
        else get_plain(res, is_ref, state->errno_ptr);
        break;

    case TN_SBTC_HERRNOLONGPTR:
        if (is_set) { res->op = TN_SBTC_OP_SET_HERRNO_PTR; res->value = data; }
        else get_plain(res, is_ref, state->herrno_ptr);
        break;

    case TN_SBTC_RELEASESTRPTR:
        if (!is_set) get_plain(res, is_ref, state->release_str);
        break;

    case TN_SBTC_NUM_PACKET_FILTER_CHANNELS:
        if (!is_set) get_plain(res, is_ref, 0u);
        break;

    case TN_SBTC_UDP_CHECKSUM:
        if (is_set) { res->op = TN_SBTC_OP_SET_UDPCHECKSUM; res->is_ref = is_ref; res->value = data; }
        else get_plain(res, is_ref, state->udp_checksum);
        break;

    case TN_SBTC_IP_FORWARDING:
        if (!is_set) get_plain(res, is_ref, 0u);
        break;

    case TN_SBTC_IP_DEFAULT_TTL:
        if (is_set) { res->op = TN_SBTC_OP_SET_IPDEFAULTTTL; res->is_ref = is_ref; res->value = data; }
        else get_plain(res, is_ref, state->ip_default_ttl);
        break;

    case TN_SBTC_ICMP_MASK_REPLY:
    case TN_SBTC_ICMP_SEND_REDIRECTS:
        if (!is_set) get_plain(res, is_ref, 0u);
        break;

    case TN_SBTC_ICMP_PROCESS_ECHO:
        if (!is_set) get_plain(res, is_ref, 1u); /* lwIP processes ICMP echo */
        break;

    case TN_SBTC_ICMP_PROCESS_TSTAMP:
    case TN_SBTC_CAN_SHARE_LIBRARY_BASES:
    case TN_SBTC_LOG_FILE_NAME:
    case TN_SBTC_LOG_HOOK:
        if (!is_set) get_plain(res, is_ref, 0u);
        break;

    case TN_SBTC_SYSTEM_STATUS:
        if (!is_set) get_plain(res, is_ref, 0x03u); /* ONLINE | DNS_AVAILABLE */
        break;

    case TN_SBTC_SIG_ADDRESS_CHANGE_MASK:
    case TN_SBTC_IPF_API_VERSION:
    case TN_SBTC_HAVE_KERNEL_MEMORY_API:
    case TN_SBTC_IP_FILTER_HOOK:
    case TN_SBTC_GET_BYTES_RECEIVED:
    case TN_SBTC_GET_BYTES_SENT:
    case TN_SBTC_IDN_DEFAULT_CHARACTER_SET:
    case TN_SBTC_HAVE_ROADSHOWDATA_API:
    case TN_SBTC_ERROR_HOOK:
        if (!is_set) get_plain(res, is_ref, 0u);
        break;

    /* Capability queries — answered truthfully from the have_bits mask */
    case TN_SBTC_HAVE_SERVER_API:
        if (!is_set) get_plain(res, is_ref,
                               (state->have_bits & TN_SBTC_HAVE_SERVER_API_BIT) ? 1u : 0u);
        break;

    case TN_SBTC_HAVE_DNS_API:
        if (!is_set) get_plain(res, is_ref,
                               (state->have_bits & TN_SBTC_HAVE_DNS_API_BIT) ? 1u : 0u);
        break;

    case TN_SBTC_HAVE_LOCAL_DATABASE_API:
        if (!is_set) get_plain(res, is_ref,
                               (state->have_bits & TN_SBTC_HAVE_LOCAL_DB_API_BIT) ? 1u : 0u);
        break;

    case TN_SBTC_HAVE_ADDRESS_CONVERSION_API:
        if (!is_set) get_plain(res, is_ref,
                               (state->have_bits & TN_SBTC_HAVE_ADDR_CONV_API_BIT) ? 1u : 0u);
        break;

    case TN_SBTC_HAVE_GETHOSTADDR_R_API:
        if (!is_set) get_plain(res, is_ref,
                               (state->have_bits & TN_SBTC_HAVE_GETHOSTADDR_R_BIT) ? 1u : 0u);
        break;

    case TN_SBTC_HAVE_ROUTING_API:
    case TN_SBTC_HAVE_INTERFACE_API:
    case TN_SBTC_HAVE_MONITORING_API:
    case TN_SBTC_HAVE_STATUS_API:
        if (!is_set) get_plain(res, is_ref, 0u);
        break;

    default:
        res->handled = 0; /* unknown tag — the LVO counts it */
        break;
    }

    return res->handled;
}
