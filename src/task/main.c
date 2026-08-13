/*
 * tolunet hello-task — M0 scaffold exit artefact.
 *
 * Master prompt M0 exit test: "hello task runs in WinUAE and writes a line to
 * WORK:". This program does exactly that and nothing more. It is NOT the
 * network task yet (no lwIP, no SANA-II, no MsgPort) — it only proves the
 * toolchain produces a runnable 68k binary and that DOS works.
 *
 * Startup model (verified against the canonical amiga-gcc / -noixemul example):
 *   - libnix startup provides main(argc, argv).
 *   - We open dos.library ourselves; SysBase comes from absolute location 4.
 * All DOS/Exec symbols below are from NDK 3.2 (proto/exec.h, proto/dos.h).
 *
 * The program writes one line to "WORK:tolunet-hello.log" and echoes the same
 * line to the current window (stdout via Output()). It then returns 0 on
 * success or 5 (RETURN_WARN) on failure, so the exit test is unambiguous.
 */

#include <proto/exec.h>
#include <proto/dos.h>

#include <exec/types.h>

/* Standard AmigaOS shell return codes (NDK dos/dos.h). */
#ifndef RETURN_OK
#define RETURN_OK    0
#endif
#ifndef RETURN_WARN
#define RETURN_WARN  5
#endif
#ifndef RETURN_FAIL
#define RETURN_FAIL  20
#endif

static const char TN_HELLO_MSG[] =
    "tolunet M0: hello-task alive\n";

int main(int argc, char *argv[])
{
    struct ExecBase *SysBase;
    struct Library  *DOSBase;
    BPTR  fh = (BPTR)0;
    LONG  nwritten;
    int   rc = RETURN_FAIL;

    (void)argc; (void)argv;   /* no args handled in M0 */

    SysBase = *(struct ExecBase **)4UL;
    DOSBase = OpenLibrary((CONST_STRPTR)"dos.library", 0);
    if (DOSBase == NULL) {
        /* No DOS at all: cannot print anywhere meaningful. Fail. */
        return RETURN_FAIL;
    }

    /* Echo to the current window so the human sees it on the WinUAE screen. */
    Write(Output(), (APTR)TN_HELLO_MSG, (LONG)sizeof(TN_HELLO_MSG) - 1);

    /* Persist the line to the host-mounted WORK: directory (M0 exit test). */
    fh = Open((CONST_STRPTR)"WORK:tolunet-hello.log", MODE_NEWFILE);
    if (fh == (BPTR)0) {
        Write(Output(), (APTR)"tolunet M0: could not open WORK:tolunet-hello.log\n",
              (LONG)41);
        rc = RETURN_WARN;
        goto out;
    }

    nwritten = Write(fh, (APTR)TN_HELLO_MSG, (LONG)sizeof(TN_HELLO_MSG) - 1);
    if (nwritten != (LONG)(sizeof(TN_HELLO_MSG) - 1)) {
        Write(Output(), (APTR)"tolunet M0: short write to log\n", (LONG)31);
        rc = RETURN_WARN;
    } else {
        Write(Output(), (APTR)"tolunet M0: wrote WORK:tolunet-hello.log\n",
              (LONG)36);
        rc = RETURN_OK;
    }

    Close(fh);

out:
    CloseLibrary(DOSBase);
    return rc;
}
