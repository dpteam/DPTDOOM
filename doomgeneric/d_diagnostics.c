// DPT.DOOM deterministic test instrumentation, headless builds only.
// GPL version 2 or, at your option, any later version. See LICENSE.
#include "d_diagnostics.h"
#include "doomstat.h"
#include "g_game.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_random.h"
#include "p_local.h"
#include "p_saveg.h"
#include "r_state.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

static unsigned int tic_limit, save_tic, ticks;
static FILE *trace_file;
static boolean enabled;
extern int prndindex;
extern boolean sendsave;
void G_DoSaveGame(void);

static unsigned int PositiveArg(char *name)
{
    int p = M_CheckParm(name);
    char *end;
    unsigned long value;
    if (!p) return 0;
    if (p + 1 >= myargc) I_Error("%s requires a positive integer", name);
    errno = 0;
    value = strtoul(myargv[p + 1], &end, 10);
    if (errno || end == myargv[p + 1] || *end || myargv[p + 1][0] == '-'
        || !value || value > INT_MAX)
        I_Error("%s requires a positive integer", name);
    return (unsigned int)value;
}

static void CloseTrace(void)
{
    if (trace_file)
    {
        FILE *file = trace_file;
        trace_file = NULL;
        if (fclose(file)) I_Error("Failed closing diagnostic trace");
    }
}

void D_DiagnosticsInit(void)
{
    int p = M_CheckParm("-headless-trace");
    tic_limit = PositiveArg("-headless-tics");
    save_tic = PositiveArg("-headless-save-tic");
    if (p)
    {
        if (p + 1 >= myargc || myargv[p + 1][0] == '-')
            I_Error("-headless-trace requires an output path");
        trace_file = fopen(myargv[p + 1], "w");
        if (!trace_file) I_Error("Cannot open diagnostic trace: %s", myargv[p + 1]);
        if (fprintf(trace_file, "# dptdoom-state-v1: tick gametic leveltime archive-fnv1a sim-fnv1a\n") < 0)
            I_Error("Failed writing diagnostic trace");
        I_AtExit(CloseTrace, true);
    }
    enabled = trace_file != NULL || tic_limit || save_tic;
}

static uint32_t Add32(uint32_t hash, uint32_t value)
{
    int i;
    for (i = 0; i < 4; ++i)
    {
        hash = (hash ^ (value & 255)) * 16777619u;
        value >>= 8;
    }
    return hash;
}

// Address-independent IDs in thinker-list order. Diagnostic only, not wire IDs.
static unsigned int MobjID(const mobj_t *object)
{
    thinker_t *th;
    unsigned int id = 0;
    if (!object) return 0;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function.acp1 == (actionf_p1)P_MobjThinker)
        {
            ++id;
            if ((const void *)th == (const void *)object) return id;
        }
    return UINT_MAX; // dangling/pending-removal reference: never dereference it
}

static uint32_t SimulationChecksum(uint32_t archive)
{
    thinker_t *th;
    int i;
    uint32_t hash = Add32(2166136261u, archive);
    hash = Add32(hash, prndindex);
    hash = Add32(hash, gamestate);
    hash = Add32(hash, respawnparm);
    hash = Add32(hash, fastparm);
    hash = Add32(hash, nomonsters);
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function.acp1 == (actionf_p1)P_MobjThinker)
        {
            mobj_t *mo = (mobj_t *)th;
            hash = Add32(hash, mo->floorz);
            hash = Add32(hash, mo->ceilingz);
            hash = Add32(hash, MobjID(mo->target));
            hash = Add32(hash, MobjID(mo->tracer));
        }
    for (i = 0; i < numsectors; ++i)
    {
        hash = Add32(hash, sectors[i].floorheight);
        hash = Add32(hash, sectors[i].ceilingheight);
        hash = Add32(hash, MobjID(sectors[i].soundtarget));
    }
    for (i = 0; i < numsides; ++i)
    {
        hash = Add32(hash, sides[i].textureoffset);
        hash = Add32(hash, sides[i].rowoffset);
    }
    for (i = 0; i < MAXBUTTONS; ++i)
    {
        button_t *b = &buttonlist[i];
        hash = Add32(hash, b->btimer);
        if (b->btimer)
        {
            hash = Add32(hash, b->line ? (unsigned int)(b->line - lines) + 1 : 0);
            hash = Add32(hash, b->where);
            hash = Add32(hash, b->btexture);
        }
    }
    return hash;
}

void D_DiagnosticsLoaded(void)
{
    if (enabled)
        printf("Diagnostics: loaded archive-fnv1a-v1=%08x leveltime=%d\n",
            P_ArchiveChecksum(), leveltime);
}

void D_DiagnosticsTic(void)
{
    unsigned int archive;
    if (!enabled) return;
    ++ticks;
    if (gamestate == GS_LEVEL)
    {
        archive = (trace_file || ticks == save_tic) ? P_ArchiveChecksum() : 0;
        if (trace_file && fprintf(trace_file, "%u %d %d %08x %08x\n", ticks, gametic,
                leveltime, archive, SimulationChecksum(archive)) < 0)
            I_Error("Failed writing diagnostic trace");
        if (ticks == save_tic)
        {
            // Snapshot exactly at this completed tic, not through asynchronous
            // input delivery. Exercise the same serializer/commit as the menu.
            G_SaveGame(0, "DPT.DOOM TEST");
            sendsave = false;
            G_DoSaveGame();
            printf("Diagnostics: saved slot=0 archive-fnv1a-v1=%08x leveltime=%d\n",
                archive, leveltime);
        }
    }
    else if (ticks == save_tic)
        I_Error("Diagnostic checkpoint requires an active level");
    if (tic_limit && ticks >= tic_limit)
    {
        // Explicit bounded test run is success, not timedemo's I_Error report.
        timingdemo = false;
        demoplayback = false;
        I_Quit();
    }
}
