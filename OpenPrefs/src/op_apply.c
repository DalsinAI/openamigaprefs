/* OpenPrefs APPLY: the one boot call in place of User-Startup's
 *     SYS:Prefs/OpenSound USE
 *     SYS:Prefs/OpenGamepads USE        (OpenUp's OpenInput part)
 *     OpenSystem APPLY                  (System/DESIGN.md)
 * (Dale, 17 October 2026, 08:24). Each is that editor's own Shell path, run with the same arguments
 * (lt_args): Sound's levels to the ACAHI board and its files, Gamepads' settings to openinput.library (only
 * when openinput.library opens, as its line was only there with OpenInput), and the stack caps to
 * AmigaChrome's runtime; OpenSystem's "no runtime here" line is not printed at boot (its return code 5 is
 * kept out of APPLY's). In ROM: it is served by C:OpenPrefs, the small body (op_small.c), so the boot
 * doesn't unpack the app; SYS:Prefs/OpenPrefs APPLY does the same from the app.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "openprefs.h"

extern const char *lt_args;
char *op_argv[] = { (char *)"OpenPrefs", NULL };

int op_apply(void)
{
    int rc, r;
    struct Library *oi;
    BPTR nil, was;
    lt_args = "USE\n";
    rc = sound_main(1, op_argv);
    if ((oi = OpenLibrary((STRPTR)"openinput.library", 0))) {
        CloseLibrary(oi);
        lt_args = "USE\n";
        if ((r = gamepads_main(1, op_argv)) > rc) rc = r;
    }
    lt_args = "APPLY\n";
    if ((nil = Open((STRPTR)"NIL:", MODE_NEWFILE))) {
        was = SelectOutput(nil);
        r = system_main(1, op_argv);
        SelectOutput(was);
        Close(nil);
        if (r > RETURN_WARN && r > rc) rc = r;
    }
    lt_args = NULL;
    return rc;
}
