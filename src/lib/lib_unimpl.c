/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * lib_unimpl.c — Honest C stubs for unimplemented bsdsocket.library vectors.
 * Generated automatically by scripts/gen_lvo_table.py from sfd/bsdsocket_lib.sfd (Round 3 §D.2).
 * Returns exact honest values per SFD signature: LONG -> -1 + ENOSYS/ENXIO,
 * pointers -> NULL (+ NO_RECOVERY for resolver), BOOL -> FALSE, in_addr_t -> INADDR_NONE.
 */

#include "../../include/ipc.h"
#include "../common/log.h"
#include "lib_init.h"
#include <proto/exec.h>
#include <exec/lists.h>
#include <devices/timer.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/mbuf.h>
#include <net/route.h>
#include <netdb.h>
#include <libraries/bsdsocket.h>
#include <dos/dosextens.h>
#include <sys/errno.h>

static inline void tn_unimpl_set_errno(TnSocketBase *base, LONG err)
{
    if (base == NULL) return;
    base->task_errno = err;
    if (base->errno_ptr != NULL) {
        if (base->errno_width == 1) {
            *((UBYTE *)base->errno_ptr) = (UBYTE)err;
        } else if (base->errno_width == 2) {
            *((UWORD *)base->errno_ptr) = (UWORD)err;
        } else {
            *base->errno_ptr = err;
        }
    }
}

static inline void tn_unimpl_set_herrno(TnSocketBase *base, LONG herr)
{
    if (base == NULL) return;
    base->task_herrno = herr;
    if (base->herrno_ptr != NULL) {
        *base->herrno_ptr = herr;
    }
}

/* -366: bpf_open */
LONG tn_unimpl_bpf_open(LONG channel, TnSocketBase *base)
{
    static int logged = 0; (void)channel; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: bpf_open not implemented\n"); }
    tn_unimpl_set_errno(base, ENXIO);
    return -1;
}

/* -372: bpf_close */
LONG tn_unimpl_bpf_close(LONG channel, TnSocketBase *base)
{
    static int logged = 0; (void)channel; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: bpf_close not implemented\n"); }
    tn_unimpl_set_errno(base, ENXIO);
    return -1;
}

/* -378: bpf_read */
LONG tn_unimpl_bpf_read(LONG channel, APTR buffer, LONG len, TnSocketBase *base)
{
    static int logged = 0; (void)channel; (void)buffer; (void)len; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: bpf_read not implemented\n"); }
    tn_unimpl_set_errno(base, ENXIO);
    return -1;
}

/* -384: bpf_write */
LONG tn_unimpl_bpf_write(LONG channel, APTR buffer, LONG len, TnSocketBase *base)
{
    static int logged = 0; (void)channel; (void)buffer; (void)len; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: bpf_write not implemented\n"); }
    tn_unimpl_set_errno(base, ENXIO);
    return -1;
}

/* -390: bpf_set_notify_mask */
LONG tn_unimpl_bpf_set_notify_mask(LONG channel, ULONG signal_mask, TnSocketBase *base)
{
    static int logged = 0; (void)channel; (void)signal_mask; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: bpf_set_notify_mask not implemented\n"); }
    tn_unimpl_set_errno(base, ENXIO);
    return -1;
}

/* -396: bpf_set_interrupt_mask */
LONG tn_unimpl_bpf_set_interrupt_mask(LONG channel, ULONG signal_mask, TnSocketBase *base)
{
    static int logged = 0; (void)channel; (void)signal_mask; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: bpf_set_interrupt_mask not implemented\n"); }
    tn_unimpl_set_errno(base, ENXIO);
    return -1;
}

/* -402: bpf_ioctl */
LONG tn_unimpl_bpf_ioctl(LONG channel, ULONG command, APTR buffer, TnSocketBase *base)
{
    static int logged = 0; (void)channel; (void)command; (void)buffer; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: bpf_ioctl not implemented\n"); }
    tn_unimpl_set_errno(base, ENXIO);
    return -1;
}

/* -408: bpf_data_waiting */
LONG tn_unimpl_bpf_data_waiting(LONG channel, TnSocketBase *base)
{
    static int logged = 0; (void)channel; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: bpf_data_waiting not implemented\n"); }
    tn_unimpl_set_errno(base, ENXIO);
    return -1;
}

/* -414: AddRouteTagList */
LONG tn_unimpl_addroutetaglist(struct TagItem *tags, TnSocketBase *base)
{
    static int logged = 0; (void)tags; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: AddRouteTagList not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return -1;
}

/* -420: DeleteRouteTagList */
LONG tn_unimpl_deleteroutetaglist(struct TagItem *tags, TnSocketBase *base)
{
    static int logged = 0; (void)tags; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: DeleteRouteTagList not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return -1;
}

/* -426: ChangeRouteTagList */
LONG tn_unimpl_changeroutetaglist(struct TagItem *tags, TnSocketBase *base)
{
    static int logged = 0; (void)tags; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: ChangeRouteTagList not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return -1;
}

/* -432: FreeRouteInfo */
VOID tn_unimpl_freerouteinfo(struct rt_msghdr *buf, TnSocketBase *base)
{
    static int logged = 0; (void)buf; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: FreeRouteInfo not implemented\n"); }
    return;
}

/* -438: GetRouteInfo */
struct rt_msghdr * tn_unimpl_getrouteinfo(LONG address_family, LONG flags, TnSocketBase *base)
{
    static int logged = 0; (void)address_family; (void)flags; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: GetRouteInfo not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return NULL;
}

/* -444: AddInterfaceTagList */
LONG tn_unimpl_addinterfacetaglist(STRPTR interface_name, STRPTR device_name, LONG unit, struct TagItem *tags, TnSocketBase *base)
{
    static int logged = 0; (void)interface_name; (void)device_name; (void)unit; (void)tags; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: AddInterfaceTagList not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return -1;
}

/* -450: ConfigureInterfaceTagList */
LONG tn_unimpl_configureinterfacetaglist(STRPTR interface_name, struct TagItem *tags, TnSocketBase *base)
{
    static int logged = 0; (void)interface_name; (void)tags; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: ConfigureInterfaceTagList not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return -1;
}

/* -456: ReleaseInterfaceList */
VOID tn_unimpl_releaseinterfacelist(struct List *list, TnSocketBase *base)
{
    static int logged = 0; (void)list; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: ReleaseInterfaceList not implemented\n"); }
    return;
}

/* -462: ObtainInterfaceList */
struct List * tn_unimpl_obtaininterfacelist(TnSocketBase *base)
{
    static int logged = 0; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: ObtainInterfaceList not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return NULL;
}

/* -468: QueryInterfaceTagList */
LONG tn_unimpl_queryinterfacetaglist(STRPTR interface_name, struct TagItem *tags, TnSocketBase *base)
{
    static int logged = 0; (void)interface_name; (void)tags; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: QueryInterfaceTagList not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return -1;
}

/* -474: CreateAddrAllocMessageA */
LONG tn_unimpl_createaddrallocmessagea(LONG version, LONG protocol, STRPTR interface_name, struct AddressAllocationMessage **result_ptr, struct TagItem *tags, TnSocketBase *base)
{
    static int logged = 0; (void)version; (void)protocol; (void)interface_name; (void)result_ptr; (void)tags; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: CreateAddrAllocMessageA not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return -1;
}

/* -480: DeleteAddrAllocMessage */
VOID tn_unimpl_deleteaddrallocmessage(struct AddressAllocationMessage *aam, TnSocketBase *base)
{
    static int logged = 0; (void)aam; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: DeleteAddrAllocMessage not implemented\n"); }
    return;
}

/* -486: BeginInterfaceConfig */
VOID tn_unimpl_begininterfaceconfig(struct AddressAllocationMessage * message, TnSocketBase *base)
{
    static int logged = 0; (void)message; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: BeginInterfaceConfig not implemented\n"); }
    return;
}

/* -492: AbortInterfaceConfig */
VOID tn_unimpl_abortinterfaceconfig(struct AddressAllocationMessage * message, TnSocketBase *base)
{
    static int logged = 0; (void)message; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: AbortInterfaceConfig not implemented\n"); }
    return;
}

/* -498: AddNetMonitorHookTagList */
LONG tn_unimpl_addnetmonitorhooktaglist(LONG type, struct Hook *hook, struct TagItem *tags, TnSocketBase *base)
{
    static int logged = 0; (void)type; (void)hook; (void)tags; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: AddNetMonitorHookTagList not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return -1;
}

/* -504: RemoveNetMonitorHook */
VOID tn_unimpl_removenetmonitorhook(struct Hook *hook, TnSocketBase *base)
{
    static int logged = 0; (void)hook; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: RemoveNetMonitorHook not implemented\n"); }
    return;
}

/* -510: GetNetworkStatistics */
LONG tn_unimpl_getnetworkstatistics(LONG type, LONG version, APTR destination, LONG size, TnSocketBase *base)
{
    static int logged = 0; (void)type; (void)version; (void)destination; (void)size; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: GetNetworkStatistics not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return -1;
}

/* -516: AddDomainNameServer */
LONG tn_unimpl_adddomainnameserver(STRPTR address, TnSocketBase *base)
{
    static int logged = 0; (void)address; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: AddDomainNameServer not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return -1;
}

/* -522: RemoveDomainNameServer */
LONG tn_unimpl_removedomainnameserver(STRPTR address, TnSocketBase *base)
{
    static int logged = 0; (void)address; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: RemoveDomainNameServer not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return -1;
}

/* -528: ReleaseDomainNameServerList */
VOID tn_unimpl_releasedomainnameserverlist(struct List *list, TnSocketBase *base)
{
    static int logged = 0; (void)list; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: ReleaseDomainNameServerList not implemented\n"); }
    return;
}

/* -534: ObtainDomainNameServerList */
struct List * tn_unimpl_obtaindomainnameserverlist(TnSocketBase *base)
{
    static int logged = 0; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: ObtainDomainNameServerList not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return NULL;
}

/* -624: mbuf_copym */
struct mbuf * tn_unimpl_mbuf_copym(struct mbuf *m, LONG off, LONG len, TnSocketBase *base)
{
    static int logged = 0; (void)m; (void)off; (void)len; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: mbuf_copym not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return NULL;
}

/* -630: mbuf_copyback */
LONG tn_unimpl_mbuf_copyback(struct mbuf *m, LONG off, LONG len, APTR cp, TnSocketBase *base)
{
    static int logged = 0; (void)m; (void)off; (void)len; (void)cp; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: mbuf_copyback not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return -1;
}

/* -636: mbuf_copydata */
LONG tn_unimpl_mbuf_copydata(struct mbuf *m, LONG off, LONG len, APTR cp, TnSocketBase *base)
{
    static int logged = 0; (void)m; (void)off; (void)len; (void)cp; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: mbuf_copydata not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return -1;
}

/* -642: mbuf_free */
struct mbuf * tn_unimpl_mbuf_free(struct mbuf *m, TnSocketBase *base)
{
    static int logged = 0; (void)m; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: mbuf_free not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return NULL;
}

/* -648: mbuf_freem */
VOID tn_unimpl_mbuf_freem(struct mbuf *m, TnSocketBase *base)
{
    static int logged = 0; (void)m; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: mbuf_freem not implemented\n"); }
    return;
}

/* -654: mbuf_get */
struct mbuf * tn_unimpl_mbuf_get(TnSocketBase *base)
{
    static int logged = 0; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: mbuf_get not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return NULL;
}

/* -660: mbuf_gethdr */
struct mbuf * tn_unimpl_mbuf_gethdr(TnSocketBase *base)
{
    static int logged = 0; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: mbuf_gethdr not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return NULL;
}

/* -666: mbuf_prepend */
struct mbuf * tn_unimpl_mbuf_prepend(struct mbuf *m, LONG len, TnSocketBase *base)
{
    static int logged = 0; (void)m; (void)len; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: mbuf_prepend not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return NULL;
}

/* -672: mbuf_cat */
LONG tn_unimpl_mbuf_cat(struct mbuf *m, struct mbuf *n, TnSocketBase *base)
{
    static int logged = 0; (void)m; (void)n; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: mbuf_cat not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return -1;
}

/* -678: mbuf_adj */
LONG tn_unimpl_mbuf_adj(struct mbuf *mp, LONG req_len, TnSocketBase *base)
{
    static int logged = 0; (void)mp; (void)req_len; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: mbuf_adj not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return -1;
}

/* -684: mbuf_pullup */
struct mbuf * tn_unimpl_mbuf_pullup(struct mbuf *m, LONG len, TnSocketBase *base)
{
    static int logged = 0; (void)m; (void)len; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: mbuf_pullup not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return NULL;
}

/* -714: ObtainRoadshowData */
struct List * tn_unimpl_obtainroadshowdata(LONG access, TnSocketBase *base)
{
    static int logged = 0; (void)access; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: ObtainRoadshowData not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return NULL;
}

/* -720: ReleaseRoadshowData */
VOID tn_unimpl_releaseroadshowdata(struct List *list, TnSocketBase *base)
{
    static int logged = 0; (void)list; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: ReleaseRoadshowData not implemented\n"); }
    return;
}

/* -726: ChangeRoadshowData */
BOOL tn_unimpl_changeroadshowdata(struct List *list, STRPTR name, ULONG length, APTR data, TnSocketBase *base)
{
    static int logged = 0; (void)list; (void)name; (void)length; (void)data; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: ChangeRoadshowData not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return FALSE;
}

/* -732: RemoveInterface */
LONG tn_unimpl_removeinterface(STRPTR interface_name, LONG force, TnSocketBase *base)
{
    static int logged = 0; (void)interface_name; (void)force; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: RemoveInterface not implemented\n"); }
    tn_unimpl_set_errno(base, ENOSYS);
    return -1;
}

/* -762: ipf_open */
LONG tn_unimpl_ipf_open(LONG channel, TnSocketBase *base)
{
    static int logged = 0; (void)channel; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: ipf_open not implemented\n"); }
    tn_unimpl_set_errno(base, ENXIO);
    return -1;
}

/* -768: ipf_close */
LONG tn_unimpl_ipf_close(LONG channel, TnSocketBase *base)
{
    static int logged = 0; (void)channel; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: ipf_close not implemented\n"); }
    tn_unimpl_set_errno(base, ENXIO);
    return -1;
}

/* -774: ipf_ioctl */
LONG tn_unimpl_ipf_ioctl(LONG channel, ULONG command, APTR buffer, TnSocketBase *base)
{
    static int logged = 0; (void)channel; (void)command; (void)buffer; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: ipf_ioctl not implemented\n"); }
    tn_unimpl_set_errno(base, ENXIO);
    return -1;
}

/* -780: ipf_log_read */
LONG tn_unimpl_ipf_log_read(LONG channel, APTR buffer, LONG len, TnSocketBase *base)
{
    static int logged = 0; (void)channel; (void)buffer; (void)len; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: ipf_log_read not implemented\n"); }
    tn_unimpl_set_errno(base, ENXIO);
    return -1;
}

/* -786: ipf_log_data_waiting */
LONG tn_unimpl_ipf_log_data_waiting(LONG channel, TnSocketBase *base)
{
    static int logged = 0; (void)channel; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: ipf_log_data_waiting not implemented\n"); }
    tn_unimpl_set_errno(base, ENXIO);
    return -1;
}

/* -792: ipf_set_notify_mask */
LONG tn_unimpl_ipf_set_notify_mask(LONG channel, ULONG mask, TnSocketBase *base)
{
    static int logged = 0; (void)channel; (void)mask; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: ipf_set_notify_mask not implemented\n"); }
    tn_unimpl_set_errno(base, ENXIO);
    return -1;
}

/* -798: ipf_set_interrupt_mask */
LONG tn_unimpl_ipf_set_interrupt_mask(LONG channel, ULONG mask, TnSocketBase *base)
{
    static int logged = 0; (void)channel; (void)mask; (void)base;
    if (!logged) { logged = 1; tn_lib_log(base, TN_LOG_VERBOSE, "tolunnet: ipf_set_interrupt_mask not implemented\n"); }
    tn_unimpl_set_errno(base, ENXIO);
    return -1;
}
