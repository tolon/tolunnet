/*
 * S2Toggle — bench helper for the S2_ONEVENT link-event tests (TNET-109).
 *
 * Opens the same SANA-II unit as the tolunnet daemon from a second requester
 * and issues S2_OFFLINE / S2_ONLINE. The daemon's armed S2_ONEVENT request
 * then completes and its handler flips the lwIP link state; tc_link_events
 * observes the flip via GETSTATUS. This tool deliberately does NOT use the
 * tolunnet stack — it must be able to run while the link is down.
 *
 * Usage: S2Toggle OFFLINE | S2Toggle ONLINE [UNIT <n>] [DEVICE <name>]
 * Exit code 0 = command accepted by the driver, 20 = error (details printed).
 */
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/sana2.h>
#include <utility/tagitem.h>

int main(int argc, char *argv[])
{
    struct Library *DOSBase = OpenLibrary((CONST_STRPTR)"dos.library", 0);
    struct MsgPort *port;
    struct IOSana2Req *io;
    LONG opts[4];
    struct RDArgs *rda;
    LONG rc = 20;

    if (DOSBase == NULL) return 20;
    (void)argc;
    (void)argv;

    opts[0] = 0; opts[1] = 0; opts[2] = 0; opts[3] = 0;
    rda = ReadArgs((CONST_STRPTR)"OFFLINE/S,ONLINE/S,UNIT/N,DEVICE", opts, NULL);
    if (rda == NULL) {
        PutStr((CONST_STRPTR)"S2Toggle: bad args (OFFLINE/S,ONLINE/S,UNIT/N,DEVICE)\n");
        CloseLibrary(DOSBase);
        return 20;
    }

    port = CreateMsgPort();
    if (port == NULL) {
        PutStr((CONST_STRPTR)"S2Toggle: no port\n");
        FreeArgs(rda);
        CloseLibrary(DOSBase);
        return 20;
    }

    io = (struct IOSana2Req *)AllocVec(sizeof(struct IOSana2Req),
                                       MEMF_CLEAR | MEMF_PUBLIC);
    if (io == NULL) {
        PutStr((CONST_STRPTR)"S2Toggle: no memory\n");
        DeleteMsgPort(port);
        FreeArgs(rda);
        CloseLibrary(DOSBase);
        return 20;
    }

    io->ios2_Req.io_Message.mn_ReplyPort = port;
    io->ios2_Req.io_Message.mn_Length = (UWORD)sizeof(struct IOSana2Req);
    io->ios2_Req.io_Message.mn_Node.ln_Type = NT_MESSAGE;

    {
        CONST_STRPTR dev = (CONST_STRPTR)"ethernet.device";
        ULONG unit = 0;
        BYTE err;

        if (opts[3] != 0) dev = (CONST_STRPTR)opts[3];
        if (opts[2] != 0) unit = (ULONG)*(LONG *)opts[2];

        err = OpenDevice((STRPTR)dev, unit, (struct IORequest *)io, 0UL);
        if (err != 0) {
            PutStr((CONST_STRPTR)"S2Toggle: OpenDevice failed\n");
            FreeVec(io);
            DeleteMsgPort(port);
            FreeArgs(rda);
            CloseLibrary(DOSBase);
            return 20;
        }

        if (opts[0]) {
            io->ios2_Req.io_Command = S2_OFFLINE;
        } else if (opts[1]) {
            io->ios2_Req.io_Command = S2_ONLINE;
        } else {
            PutStr((CONST_STRPTR)"S2Toggle: nothing to do (OFFLINE or ONLINE)\n");
            CloseDevice((struct IORequest *)io);
            FreeVec(io);
            DeleteMsgPort(port);
            FreeArgs(rda);
            CloseLibrary(DOSBase);
            return 20;
        }

        io->ios2_Req.io_Error = 0;
        DoIO((struct IORequest *)io);

        if (io->ios2_Req.io_Error != 0 &&
            !(io->ios2_Req.io_Error == S2ERR_BAD_STATE &&
              (io->ios2_WireError == S2WERR_UNIT_OFFLINE ||
               io->ios2_WireError == S2WERR_UNIT_ONLINE))) {
            PutStr((CONST_STRPTR)"S2Toggle: command failed\n");
        } else {
            PutStr((CONST_STRPTR)"S2Toggle: ok\n");
            rc = 0;
        }

        CloseDevice((struct IORequest *)io);
    }

    FreeVec(io);
    DeleteMsgPort(port);
    FreeArgs(rda);
    CloseLibrary(DOSBase);
    return rc;
}
