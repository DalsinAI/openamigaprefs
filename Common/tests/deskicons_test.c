/* Common/deskicons.h: ENV:Sys/DesktopIcons's words, as the Icons page writes them and OpenBench reads them, and the
 * ones a person or an older file might leave there. Prints "ok"/"FAIL" lines, then PASS or FAIL.
 * Copyright (c) 2026 AmigaChrome Team. MIT. */
#include <stdio.h>
#include <string.h>
#include "deskicons.h"

static int fails;

static void check(const char *what, int ok)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

int main(void)
{
    char buf[16];

    /* what the page and OpenBench write, read back */
    check("parse: \"none\" is no icons", deskicons_parse("none", -1) == DESKICONS_NONE);
    check("parse: \"show\" is the icons", deskicons_parse("show", -1) == DESKICONS_SHOW);
    check("text: each setting's word reads back as itself",
          deskicons_parse(deskicons_text(DESKICONS_NONE), -1) == DESKICONS_NONE &&
          deskicons_parse(deskicons_text(DESKICONS_SHOW), -1) == DESKICONS_SHOW);
    check("text: the words are \"none\" and \"show\"", !strcmp(deskicons_text(DESKICONS_NONE), "none") &&
          !strcmp(deskicons_text(DESKICONS_SHOW), "show") && !strcmp(deskicons_text(7), "show"));

    /* no variable, or nothing in it: the icons */
    check("parse: no variable (NULL) is the icons", deskicons_parse(NULL, -1) == DESKICONS_SHOW);
    check("parse: an empty variable is the icons", deskicons_parse("", -1) == DESKICONS_SHOW &&
          deskicons_parse("none", 0) == DESKICONS_SHOW);
    check("parse: only spaces and a line end is the icons", deskicons_parse("  \t\n", -1) == DESKICONS_SHOW);

    /* the way a file in ENVARC: may hold it */
    check("parse: a line end after the word", deskicons_parse("none\n", -1) == DESKICONS_NONE &&
          deskicons_parse("none\r\n", -1) == DESKICONS_NONE);
    check("parse: spaces before it", deskicons_parse("   none", -1) == DESKICONS_NONE);
    check("parse: letters in either case", deskicons_parse("NONE", -1) == DESKICONS_NONE &&
          deskicons_parse("None", -1) == DESKICONS_NONE && deskicons_parse("SHOW", -1) == DESKICONS_SHOW);
    check("parse: only the first word counts", deskicons_parse("none show", -1) == DESKICONS_NONE &&
          deskicons_parse("show none", -1) == DESKICONS_SHOW);
    check("parse: the length given, not the NUL (GetVar's count)", deskicons_parse("nonexyz", 4) == DESKICONS_NONE &&
          deskicons_parse("none", 3) == DESKICONS_SHOW);

    /* the other words the Shell takes (OpenPrefs ICONS DESKTOP ...) */
    check("word: show, icons, on, yes are the icons", deskicons_word("show", -1) == DESKICONS_SHOW &&
          deskicons_word("icons", -1) == DESKICONS_SHOW && deskicons_word("ON", -1) == DESKICONS_SHOW &&
          deskicons_word("yes", -1) == DESKICONS_SHOW);
    check("word: none, noicons, off, no, hide are no icons", deskicons_word("none", -1) == DESKICONS_NONE &&
          deskicons_word("NoIcons", -1) == DESKICONS_NONE && deskicons_word("off", -1) == DESKICONS_NONE &&
          deskicons_word("no", -1) == DESKICONS_NONE && deskicons_word("Hide", -1) == DESKICONS_NONE);
    check("word: a word not known is -1 (the Shell's argument changes nothing)", deskicons_word("maybe", -1) == -1 &&
          deskicons_word("nonee", -1) == -1 && deskicons_word("non", -1) == -1 && deskicons_word("", -1) == -1 &&
          deskicons_word(NULL, -1) == -1);
    check("word: a word longer than any known is -1, not cut to one", deskicons_word("noiconsatallplease", -1) == -1 &&
          deskicons_word("nonenonenonenone", -1) == -1);

    /* a damaged variable never hides the icons */
    check("parse: a word not known is the icons", deskicons_parse("maybe", -1) == DESKICONS_SHOW &&
          deskicons_parse("n0ne", -1) == DESKICONS_SHOW);
    memset(buf, 'n', sizeof buf);
    check("parse: no NUL within the length given", deskicons_parse(buf, (long)sizeof buf) == DESKICONS_SHOW);
    memcpy(buf, "none", 4);
    buf[4] = 0;
    check("parse: a NUL inside the length ends the word", deskicons_parse(buf, (long)sizeof buf) == DESKICONS_NONE);

    printf("%s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
