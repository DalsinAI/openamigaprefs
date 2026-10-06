/* sp_test: the Sound editor's test sounds, and AHI's mode names (sp_test.c).
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef SP_TEST_H
#define SP_TEST_H

#include <exec/types.h>

/* Half a second of a tone; 1 when it played. */
int sp_test_paula(int volume_pct);
int sp_test_ahi(int unit);
/* AHI's audio modes, from ahi.device: how many (0 without AHI). */
int sp_ahi_modes(ULONG *ids, char names[][48], int max);

#endif
