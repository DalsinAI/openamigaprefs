/* sp_test: the Sound editor's test sounds, and AHI's mode names.
 *
 *   Paula: half a second of a tone on one of Paula's channels, through
 *          audio.device, as any program plays Paula.
 *   AHI:   half a second of a tone through ahi.device (unit 0), so it goes
 *          wherever unit 0's mode says: the ACAHI board, a sound card, or
 *          Paula through AHI.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/memory.h>
#include <devices/audio.h>
#include <devices/ahi.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/ahi.h>
#include <clib/alib_protos.h>

#include <string.h>
#include <stdio.h>

#include "sp_test.h"

struct Library *AHIBase;
extern struct GfxBase *GfxBase;

/* ---- Paula ------------------------------------------------------------------------------- */

int sp_test_paula(int volume_pct)
{
    static UBYTE whichannel[] = { 1, 2, 4, 8 };
    struct MsgPort *port;
    struct IOAudio *io;
    BYTE *wave;
    int ok = 0;
    ULONG clock = (GfxBase->DisplayFlags & PAL) ? 3546895UL : 3579545UL;
    if (!(port = CreateMsgPort())) return 0;
    if (!(io = (struct IOAudio *)CreateIORequest(port, sizeof *io))) { DeleteMsgPort(port); return 0; }
    if (!(wave = AllocVec(32, MEMF_CHIP | MEMF_CLEAR))) { DeleteIORequest((struct IORequest *)io); DeleteMsgPort(port); return 0; }
    for (int i = 0; i < 32; i++) wave[i] = i < 16 ? 90 : -90;      /* a soft square: 32 samples a cycle */
    io->ioa_Request.io_Message.mn_Node.ln_Pri = 0;
    io->ioa_Data = whichannel;
    io->ioa_Length = sizeof whichannel;
    if (!OpenDevice((STRPTR)AUDIONAME, 0, (struct IORequest *)io, 0)) {
        io->ioa_Request.io_Command = CMD_WRITE;
        io->ioa_Request.io_Flags = ADIOF_PERVOL;
        io->ioa_Data = (UBYTE *)wave;
        io->ioa_Length = 32;
        io->ioa_Period = (UWORD)(clock / (440UL * 32));
        io->ioa_Volume = (UWORD)(volume_pct * 64 / 100);
        io->ioa_Cycles = 220;                                        /* half a second at 440 Hz */
        BeginIO((struct IORequest *)io);                             /* not SendIO, which clears io_Flags and so ADIOF_PERVOL: silent */
        WaitIO((struct IORequest *)io);
        ok = 1;
        CloseDevice((struct IORequest *)io);
    }
    FreeVec(wave);
    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);
    return ok;
}

/* ---- AHI --------------------------------------------------------------------------------- */

#define RATE 44100
#define FRAMES (RATE / 2)

int sp_test_ahi(int unit)
{
    struct MsgPort *port;
    struct AHIRequest *io;
    WORD *buf;
    int ok = 0;
    if (!(port = CreateMsgPort())) return 0;
    if (!(io = (struct AHIRequest *)CreateIORequest(port, sizeof *io))) { DeleteMsgPort(port); return 0; }
    io->ahir_Version = 4;
    if (!OpenDevice((STRPTR)AHINAME, (ULONG)unit, (struct IORequest *)io, 0)) {
        if ((buf = AllocVec(FRAMES * 2, MEMF_ANY))) {
            for (ULONG i = 0; i < FRAMES; i++) {                       /* 441 Hz: 100 samples a cycle, a soft triangle */
                LONG t = (LONG)(i % 100);
                buf[i] = (WORD)((t < 50 ? t * 2 - 50 : 150 - t * 2) * 200);
            }
            io->ahir_Std.io_Message.mn_Node.ln_Pri = 0;
            io->ahir_Std.io_Command = CMD_WRITE;
            io->ahir_Std.io_Data = buf;
            io->ahir_Std.io_Length = FRAMES * 2;
            io->ahir_Std.io_Offset = 0;
            io->ahir_Type = AHIST_M16S;
            io->ahir_Frequency = RATE;
            io->ahir_Volume = 0x10000;
            io->ahir_Position = 0x8000;
            io->ahir_Link = NULL;
            ok = DoIO((struct IORequest *)io) == 0;
            FreeVec(buf);
        }
        CloseDevice((struct IORequest *)io);
    }
    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);
    return ok;
}

/* ---- AHI's modes --------------------------------------------------------------------------- */

int sp_ahi_modes(ULONG *ids, char names[][48], int max)
{
    struct MsgPort *port;
    struct AHIRequest *io;
    int n = 0;
    if (!(port = CreateMsgPort())) return 0;
    if (!(io = (struct AHIRequest *)CreateIORequest(port, sizeof *io))) { DeleteMsgPort(port); return 0; }
    io->ahir_Version = 4;
    if (!OpenDevice((STRPTR)AHINAME, AHI_NO_UNIT, (struct IORequest *)io, 0)) {
        ULONG id = AHI_INVALID_ID;
        AHIBase = (struct Library *)io->ahir_Std.io_Device;
        while (n < max && (id = AHI_NextAudioID(id)) != AHI_INVALID_ID) {
            ids[n] = id;
            names[n][0] = 0;
            AHI_GetAudioAttrs(id, NULL, AHIDB_Name, (ULONG)names[n], AHIDB_BufferLen, 48, TAG_DONE);
            if (!names[n][0]) snprintf(names[n], 48, "Mode %08lx", (unsigned long)id);
            n++;
        }
        AHIBase = NULL;
        CloseDevice((struct IORequest *)io);
    }
    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);
    return n;
}
