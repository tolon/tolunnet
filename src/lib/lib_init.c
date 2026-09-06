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

/* Complete generated bsdsocket.library jump table (-6..-858, lib_table.gen.c) */
extern const APTR g_lib_vectors[];


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
