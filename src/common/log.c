/*
 * tolunet logging implementation — DOS Write/Output (master prompt §3: no
 * stdio in resident code).
 *
 * M0: minimal. A formatted layer (VPrintf-based) comes with the task in M2,
 * once the task owns its DOSBase and a write-safe context. For now we only
 * write a plain string so there is no varargs/format dependency to verify.
 *
 * All DOS symbols from NDK 3.2 via proto/dos.h. The task must set g_log_dos
 * before calling tn_log(); a NULL g_log_dos means "no logging yet".
 */

#include "log.h"

#include <proto/dos.h>

struct Library *g_log_dos = NULL;
int             g_log_level = TN_LOG_OFF;

void tn_log(int tier, const char *msg)
{
    LONG len;

    if (msg == NULL) return;
    if (tier > g_log_level) return;       /* tiered suppression */
    if (g_log_dos == NULL) return;        /* DOS not yet available */

    /* strlen by hand: no libc in resident code (§3). */
    len = 0;
    while (msg[len] != '\0') len++;

    /* Output() returns the current window handle (NDK dos.library). */
    Write(Output(), (APTR)msg, len);
}
