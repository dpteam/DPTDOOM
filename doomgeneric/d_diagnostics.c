// DPT.DOOM deterministic test instrumentation, headless builds only.
// GPL version 2 or, at your option, any later version. See LICENSE.
#include "d_diagnostics.h"
#include "dpt_net.h"
#include "dpt_protocol.h"
#include "dpt_rules.h"
#include "doomstat.h"
#include "g_game.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_random.h"
#include "p_local.h"
#include "p_saveg.h"
#include "r_state.h"
#include "r_main.h"
#include "deh_classic.h"
#include "deh_misc.h"
#include "deh_str.h"
#include "d_englsh.h"
#include "d_items.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

static unsigned int tic_limit, save_tic, ticks;
static FILE *trace_file;
static FILE *input_file;
static boolean enabled;
static boolean rules_probe;
extern int prndindex;
extern boolean sendsave;
extern int cpars[32];
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
    if (input_file) { fclose(input_file); input_file = NULL; }
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
    rules_probe = M_ParmExists("-headless-rules-probe");
    {
        // Testing aid: a scripted run cannot move the mouse, so the vertical
        // aim needs a way to start pitched. Diagnostics-only, like the rest of
        // this module; the value is clamped by the normal pitch path.
        int pitch = M_CheckParm("-dpt-pitch");
        if (pitch)
        {
            if (pitch + 1 >= myargc) I_Error("-dpt-pitch requires a value");
            dpt_view_pitch = atoi(myargv[pitch + 1]);
        }
    }
    {
        int input = M_CheckParm("-headless-input");
        if (input)
        {
            if (input + 1 >= myargc || myargv[input + 1][0] == '-')
                I_Error("-headless-input requires an input path");
            input_file = fopen(myargv[input + 1], "rb");
            if (!input_file) I_Error("Cannot open diagnostic input");
            I_AtExit(CloseTrace, true);
        }
    }
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
    enabled = trace_file != NULL || tic_limit || save_tic || rules_probe;
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

void D_DiagnosticsCommand(ticcmd_t *cmd, int tic)
{
    byte record[8];
    byte consistency = cmd->consistancy;
    size_t length;
    (void)tic;
    if (!input_file) return;
    length = fread(record, 1, sizeof(record), input_file);
    if (!length && !ferror(input_file))
    {
        memset(cmd, 0, sizeof(*cmd));
        cmd->consistancy = consistency;
        return;
    }
    if (length != sizeof(record) || !DPT_DecodeCommand(record, cmd))
        I_Error("Invalid/truncated diagnostic input command");
    cmd->consistancy = consistency;
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
    // Keep existing single-player baseline schema unchanged at flags=0.
    if (dpt_dmflags) hash = Add32(hash, dpt_dmflags);
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
        if (rules_probe && ticks == 1)
        {
            thinker_t *th;
            unsigned int weapons = 0, extra_weapons = 0;
            boolean overlap;
            if (!DPT_NetActive() || !players[0].mo || !players[1].mo)
                I_Error("Rules probe requires direct coop with two spawned players");
            for (th = thinkercap.next; th != &thinkercap; th = th->next)
                if (th->function.acp1 == (actionf_p1)P_MobjThinker)
                {
                    mobj_t *mo = (mobj_t *)th;
                    if (DPT_WeaponMapType(mobjinfo[mo->type].doomednum))
                    {
                        ++weapons;
                        if (mo->spawnpoint.options & 16) ++extra_weapons;
                    }
                }
            // Query actual movement clipping at the other player's valid spot.
            // Disable pickup side effects for this diagnostic-only query.
            {
                int flags = players[0].mo->flags;
                players[0].mo->flags &= ~MF_PICKUP;
                overlap = P_CheckPosition(players[0].mo, players[1].mo->x, players[1].mo->y);
                players[0].mo->flags = flags;
            }
            printf("Rules probe: dmflags=%u weapons=%u extra-weapons=%u player-overlap=%s\n",
                dpt_dmflags, weapons, extra_weapons, overlap ? "pass" : "block");
            fflush(stdout);
        }
        archive = (trace_file || ticks == save_tic)
            ? (DPT_NetActive() ? P_NetArchiveChecksum() : P_ArchiveChecksum()) : 0;
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
        if (M_ParmExists("-headless-session-report"))
        {
            player_t *p = &players[consoleplayer];
            printf("Session report: state=%d player=%d leveltime=%d flags=%u mask=%u%u paused=%u x=%d y=%d health=%d ammo=%d\n",
                gamestate, consoleplayer + 1, leveltime, dpt_dmflags,
                (unsigned int)playeringame[0], (unsigned int)playeringame[1], (unsigned int)paused,
                 p->mo ? p->mo->x : 0, p->mo ? p->mo->y : 0, p->health, p->ammo[am_clip]);
            {
                // Boom/MBF sky transfer state, derived from map linedefs.
                int si,count=0,first=-1;
                for (si=0;si<numsectors;++si)
                    if (sectors[si].sky)
                    {++count;if(first<0)first=si;}
                printf("Map sky: count=%d first=%d:%d\n",
                    count,first,first>=0?sectors[first].sky:0);
            }
            if (DPT_DehLoadedCount())
            {
                printf("Patch report: maxammo=%d,%d,%d,%d ammo=%d,%d,%d,%d perammo=%d,%d,%d,%d pistol-ammo=%d\n",
                    p->maxammo[0],p->maxammo[1],p->maxammo[2],p->maxammo[3],
                    p->ammo[0],p->ammo[1],p->ammo[2],p->ammo[3],
                    clipammo[0],clipammo[1],clipammo[2],clipammo[3],(int)weaponinfo[wp_pistol].ammo);
                printf("Patch text: armor=%s saved=%s msgon=%s level=%s\n",DPT_DehString(GOTARMOR),DPT_DehString(GGSAVED),DPT_DehString(MSGON),DPT_DehString(HUSTR_1));
                printf("Patch misc: infight=%d par1=%d par32=%d\n",deh_species_infighting,cpars[0],cpars[31]);                if(p->psprites[ps_weapon].state)
                    printf("Patch weapon visual: sprite=%d frame=%d sx=%d sy=%d\n",
                        (int)p->psprites[ps_weapon].state->sprite,p->psprites[ps_weapon].state->frame,
                        p->psprites[ps_weapon].sx,p->psprites[ps_weapon].sy);
                // Report an actual spawned entity, not just the patched table.
                // Entity fields are copied at spawn, while speed/mass/damage
                // are consumed through info during gameplay.
                {
                    thinker_t *th;
                    for (th=thinkercap.next;th!=&thinkercap;th=th->next)
                        if (th->function.acp1==(actionf_p1)P_MobjThinker)
                        {
                            mobj_t *mo=(mobj_t*)th;
                            if(mo->type==MT_BARREL)
                            {
                                printf("Patch thing: type=%d id=%d flags=%d state=%d death=%d sounds=%d,%d,%d,%d,%d health=%d radius=%d height=%d speed=%d mass=%d damage=%d pain=%d reaction=%d\n",
                                    (int)mo->type,mo->info->doomednum,mo->flags,(int)(mo->state-states),mo->info->deathstate,mo->info->seesound,mo->info->attacksound,mo->info->painsound,mo->info->deathsound,mo->info->activesound,mo->health,mo->radius,mo->height,mo->info->speed,
                                    mo->info->mass,mo->info->damage,mo->info->painchance,mo->reactiontime);
                                printf("Patch visual: type=%d sprite=%d frame=%d action=%s\n",(int)mo->type,(int)mo->sprite,mo->frame,DPT_DehActionName(&mo->state->action));
                                break;
                            }
                        }
                }
            }
        }
        // Explicit bounded test run is success, not timedemo's I_Error report.
        // A bounded recording still has to be finalized into a real file.
        if (demorecording)
            G_CheckDemoStatus();
        timingdemo = false;
        demoplayback = false;
        I_Quit();
    }
}
