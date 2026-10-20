// DPT.DOOM bounded classic subset, written for this port.
// No imported ACE/csDoom code, BEX, pointers or executable offsets.
// GPL-2.0-or-later; see LICENSE.
#include "deh_classic.h"
#include "deh_misc.h"
#include "w_wad.h"
#include "d_items.h"
#include "info.h"
#include "m_fixed.h"
#include "p_mobj.h"
#include "p_pspr.h"
#include "sounds.h"
#include "d_englsh.h"
#include "deh_str.h"
#include "deh_actions.h"
#include "doomstat.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

enum { MAX_LINE=2047, MAX_SOURCE=262144, MAX_PATCHES=16, FIELD_COUNT=16,
       TEXT_MAX=2047, TEXT_COUNT=57 };
#define MISC_FIELDS(X) \
    X("Initial Health",initial_health,100,1,10000) \
    X("Initial Bullets",initial_bullets,50,0,200) \
    X("Max Health",max_health,200,1,10000) \
    X("Max Armor",max_armor,200,0,10000) \
    X("Green Armor Class",green_armor_class,1,1,2) \
    X("Blue Armor Class",blue_armor_class,2,1,2) \
    X("Max Soulsphere",max_soulsphere,200,1,10000) \
    X("Soulsphere Health",soulsphere_health,100,0,10000) \
    X("Megasphere Health",megasphere_health,200,1,10000) \
    X("God Mode Health",god_mode_health,100,1,10000) \
    X("IDFA Armor",idfa_armor,200,0,10000) \
    X("IDFA Armor Class",idfa_armor_class,2,1,2) \
    X("IDKFA Armor",idkfa_armor,200,0,10000) \
    X("IDKFA Armor Class",idkfa_armor_class,2,1,2) \
    X("BFG Cells/Shot",bfg_cells_per_shot,40,1,1000) \
    X("Monsters Infight",species_infighting,0,0,1)
#define DEFINE(name,field,def,low,high) int deh_##field=def;
MISC_FIELDS(DEFINE)
#undef DEFINE
typedef struct { const char *name; int *destination, initial, low, high; } misc_field_t;
#define ENTRY(name,field,def,low,high) {name,&deh_##field,def,low,high},
static const misc_field_t fields[FIELD_COUNT]={MISC_FIELDS(ENTRY)};
#undef ENTRY
static char last_error[256];
static unsigned int loaded,bytes_total;
static sha1_context_t digest_context;
static boolean initialized;
extern int maxammo[NUMAMMO],clipammo[NUMAMMO];
static int base_maxammo[NUMAMMO],base_clipammo[NUMAMMO];
static weaponinfo_t base_weapons[NUMWEAPONS];
static mobjinfo_t base_things[NUMMOBJTYPES];
// Parser startup is single-threaded. Keep staged tables off the small C stack;
// unsupported states/flags/sounds never receive an assignment.
static mobjinfo_t staged_things[NUMMOBJTYPES];
// Stage supported state fields only, never action pointers or misc payloads.
static int base_tics[NUMSTATES],staged_tics[NUMSTATES];
static statenum_t base_nextstate[NUMSTATES],staged_nextstate[NUMSTATES];
static spritenum_t base_sprite[NUMSTATES],staged_sprite[NUMSTATES];
static int base_frame[NUMSTATES],staged_frame[NUMSTATES];
static boolean visual_patched[NUMSTATES],staged_visual_patched[NUMSTATES];
// Psprite coordinate offsets. Vanilla only consumes these on weapon states.
static int base_misc1[NUMSTATES],staged_misc1[NUMSTATES];
static int base_misc2[NUMSTATES],staged_misc2[NUMSTATES];
static boolean base_weapon_state[NUMSTATES];
static weaponinfo_t staged_weapons[NUMWEAPONS];
typedef struct { const char *original; char replacement[TEXT_MAX+1]; boolean present; } text_replacement_t;
// Classic Text matches the original bytes; BEX [STRINGS] matches these names.
// A NULL name means the string is reachable through classic Text only: the BEX
// key for it is not verified in our reference sources, and guessing one would
// accept a patch that silently targets nothing.
static const char *const text_names[TEXT_COUNT]={
    "GOTARMOR","GOTMEGA","GOTHTHBONUS","GOTARMBONUS","GOTSTIM","GOTMEDINEED",
    "GOTMEDIKIT","GOTSUPER","GOTBLUECARD","GOTYELWCARD","GOTREDCARD","GOTBLUESKUL",
    "GOTYELWSKUL","GOTREDSKULL","GGSAVED","HUSTR_MSGU","MSGON","MSGOFF",
    "DETAILHI","DETAILLO","GAMMALVL0","GAMMALVL1","GAMMALVL2","GAMMALVL3",
    "GAMMALVL4",
    // Doom II level names: classic Text only.
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL
};
static const char *const text_originals[TEXT_COUNT]={
    GOTARMOR,GOTMEGA,GOTHTHBONUS,GOTARMBONUS,GOTSTIM,GOTMEDINEED,
    GOTMEDIKIT,GOTSUPER,GOTBLUECARD,GOTYELWCARD,GOTREDCARD,GOTBLUESKUL,
    GOTYELWSKUL,GOTREDSKULL,GGSAVED,HUSTR_MSGU,MSGON,MSGOFF,
    DETAILHI,DETAILLO,GAMMALVL0,GAMMALVL1,GAMMALVL2,GAMMALVL3,
    GAMMALVL4,
    HUSTR_1,HUSTR_2,HUSTR_3,HUSTR_4,HUSTR_5,HUSTR_6,HUSTR_7,HUSTR_8,
    HUSTR_9,HUSTR_10,HUSTR_11,HUSTR_12,HUSTR_13,HUSTR_14,HUSTR_15,HUSTR_16,
    HUSTR_17,HUSTR_18,HUSTR_19,HUSTR_20,HUSTR_21,HUSTR_22,HUSTR_23,HUSTR_24,
    HUSTR_25,HUSTR_26,HUSTR_27,HUSTR_28,HUSTR_29,HUSTR_30,HUSTR_31,HUSTR_32
};
static text_replacement_t text_replacements[TEXT_COUNT],staged_text[TEXT_COUNT];
// BEX [PARS] writes the same tables G_DoCompleted reads. Episode 4 has no row
// in pars[][], so the engine deliberately falls through to cpars[gamemap];
// the parser mirrors that exact lookup instead of inventing a fourth row.
extern int pars[4][10],cpars[32];
static int base_pars[4][10],staged_pars[4][10];
static int base_cpars[32],staged_cpars[32];
// Action allowlist. P_SetMobjState calls action.acp1(mobj) while P_SetPsprite
// calls action.acp2(player, psp). Assigning across those classes would pass a
// pspdef_t where a mobj_t is expected (or vice versa), so each entry records
// which calling convention it belongs to and the parser enforces it.
static actionf_t base_action[NUMSTATES],staged_action[NUMSTATES];
static boolean action_patched[NUMSTATES],staged_action_patched[NUMSTATES];
typedef struct { const char *name; size_t offset; int low, high; } thing_field_t;
#define THING(name,field,low,high) {name,offsetof(mobjinfo_t,field),low,high}
static const thing_field_t thing_fields[]={
    THING("ID #",doomednum,-1,32767),
    THING("Hit points",spawnhealth,1,10000),
    THING("Reaction time",reactiontime,0,10000),
    THING("Pain chance",painchance,0,255),
    THING("Width",radius,1,128*FRACUNIT),
    THING("Height",height,1,256*FRACUNIT),
    THING("Mass",mass,1,1000000),
    THING("Missile damage",damage,0,100)
};
#undef THING
static const int thing_spawn_flags=
    MF_SPECIAL|MF_SOLID|MF_SHOOTABLE|MF_NOSECTOR|MF_NOBLOCKMAP|MF_AMBUSH|
    MF_SPAWNCEILING|MF_NOGRAVITY|MF_DROPOFF|MF_PICKUP|MF_NOCLIP|MF_SLIDE|
    MF_FLOAT|MF_TELEPORT|MF_MISSILE|MF_DROPPED|MF_SHADOW|MF_NOBLOOD|
    MF_CORPSE|MF_INFLOAT|MF_COUNTKILL|MF_COUNTITEM|MF_SKULLFLY|MF_NOTDMATCH;

static boolean Error(unsigned int line,const char *message)
{
    snprintf(last_error,sizeof(last_error),"line %u: %s",line,message);
    return false;
}
boolean DPT_DehValidateSpriteFrames(const int *frame_counts,size_t count)
{
    unsigned int i;
    for(i=1;i<NUMSTATES;++i)
    {
        int sprite=(int)states[i].sprite,frame=states[i].frame&FF_FRAMEMASK;
        if(!visual_patched[i])continue;
        if(!frame_counts||sprite<0||(size_t)sprite>=count||frame>=frame_counts[sprite])
        {
            snprintf(last_error,sizeof(last_error),"Frame %u: sprite %d frame %d is absent from loaded sprite resources",i,sprite,frame);
            return false;
        }
    }
    return true;
}
static int Equal(const char *a,const char *b)
{
    while(*a&&*b)if(tolower((unsigned char)*a++)!=tolower((unsigned char)*b++))return 0;
    return *a==*b;
}
static char *Trim(char *text)
{
    char *end;
    while(*text&&isspace((unsigned char)*text))++text;
    end=text+strlen(text);
    while(end>text&&isspace((unsigned char)end[-1]))*--end=0;
    return text;
}
static boolean Number(const char *text,int *value)
{
    const char *p=text; char *end; long number;
    if(*p=='-')++p;
    if(!*p)return false;
    while(*p){if(*p<'0'||*p>'9')return false;++p;}
    errno=0;number=strtol(text,&end,10);
    if(errno==ERANGE||*end||number<INT_MIN||number>INT_MAX)return false;
    *value=(int)number;return true;
}
static boolean IsToken(const char *text,const char *token)
{
    while(*token&&*text&&tolower((unsigned char)*text)==tolower((unsigned char)*token))
        ++text,++token;
    return !*token&&(!*text||isspace((unsigned char)*text));
}
static boolean TextHeader(char *text,int *old_length,int *new_length)
{
    char *p=Trim(text),*q;
    if(!IsToken(p,"Text"))return false;
    while(*p&&!isspace((unsigned char)*p))++p;
    if(!*p)return false;
    p=Trim(p);q=p;while(*q&&!isspace((unsigned char)*q))++q;
    if(*q)*q++=0;
    if(!Number(p,old_length))return false;
    p=Trim(q);q=p;while(*q&&!isspace((unsigned char)*q))++q;
    if(*q)*q++=0;
    if(!Number(p,new_length)||*Trim(q))return false;
    return true;
}
static int TextSlot(const unsigned char *old,int length)
{
    unsigned int i;
    for(i=0;i<TEXT_COUNT;++i)
        if((int)strlen(text_originals[i])==length&&!memcmp(old,text_originals[i],(size_t)length))return (int)i;
    return -1;
}
static boolean ValidTextBytes(const unsigned char *text,int length)
{
    int i;
    for(i=0;i<length;++i)
        if(text[i]=='%'||(text[i]<32&&text[i]!='\n'&&text[i]!='\r'&&text[i]!='\t')||text[i]>126)
            return false;
    return true;
}
// BEX [STRINGS] assignment: NAME = value, value already trimmed in place.
static boolean ApplyBexString(const char *name,const char *value,
    text_replacement_t *targets,unsigned int line)
{
    unsigned int i;
    size_t length=strlen(value);
    for(i=0;i<TEXT_COUNT;++i)
        if(text_names[i]&&Equal(name,text_names[i]))break;
    if(i==TEXT_COUNT)return Error(line,"unknown [STRINGS] name (not in the bounded whitelist)");
    if(length>TEXT_MAX)return Error(line,"[STRINGS] value exceeds 2047 bytes");
    // BEX allows a trailing backslash to continue the value on the next line.
    // This parser is line-oriented, so reject it instead of storing a stray
    // backslash that would silently differ from the reference behaviour.
    if(length&&value[length-1]=='\\')
        return Error(line,"[STRINGS] multi-line continuation is not supported");
    if(!ValidTextBytes((const unsigned char *)value,(int)length))
        return Error(line,"[STRINGS] value contains unsafe bytes or format marker");
    memset(targets[i].replacement,0,sizeof(targets[i].replacement));
    memcpy(targets[i].replacement,value,length);
    targets[i].original=text_originals[i];
    targets[i].present=true;
    return true;
}
// BEX [PARS]: "par <map> <seconds>" for commercial, "par <ep> <map> <seconds>"
// for episode builds. Token count decides the form, exactly like the reference
// parser, but the form must also match the loaded IWAD so an assignment can
// never be silently inert.
static boolean ApplyPar(char *rest,unsigned int line)
{
    char *token[3];int count=0;char *p=Trim(rest);
    while(*p&&count<3)
    {
        char *start=p;
        while(*p&&!isspace((unsigned char)*p))++p;
        if(*p)*p++=0;
        token[count++]=start;
        p=Trim(p);
    }
    if(*p)return Error(line,"[PARS] par accepts at most three numbers");
    if(count==2)
    {
        int map,par;
        if(!Number(token[0],&map)||!Number(token[1],&par))
            return Error(line,"[PARS] par values must be decimal integers");
        if(gamemode!=commercial)
            return Error(line,"[PARS] MAP?? form requires a commercial IWAD");
        if(map<1||map>32)return Error(line,"[PARS] MAP number must be 1..32");
        if(par<0||par>65535)return Error(line,"[PARS] par time must be 0..65535 seconds");
        staged_cpars[map-1]=par;return true;
    }
    if(count==3)
    {
        int episode,map,par;
        if(!Number(token[0],&episode)||!Number(token[1],&map)||!Number(token[2],&par))
            return Error(line,"[PARS] par values must be decimal integers");
        if(gamemode==commercial)
            return Error(line,"[PARS] E?M? form requires a non-commercial IWAD");
        if(episode<1||episode>4)return Error(line,"[PARS] episode must be 1..4");
        if(map<1||map>9)return Error(line,"[PARS] map must be 1..9");
        if(par<0||par>65535)return Error(line,"[PARS] par time must be 0..65535 seconds");
        // Vanilla E4 par lookup reads cpars[gamemap]; keep that quirk visible.
        if(episode==4)staged_cpars[map]=par;
        else staged_pars[episode][map]=par;
        return true;
    }
    return Error(line,"[PARS] par requires <map> <time> or <episode> <map> <time>");
}
// BEX [CODEPTR]: Frame <n> = A_Name, restricted to the allowlist above. The
// reference parser also accepts the bare name without the "A_" prefix.
static boolean ApplyCodePtr(char *key,const char *value,unsigned int line)
{
    unsigned int i;
    int index;
    char *rest=key;
    const char *name=value;
    if(!IsToken(rest,"Frame"))return Error(line,"[CODEPTR] expected Frame <number> = A_Name");
    while(*rest&&!isspace((unsigned char)*rest))++rest;
    if(!Number(Trim(rest),&index))return Error(line,"[CODEPTR] frame number is not a decimal integer");
    if(index<=S_NULL||index>=NUMSTATES)
        return Error(line,"[CODEPTR] frame out of range (S_NULL is immutable)");
    if(name[0]=='A'&&name[1]=='_')name+=2;
    if(!*name)return Error(line,"[CODEPTR] missing action name");
    for(i=0;i<dpt_action_count;++i)
    {
        const char *entry=dpt_action_entries[i].name;
        // The table stores the canonical "A_Name" form; [CODEPTR] accepts both.
        if(entry[0]=='A'&&entry[1]=='_')entry+=2;
        if(Equal(name,entry))break;
    }
    if(i==dpt_action_count)
        return Error(line,"[CODEPTR] unknown action function (not in the allowlist)");
    if(base_weapon_state[index]!=dpt_action_entries[i].psprite)
        return Error(line,"[CODEPTR] action class does not match this frame (weapon vs mobj)");
    staged_action[index]=dpt_action_entries[i].action;
    staged_action_patched[index]=true;
    return true;
}
char *DPT_DehString(const char *s)
{    unsigned int i;
    if(!s)return (char *)s;
    for(i=0;i<TEXT_COUNT;++i)
        if(text_replacements[i].present&&!strcmp(s,text_replacements[i].original))
            return text_replacements[i].replacement;
    return (char *)s;
}
// Report a state action by allowlist name so tests and diagnostics can prove
// which function was installed instead of only that one exists.
const char *DPT_DehActionName(const actionf_t *action)
{
    unsigned int i;
    if(!action)return "none";
    for(i=0;i<dpt_action_count;++i)
        if(action->acp1==dpt_action_entries[i].action.acp1)return dpt_action_entries[i].name;
    return action->acp1?"custom":"none";
}
static boolean ThingIndex(char *text,int *index)
{
    char *label=strchr(text,'(');
    // Classic editor descriptions are inert labels: Thing 2 (Trooper).
    if(label)
    {
        char *end=strchr(label,')'),*p;
        if(!end||*Trim(end+1)||strchr(label+1,'('))return false;
        for(p=label+1;p<end;++p)if((unsigned char)*p<32||(unsigned char)*p>126)return false;
        *label=0;
    }
    if(!Number(Trim(text),index)||*index<1||*index>NUMMOBJTYPES)return false;
    --*index;return true;
}
static boolean ApplyThing(int index,const char *field,int value,unsigned int line)
{
    unsigned int i;
    int *state_destination=NULL,*sound_destination=NULL;
    if(Equal(field,"Speed"))
    {
        // Vanilla actors use whole units for walking but fixed-point for
        // projectiles/charging skulls. No flags/states can change that category
        // in this subset. Reject zero (missile aim divides by speed).
        int high=base_things[index].speed>=FRACUNIT?64*FRACUNIT:128;
        if(value<1||value>high)return Error(line,"Thing Speed outside supported category/range");
        staged_things[index].speed=value;return true;
    }
    if(Equal(field,"Bits"))
    {
        if(value<0||(value&~thing_spawn_flags))
            return Error(line,"Thing Bits contains unsupported or reserved flags");
        staged_things[index].flags=value;return true;
    }
    if(Equal(field,"Initial frame"))state_destination=&staged_things[index].spawnstate;
    else if(Equal(field,"First moving frame"))state_destination=&staged_things[index].seestate;
    else if(Equal(field,"Injury frame"))state_destination=&staged_things[index].painstate;
    else if(Equal(field,"Close attack frame"))state_destination=&staged_things[index].meleestate;
    else if(Equal(field,"Far attack frame"))state_destination=&staged_things[index].missilestate;
    else if(Equal(field,"Death frame"))state_destination=&staged_things[index].deathstate;
    else if(Equal(field,"Exploding frame"))state_destination=&staged_things[index].xdeathstate;
    else if(Equal(field,"Respawn frame"))state_destination=&staged_things[index].raisestate;
    if(state_destination)
    {
        if(value<0||value>=NUMSTATES)
            return Error(line,"Thing state frame outside the state table");
        if(value!=S_NULL&&base_weapon_state[value])
            return Error(line,"Thing state frame points into the weapon-state pool");
        *state_destination=value;return true;
    }
    if(Equal(field,"Alert sound"))sound_destination=&staged_things[index].seesound;
    else if(Equal(field,"Attack sound"))sound_destination=&staged_things[index].attacksound;
    else if(Equal(field,"Pain sound"))sound_destination=&staged_things[index].painsound;
    else if(Equal(field,"Death sound"))sound_destination=&staged_things[index].deathsound;
    else if(Equal(field,"Action sound"))sound_destination=&staged_things[index].activesound;
    if(sound_destination)
    {
        if(value<0||value>=NUMSFX)
            return Error(line,"Thing sound outside the vanilla sound table");
        *sound_destination=value;return true;
    }
    for(i=0;i<sizeof(thing_fields)/sizeof(thing_fields[0]);++i)
    {
        const thing_field_t *f=&thing_fields[i];
        if(Equal(field,f->name))
        {
            if(value<f->low||value>f->high)return Error(line,"Thing field outside supported safe range");
            *(int*)((unsigned char*)&staged_things[index]+f->offset)=value;return true;
        }
    }
    return Error(line,"unsupported Thing field (IDs/state/sound fields are not supported)");
}
static boolean ValidateThingIds(const mobjinfo_t *things,unsigned int line)
{
    unsigned int i,j;
    for(i=0;i<NUMMOBJTYPES;++i)
    {
        if(things[i].doomednum<0)continue;
        for(j=i+1;j<NUMMOBJTYPES;++j)
            if(things[j].doomednum==things[i].doomednum)
                return Error(line,"Thing ID # duplicates another DoomEd number");
    }
    return true;
}
static boolean ValidateFrameGraph(const int *tics,const statenum_t *next,
    const boolean *next_changed,unsigned int line)
{
    int start;
    // P_SetMobjState/P_SetPsprite follow zero-tic states synchronously. A
    // cycle containing a positive or -1 tic yields first; a cycle made solely
    // of zero-tic states would spin forever.
    for(start=1;start<NUMSTATES;++start)
    {
        int state=start,steps=0;
        if(next_changed[state]&&next[state]!=S_NULL &&
            base_weapon_state[state]!=base_weapon_state[next[state]])
            return Error(line,"Frame transition crosses the weapon/non-weapon state domain");
        while(state!=S_NULL&&tics[state]==0)
        {
            if((int)next[state]<0||(int)next[state]>=NUMSTATES)
                return Error(line,"Frame transition points outside the state table");
            state=(int)next[state];
            if(next_changed[start]&&state!=S_NULL &&
                base_weapon_state[state]!=base_weapon_state[start])
                return Error(line,"Frame transition crosses the weapon/non-weapon state domain");
            if(++steps>NUMSTATES)
                return Error(line,"Frame transitions create a zero-duration cycle");
        }
    }
    return true;
}
static void BuildWeaponStateSet(void)
{
    unsigned int weapon,slot;
    boolean changed=true;
    memset(base_weapon_state,0,sizeof(base_weapon_state));
    for(weapon=0;weapon<NUMWEAPONS;++weapon)
    {
        const int frames[]={base_weapons[weapon].upstate,base_weapons[weapon].downstate,
            base_weapons[weapon].readystate,base_weapons[weapon].atkstate,
            base_weapons[weapon].flashstate};
        for(slot=0;slot<sizeof(frames)/sizeof(frames[0]);++slot)
            if(frames[slot]>=0&&frames[slot]<NUMSTATES)base_weapon_state[frames[slot]]=true;
    }
    while(changed)
    {
        changed=false;
        for(weapon=1;weapon<NUMSTATES;++weapon)
        {
            statenum_t next;
            if(!base_weapon_state[weapon])continue;
            next=base_nextstate[weapon];
            if(next!=S_NULL&&!base_weapon_state[next])
            {base_weapon_state[next]=true;changed=true;}
        }
    }
}
static boolean ApplyWeaponFrame(int weapon,const char *field,int value,unsigned int line)
{
    int *destination=NULL;boolean firing=false;
    if(Equal(field,"Deselect frame"))destination=&staged_weapons[weapon].downstate;
    else if(Equal(field,"Select frame"))destination=&staged_weapons[weapon].upstate;
    else if(Equal(field,"Bobbing frame"))destination=&staged_weapons[weapon].readystate;
    else if(Equal(field,"Shooting frame"))destination=&staged_weapons[weapon].atkstate;
    else if(Equal(field,"Firing frame")){destination=&staged_weapons[weapon].flashstate;firing=true;}
    else return Error(line,"unsupported Weapon field (only Ammo type and frame references)");
    if(value<0||value>=NUMSTATES)return Error(line,"Weapon frame outside the state table");
    if(value==S_NULL)
    {
        if(!firing)return Error(line,"only Firing frame may use S_NULL");
    }
    else if(!base_weapon_state[value])
        return Error(line,"Weapon frame is not in the vanilla weapon-state pool");
    else if(states[value].tics==-1)
        return Error(line,"Weapon frame cannot use an infinite state");
    *destination=value;return true;
}
void DPT_DehReset(void)
{
    unsigned int i;
    if(!initialized)
    {
        memcpy(base_maxammo,maxammo,sizeof(base_maxammo));memcpy(base_clipammo,clipammo,sizeof(base_clipammo));
        memcpy(base_weapons,weaponinfo,sizeof(base_weapons));
        memcpy(base_things,mobjinfo,sizeof(base_things));
        for(i=0;i<NUMSTATES;++i)
        {
            base_tics[i]=states[i].tics;
            base_nextstate[i]=states[i].nextstate;
            base_sprite[i]=states[i].sprite;base_frame[i]=states[i].frame;
            base_misc1[i]=states[i].misc1;base_misc2[i]=states[i].misc2;
            base_action[i]=states[i].action;
        }
        memcpy(base_pars,pars,sizeof(base_pars));
        memcpy(base_cpars,cpars,sizeof(base_cpars));
        BuildWeaponStateSet();
    }
    memcpy(maxammo,base_maxammo,sizeof(base_maxammo));memcpy(clipammo,base_clipammo,sizeof(base_clipammo));
    memcpy(weaponinfo,base_weapons,sizeof(base_weapons));
    memcpy(mobjinfo,base_things,sizeof(base_things));
    for(i=0;i<NUMSTATES;++i)
    {
        states[i].tics=base_tics[i];
        states[i].nextstate=base_nextstate[i];
        states[i].sprite=base_sprite[i];states[i].frame=base_frame[i];
        states[i].misc1=base_misc1[i];states[i].misc2=base_misc2[i];
        states[i].action=base_action[i];
        visual_patched[i]=false;
        action_patched[i]=false;
    }
    memcpy(pars,base_pars,sizeof(base_pars));
    memcpy(cpars,base_cpars,sizeof(base_cpars));
    for(i=0;i<TEXT_COUNT;++i)
    {
        text_replacements[i].original=text_originals[i];
        text_replacements[i].replacement[0]=0;
        text_replacements[i].present=false;
    }
    for(i=0;i<FIELD_COUNT;++i)*fields[i].destination=fields[i].initial;
    loaded=bytes_total=0;last_error[0]=0;
    SHA1_Init(&digest_context);SHA1_UpdateString(&digest_context,DPT_DEH_SUBSET);initialized=true;
}
boolean DPT_DehLoadBytes(const unsigned char *data,size_t size)
{
    int staged[FIELD_COUNT];unsigned int i,line_number=0,assignments=0;
    boolean next_changed[NUMSTATES];
    boolean text_changed[TEXT_COUNT];
    int staged_max[NUMAMMO],staged_clip[NUMAMMO];
    size_t offset=0;boolean signature=false,doom=false,format=false;
    enum { NONE, MISC, AMMO, WEAPON, THING, FRAME, TEXT, STRINGS, PARS, CODEPTR } section=NONE;
    int index=0;
    char line[MAX_LINE+1];
    if(!initialized)DPT_DehReset();
    last_error[0]=0;
    if(!data||!size||size>MAX_SOURCE||size>MAX_SOURCE-bytes_total)return Error(0,"source/total patch bytes exceed 256 KiB or empty source");
    if(loaded>=MAX_PATCHES)return Error(0,"maximum 16 patch sources exceeded");
    if(memchr(data,0,size))return Error(0,"embedded NUL is not valid text");
    for(i=0;i<FIELD_COUNT;++i)staged[i]=*fields[i].destination;
    memcpy(staged_max,maxammo,sizeof(staged_max));memcpy(staged_clip,clipammo,sizeof(staged_clip));
    memcpy(staged_weapons,weaponinfo,sizeof(staged_weapons));
    memcpy(staged_things,mobjinfo,sizeof(staged_things));
    memcpy(staged_text,text_replacements,sizeof(staged_text));
    memcpy(staged_pars,pars,sizeof(staged_pars));
    memcpy(staged_cpars,cpars,sizeof(staged_cpars));
    memset(text_changed,0,sizeof(text_changed));
    for(i=0;i<NUMSTATES;++i)
    {
        staged_tics[i]=states[i].tics;
        staged_nextstate[i]=states[i].nextstate;
        staged_sprite[i]=states[i].sprite;staged_frame[i]=states[i].frame;
        staged_misc1[i]=states[i].misc1;staged_misc2[i]=states[i].misc2;
        staged_action[i]=states[i].action;
        staged_action_patched[i]=action_patched[i];
        staged_visual_patched[i]=visual_patched[i];
        next_changed[i]=false;
    }
    while(offset<size)
    {
        size_t start=offset,length;char *text,*equals;int value;
        while(offset<size&&data[offset]!='\n')++offset;
        length=offset-start;if(offset<size)++offset;++line_number;
        if(length>MAX_LINE)return Error(line_number,"line exceeds 2047 bytes");
        memcpy(line,data+start,length);line[length]=0;text=Trim(line);
        if(!*text||*text=='#'||*text==';')continue;
        if(!signature)
        {
            if(!Equal(text,"Patch File for DeHackEd v3.0"))return Error(line_number,"expected classic DeHackEd v3.0 signature");
            signature=true;continue;
        }
        if(Equal(text,"Misc 0"))
        {
            if(!doom||!format)return Error(line_number,"Doom version / Patch format headers required before Misc 0");
            section=MISC;continue;
        }
        if(Equal(text,"[STRINGS]"))
        {
            // BEX extension. Same whitelist and bounds as classic Text, but the
            // key is the string identifier instead of its original bytes.
            if(!doom||!format)return Error(line_number,"Doom version / Patch format headers required before [STRINGS]");
            section=STRINGS;continue;
        }
        if(Equal(text,"[PARS]"))
        {
            if(!doom||!format)return Error(line_number,"Doom version / Patch format headers required before [PARS]");
            section=PARS;continue;
        }
        if(Equal(text,"[CODEPTR]"))
        {
            if(!doom||!format)return Error(line_number,"Doom version / Patch format headers required before [CODEPTR]");
            section=CODEPTR;continue;
        }
        if(section==PARS&&IsToken(text,"par"))
        {
            char *separator=strchr(text,' ');
            if(!separator)return Error(line_number,"[PARS] par requires map and time");
            if(!ApplyPar(separator+1,line_number))return false;
            ++assignments;continue;
        }
        if(IsToken(text,"Text"))
        {
            int old_length,new_length,slot;
            if(!doom||!format)return Error(line_number,"headers required before Text section");
            if(!TextHeader(text,&old_length,&new_length))return Error(line_number,"invalid Text header (expected Text old-length new-length)");
            if(old_length<0||old_length>TEXT_MAX||new_length<0||new_length>TEXT_MAX)
                return Error(line_number,"Text lengths must be 0..2047");
            if(size-offset<(size_t)old_length+(size_t)new_length)
                return Error(line_number,"Text payload is truncated");
            slot=TextSlot(data+offset,old_length);
            if(slot<0)return Error(line_number,"Text old bytes are not a registered vanilla string");
            if(!ValidTextBytes(data+offset+old_length,new_length))
                return Error(line_number,"Text replacement contains unsafe bytes or format marker");
            memset(staged_text[slot].replacement,0,sizeof(staged_text[slot].replacement));
            memcpy(staged_text[slot].replacement,data+offset+old_length,(size_t)new_length);
            staged_text[slot].original=text_originals[slot];staged_text[slot].present=true;
            text_changed[slot]=true;offset+=(size_t)old_length+(size_t)new_length;
            section=TEXT;++assignments;continue;
        }
        if(!strchr(text,'='))
        {
            char *separator=strchr(text,' ');
            if(separator)
            {
                int ammo,weapon,thing,frame;
                *separator=0;ammo=Equal(text,"Ammo");weapon=Equal(text,"Weapon");thing=Equal(text,"Thing");frame=Equal(text,"Frame");
                if(ammo||weapon||thing||frame)
                {
                    if(!doom||!format)return Error(line_number,"headers required before indexed section");
                    if(frame)
                    {
                        if(!Number(Trim(separator+1),&index)||index<=S_NULL||index>=NUMSTATES)
                            return Error(line_number,"Frame index out of range (S_NULL is immutable)");
                        if(index>=S_SARG_RUN1&&index<=S_SARG_PAIN2)
                            return Error(line_number,"Frame belongs to unsupported fast/Nightmare duration range");
                        section=FRAME;continue;
                    }
                    if(thing)
                    {
                        if(!ThingIndex(separator+1,&index))return Error(line_number,"Thing index/label invalid (indices are 1-based)");
                        section=THING;continue;
                    }
                    if(!Number(Trim(separator+1),&index)||index<0||index>=(ammo?NUMAMMO:NUMWEAPONS))
                        return Error(line_number,"Ammo/Weapon section index out of range");
                    section=ammo?AMMO:WEAPON;continue;
                }
            }
            return Error(line_number,"unsupported section (Misc, Ammo, Weapon-ammo/frames, Thing-numeric, Frame graph, [STRINGS] only)");
        }
        equals=strchr(text,'=');
        if(!equals)return Error(line_number,"unsupported section");
        *equals=0;text=Trim(text);
        if(section==STRINGS)
        {
            if(!ApplyBexString(text,Trim(equals+1),staged_text,line_number))return false;
            ++assignments;continue;
        }
        if(section==CODEPTR)
        {
            if(!ApplyCodePtr(text,Trim(equals+1),line_number))return false;
            ++assignments;continue;
        }
        if(!Number(Trim(equals+1),&value))return Error(line_number,"invalid decimal integer / overflow");
        if(!section)
        {
            if(Equal(text,"Doom version")&&!doom&&value==19)doom=true;
            else if(Equal(text,"Patch format")&&!format&&value==6)format=true;
            else return Error(line_number,"expected Doom version = 19 and Patch format = 6 (no duplicates)");
            continue;
        }
        if(section==FRAME)
        {
            if(Equal(text,"Sprite number"))
            {
                if(value<0||value>=NUMSPRITES)return Error(line_number,"Sprite number outside the existing sprite table");
                staged_sprite[index]=(spritenum_t)value;
                staged_visual_patched[index]=true;++assignments;continue;
            }
            if(Equal(text,"Sprite subnumber"))
            {
                // Renderer supports A..] (29 frames), optionally fullbright.
                if(value<0||value>=(FF_FULLBRIGHT*2)||(value&FF_FRAMEMASK)>=29)
                    return Error(line_number,"Sprite subnumber must be 0..28 with optional fullbright bit 32768");
                staged_frame[index]=value;
                staged_visual_patched[index]=true;++assignments;continue;
            }
            if(Equal(text,"Unknown 1")||Equal(text,"Unknown 2"))
            {
                // P_SetPsprite applies these as psprite coordinate offsets, so
                // a mobj-state assignment would be inert in this engine. Bound
                // them to whole map units and reject non-psprite states instead
                // of silently accepting a value nothing reads.
                if(!base_weapon_state[index])
                    return Error(line_number,"Unknown 1/2 is supported only on weapon states");
                if(value<-128||value>128)
                    return Error(line_number,"Unknown 1/2 must be -128..128 map units");
                if(Equal(text,"Unknown 1"))staged_misc1[index]=value;
                else staged_misc2[index]=value;
                ++assignments;continue;
            }
            if(Equal(text,"Duration"))
            {
                if(value<1||value>32767)return Error(line_number,"Frame Duration must be 1..32767 (zero/negative unsupported)");
                staged_tics[index]=value;++assignments;continue;
            }
            if(Equal(text,"Next frame"))
            {
                if(value<0||value>=NUMSTATES)return Error(line_number,"Next frame outside the state table");
                staged_nextstate[index]=(statenum_t)value;
                next_changed[index]=true;++assignments;continue;
            }
            return Error(line_number,"unsupported Frame field (Duration, Next frame, Sprite number/subnumber, Unknown 1/2 only)");
        }
        if(section==TEXT)
            return Error(line_number,"Text payload must follow its Text header");
        if(section==THING)
        {
            if(!ApplyThing(index,text,value,line_number))return false;
            ++assignments;continue;
        }
        if(section==AMMO)
        {
            if(Equal(text,"Max ammo"))
            {if(value<1||value>10000)return Error(line_number,"Max ammo must be 1..10000");staged_max[index]=value;}
            else if(Equal(text,"Per ammo"))
            {if(value<0||value>1000)return Error(line_number,"Per ammo must be 0..1000");staged_clip[index]=value;}
            else return Error(line_number,"unsupported Ammo field");
            ++assignments;continue;
        }
        if(section==WEAPON)
        {
            if(Equal(text,"Ammo type"))
            {
                // Historical overflow/noammo tricks intentionally not supported.
                // In this engine ammo type 5 on a shooting action targets maxammo;
                // do not advertise that memory-layout behavior as safe compatibility.
                if(value<0||value>=NUMAMMO)return Error(line_number,"Weapon Ammo type must be 0..3");
                staged_weapons[index].ammo=(ammotype_t)value;++assignments;continue;
            }
            if(!ApplyWeaponFrame(index,text,value,line_number))return false;
            ++assignments;continue;
        }
        for(i=0;i<FIELD_COUNT;++i)if(Equal(text,fields[i].name))break;
        if(i==FIELD_COUNT)return Error(line_number,"unsupported Misc field in " DPT_DEH_SUBSET);
        if(value<fields[i].low||value>fields[i].high)return Error(line_number,"Misc field outside supported safe range");
        staged[i]=value;++assignments;
    }
    if(!section||!assignments)return Error(line_number,"patch has no supported assignments");
    if(staged[1]>staged_max[am_clip])return Error(line_number,"Initial Bullets exceeds Max ammo for bullets");
    if(!ValidateThingIds(staged_things,line_number))return false;
    if(!ValidateFrameGraph(staged_tics,staged_nextstate,next_changed,line_number))return false;
    // Commit only after the entire source has passed validation.
    for(i=0;i<FIELD_COUNT;++i)*fields[i].destination=staged[i];
    memcpy(maxammo,staged_max,sizeof(staged_max));memcpy(clipammo,staged_clip,sizeof(staged_clip));
    memcpy(weaponinfo,staged_weapons,sizeof(staged_weapons));
    memcpy(mobjinfo,staged_things,sizeof(staged_things));
    for(i=0;i<NUMSTATES;++i)states[i].tics=staged_tics[i];
    for(i=0;i<NUMSTATES;++i)states[i].nextstate=staged_nextstate[i];
    for(i=0;i<NUMSTATES;++i)
    {
        states[i].sprite=staged_sprite[i];states[i].frame=staged_frame[i];
        states[i].misc1=staged_misc1[i];states[i].misc2=staged_misc2[i];
        states[i].action=staged_action[i];
        action_patched[i]=staged_action_patched[i];
        visual_patched[i]=staged_visual_patched[i];
    }
    memcpy(text_replacements,staged_text,sizeof(staged_text));
    memcpy(pars,staged_pars,sizeof(staged_pars));
    memcpy(cpars,staged_cpars,sizeof(staged_cpars));
    SHA1_UpdateInt32(&digest_context,(unsigned int)size);
    SHA1_Update(&digest_context,(byte*)data,size);
    ++loaded;bytes_total+=(unsigned int)size;return true;
}
boolean DPT_DehLoadLump(int lump)
{
    int length;byte *data;boolean result;
    if(lump<0||(unsigned)lump>=numlumps)return Error(0,"invalid DEHACKED lump index");
    length=W_LumpLength((unsigned)lump);
    if(length<=0||length>MAX_SOURCE)return Error(0,"invalid/oversized DEHACKED lump");
    data=malloc((size_t)length);if(!data)return Error(0,"out of memory reading patch");
    W_ReadLump((unsigned)lump,data);result=DPT_DehLoadBytes(data,(size_t)length);free(data);return result;
}
boolean DPT_DehLoadFile(const char *path)
{
    FILE *file=fopen(path,"rb");long length;byte *data;boolean result;
    if(!file)return Error(0,"cannot open -deh file");
    if(fseek(file,0,SEEK_END)||(length=ftell(file))<=0||length>MAX_SOURCE||fseek(file,0,SEEK_SET))
    {fclose(file);return Error(0,"invalid/oversized -deh file");}
    data=malloc((size_t)length);if(!data){fclose(file);return Error(0,"out of memory reading patch");}
    result=fread(data,1,(size_t)length,file)==(size_t)length;
    if(fclose(file))result=false;
    if(result)result=DPT_DehLoadBytes(data,(size_t)length);else Error(0,"patch file read/close failed");
    free(data);return result;
}
void DPT_DehChecksum(sha1_digest_t digest)
{
    sha1_context_t copy;
    if(!initialized)DPT_DehReset();
    copy=digest_context;SHA1_Final(digest,&copy);
}
unsigned int DPT_DehLoadedCount(void){return loaded;}
const char *DPT_DehLastError(void){return last_error;}
