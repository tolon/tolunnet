/*
 * tolunnet — Native AmigaOS Installer Launcher Binary (v1.1)
 *
 * Fully supports Workbench (WBStartup) and CLI launches:
 * - Changes CurrentDir to the icon's directory.
 * - Searches for Installer in current dir, C:, and SYS:Utilities/.
 * - Launches Commodore Installer with the installation script.
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <workbench/startup.h>

struct DosLibrary *DOSBase = NULL;

int main(int argc, char *argv[])
{
    BPTR lock;
    BPTR old_cd = (BPTR)0;
    CONST_STRPTR installer_cmd = NULL;
    CONST_STRPTR script_name = NULL;
    char cmd_buf[256];
    struct WBStartup *wb = NULL;

    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 36);
    if (!DOSBase) return 20;

    /* Handle Workbench double-click */
    if (argc == 0 && argv != NULL) {
        wb = (struct WBStartup *)argv;
        if (wb->sm_NumArgs > 0 && wb->sm_ArgList[0].wa_Lock != (BPTR)0) {
            old_cd = CurrentDir(wb->sm_ArgList[0].wa_Lock);
        }
    }

    /* 1. Check if Installer exists in current directory */
    lock = Lock((CONST_STRPTR)"Installer", ACCESS_READ);
    if (lock != (BPTR)0) {
        UnLock(lock);
        installer_cmd = (CONST_STRPTR)"Installer";
    }

    /* 2. Check C:Installer */
    if (!installer_cmd) {
        lock = Lock((CONST_STRPTR)"C:Installer", ACCESS_READ);
        if (lock != (BPTR)0) {
            UnLock(lock);
            installer_cmd = (CONST_STRPTR)"C:Installer";
        }
    }

    /* 3. Check SYS:Utilities/Installer */
    if (!installer_cmd) {
        lock = Lock((CONST_STRPTR)"SYS:Utilities/Installer", ACCESS_READ);
        if (lock != (BPTR)0) {
            UnLock(lock);
            installer_cmd = (CONST_STRPTR)"SYS:Utilities/Installer";
        }
    }

    /* 4. Check SYS:C/Installer */
    if (!installer_cmd) {
        lock = Lock((CONST_STRPTR)"SYS:C/Installer", ACCESS_READ);
        if (lock != (BPTR)0) {
            UnLock(lock);
            installer_cmd = (CONST_STRPTR)"SYS:C/Installer";
        }
    }

    /* 5. Fallback */
    if (!installer_cmd) {
        installer_cmd = (CONST_STRPTR)"Installer";
    }

    /* Check script file */
    lock = Lock((CONST_STRPTR)"Install_Tolunnet.script", ACCESS_READ);
    if (lock != (BPTR)0) {
        UnLock(lock);
        script_name = (CONST_STRPTR)"Install_Tolunnet.script";
    } else {
        script_name = (CONST_STRPTR)"Install_Tolunnet";
    }

    /* Build command: "<installer_cmd> <script_name>" */
    {
        char *p = cmd_buf;
        CONST_STRPTR s = installer_cmd;
        while (*s) *p++ = *s++;
        *p++ = ' ';
        s = script_name;
        while (*s) *p++ = *s++;
        *p = '\0';
    }

    /* Execute Installer */
    Execute((CONST_STRPTR)cmd_buf, (BPTR)0, (BPTR)0);

    /* Restore current dir */
    if (old_cd != (BPTR)0) {
        CurrentDir(old_cd);
    }

    CloseLibrary((struct Library *)DOSBase);
    return 0;
}
