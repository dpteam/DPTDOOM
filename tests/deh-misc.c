// Synthetic parser/transaction/digest tests, no proprietary data.
// GPL-2.0-or-later; see LICENSE.
#include "deh_classic.h"
#include "deh_misc.h"
#include "d_items.h"
#include "info.h"
#include "m_fixed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) do { if(!(x)) { fprintf(stderr,"line %d: %s; %s\n",__LINE__,#x,DPT_DehLastError());exit(1); } } while(0)
#define HEADER "Patch File for DeHackEd v3.0\nDoom version = 19\nPatch format = 6\n\nMisc 0\n"
unsigned int numlumps;
int maxammo[NUMAMMO]={200,50,300,50},clipammo[NUMAMMO]={10,4,20,1};
mobjinfo_t mobjinfo[NUMMOBJTYPES]; // synthetic tables; real info.c covered by engine test
state_t states[NUMSTATES];
static void ActionSentinel(void) {}
int W_LumpLength(unsigned int i){(void)i;return 0;}
void W_ReadLump(unsigned int i,void *p){(void)i;(void)p;}
static int Load(const char *text){return DPT_DehLoadBytes((const unsigned char*)text,strlen(text));}
int main(void)
{
    sha1_digest_t a,b,c;unsigned int i;unsigned char *data;
    const char *bad[]={
        "", "[Misc]\nInitial Health = 42\n", HEADER "Initial Health = 0\n",
        HEADER "Initial Health = 50\nInitial Bullets = 9999999999999999999999999999999\n",
        HEADER "Initial Health = 50\nBlue Armor Class = 3\n", HEADER "BFG Cells/Shot = 0\n",
        HEADER "Monsters Infight = 202\n", HEADER "Unknown field = 1\n",
        HEADER "Thing 1\nBits = 20\n", HEADER "[CODEPTR]\n", HEADER "Initial Health = 12junk\n",
        "Patch File for DeHackEd v3.0\nDoom version = 109\nPatch format = 6\nMisc 0\nInitial Health = 12\n",
        "Patch File for DeHackEd v3.0\nDoom version = 19\nMisc 0\nInitial Health = 12\n"};
    mobjinfo[MT_POSSESSED].spawnhealth=20;mobjinfo[MT_POSSESSED].speed=8;mobjinfo[MT_ROCKET].speed=20*FRACUNIT;
    mobjinfo[MT_POSSESSED].spawnstate=42;mobjinfo[MT_POSSESSED].flags=123;
    states[S_NULL].tics=-1;states[S_PISTOL1].tics=4;states[S_PISTOL1].nextstate=S_PISTOL2;
    states[S_PISTOL1].sprite=SPR_PISG;states[S_PISTOL1].frame=7;
    states[S_PISTOL1].action.acv=ActionSentinel;
    DPT_DehReset();DPT_DehChecksum(a);DPT_DehChecksum(b);REQUIRE(!memcmp(a,b,20));
    REQUIRE(Load(HEADER "Initial Health = 77\nInitial Bullets = 23\nBFG Cells/Shot = 20\n"));
    REQUIRE(deh_initial_health==77&&deh_initial_bullets==23&&deh_bfg_cells_per_shot==20);
    DPT_DehChecksum(b);REQUIRE(memcmp(a,b,20));DPT_DehChecksum(c);REQUIRE(!memcmp(b,c,20));
    for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i)
    {REQUIRE(!Load(bad[i]));REQUIRE(deh_initial_health==77&&deh_initial_bullets==23);DPT_DehChecksum(c);REQUIRE(!memcmp(b,c,20));REQUIRE(DPT_DehLoadedCount()==1);}
    REQUIRE(Load("# comment\r\nPatch File for DeHackEd v3.0\r\nDoom version = 19\r\nPatch format = 6\r\nMisc 0\r\ninitial health = 88\r\n"));
    REQUIRE(deh_initial_health==88);
    {unsigned char nul[]={1,0,2};REQUIRE(!DPT_DehLoadBytes(nul,sizeof(nul)));}
    data=malloc(262145);REQUIRE(data);memset(data,'x',262145);REQUIRE(!DPT_DehLoadBytes(data,262145));REQUIRE(!DPT_DehLoadBytes(data,2048));free(data);
    DPT_DehReset();for(i=0;i<16;++i)REQUIRE(Load(HEADER "Initial Health = 99\n"));REQUIRE(!Load(HEADER "Initial Health = 100\n"));
    REQUIRE(deh_initial_health==99);DPT_DehReset();REQUIRE(!DPT_DehLoadedCount()&&deh_initial_health==100);
    // Byte budget, not merely number of lines: accepted comments still count.
    data=malloc(140000);REQUIRE(data);memset(data,' ',140000);
    {const char *patch=HEADER "Initial Health = 55\n";size_t n=strlen(patch),pos=n;
     memcpy(data,patch,n);
     while(pos<139999){size_t end=pos+1000;if(end>139999)end=139999;memset(data+pos,' ',end-pos);data[end]='\n';pos=end+1;}
     REQUIRE(DPT_DehLoadBytes(data,140000));REQUIRE(!DPT_DehLoadBytes(data,140000));
     REQUIRE(deh_initial_health==55&&DPT_DehLoadedCount()==1);}
    free(data);DPT_DehReset();
    REQUIRE(Load(HEADER "Initial Bullets = 60\nAmmo 0\nMax ammo = 400\nPer ammo = 7\nWeapon 1\nAmmo type = 1\n"));
    REQUIRE(maxammo[0]==400&&clipammo[0]==7&&weaponinfo[wp_pistol].ammo==am_shell);
    DPT_DehChecksum(a);
    REQUIRE(!Load(HEADER "Initial Health = 5\nAmmo 0\nMax ammo = 20\n"));
    REQUIRE(deh_initial_health==100&&maxammo[0]==400&&weaponinfo[wp_pistol].ammo==am_shell);
    REQUIRE(!Load(HEADER "Ammo 4\nMax ammo = 500\n"));
    REQUIRE(!Load(HEADER "Weapon 1\nAmmo type = 5\n"));
    REQUIRE(!Load(HEADER "Weapon 9\nAmmo type = 1\n"));
    REQUIRE(!Load(HEADER "Weapon 1\nShooting frame = 1\n"));
    REQUIRE(!Load(HEADER "Ammo 0\nPer ammo = 1001\n"));
    DPT_DehChecksum(b);REQUIRE(!memcmp(a,b,20));
    DPT_DehReset();REQUIRE(maxammo[0]==200&&clipammo[0]==10&&weaponinfo[wp_pistol].ammo==am_clip);
    REQUIRE(Load(HEADER "ammo 0\nPer ammo = 12\nweapon 1\nAmmo type = 2\n"));
    REQUIRE(clipammo[0]==12&&weaponinfo[wp_pistol].ammo==am_cell);DPT_DehReset();
    REQUIRE(Load(HEADER "Thing 2 (Trooper)\nHit points = 123\nSpeed = 12\nWidth = 1572864\nHeight = 3932160\nMass = 250\nMissile damage = 7\nPain chance = 0\nReaction time = 20\nThing 34\nSpeed = 1966080\n"));
    REQUIRE(mobjinfo[MT_POSSESSED].spawnhealth==123&&mobjinfo[MT_POSSESSED].speed==12);
    REQUIRE(mobjinfo[MT_POSSESSED].radius==24*FRACUNIT&&mobjinfo[MT_POSSESSED].mass==250);
    REQUIRE(mobjinfo[MT_ROCKET].speed==30*FRACUNIT);
    REQUIRE(mobjinfo[MT_POSSESSED].spawnstate==42&&mobjinfo[MT_POSSESSED].flags==123);
    DPT_DehChecksum(a);
    {const char *invalid[]={HEADER "Thing 0\nHit points = 1\n",HEADER "Thing 999\nHit points = 1\n",
      HEADER "Thing 2 (Unclosed\nHit points = 1\n",HEADER "Thing 2 (OK) trailing\nHit points = 1\n",
      HEADER "Initial Health = 5\nAmmo 0\nPer ammo = 50\nThing 2\nHit points = 3\nMass = 0\n",
      HEADER "Thing 2\nSpeed = 0\n",HEADER "Thing 2\nSpeed = 129\n",HEADER "Thing 34\nSpeed = 4194305\n",
      HEADER "Thing 2\nWidth = -1\n",HEADER "Thing 2\nHeight = 2147483647\n",
      HEADER "Thing 2\nMissile damage = 101\n",HEADER "Thing 2\nPain chance = 256\n",
      HEADER "Thing 2\nInitial frame = 0\n",HEADER "Thing 2\nHit points = 999999999999999999999999\n"};
     for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){REQUIRE(!Load(invalid[i]));REQUIRE(mobjinfo[MT_POSSESSED].spawnhealth==123&&mobjinfo[MT_POSSESSED].mass==250);
      REQUIRE(deh_initial_health==100&&clipammo[0]==10);DPT_DehChecksum(b);REQUIRE(!memcmp(a,b,20));}}
    REQUIRE(Load(HEADER "thing 2\nHit points = 234\n"));REQUIRE(mobjinfo[MT_POSSESSED].spawnhealth==234);
    {char last[256];snprintf(last,sizeof(last),HEADER "Thing %d (Last original thing)\nMass = 999\n",NUMMOBJTYPES);
     REQUIRE(Load(last));REQUIRE(mobjinfo[NUMMOBJTYPES-1].mass==999);}
    REQUIRE(!Load(HEADER "Thing 2 (Trooper) garbage\nMass = 250\n"));
    REQUIRE(!Load(HEADER "Thing 2 (Outer (Inner))\nMass = 250\n"));
    DPT_DehReset();REQUIRE(mobjinfo[MT_POSSESSED].spawnhealth==20&&mobjinfo[MT_POSSESSED].speed==8&&mobjinfo[MT_ROCKET].speed==20*FRACUNIT);
    REQUIRE(mobjinfo[NUMMOBJTYPES-1].mass==0);
    REQUIRE(Load(HEADER "Frame 13\nDuration = 30\nframe 14\nDuration = 31\n"));
    REQUIRE(states[S_PISTOL1].tics==30&&states[S_PISTOL2].tics==31);
    REQUIRE(states[S_PISTOL1].nextstate==S_PISTOL2&&states[S_PISTOL1].sprite==SPR_PISG&&states[S_PISTOL1].frame==7);
    REQUIRE(states[S_PISTOL1].action.acv==ActionSentinel);DPT_DehChecksum(a);
    {const char *invalid[]={HEADER "Frame 0\nDuration = 1\n",HEADER "Frame -1\nDuration = 1\n",
      HEADER "Frame 999999999999999999999\nDuration = 1\n",HEADER "Frame 13 (Pistol)\nDuration = 1\n",
      HEADER "Frame 13\nDuration = 0\n",HEADER "Frame 13\nDuration = -1\n",HEADER "Frame 13\nDuration = 32768\n",
      HEADER "Frame 13\nNext frame = 14\n",HEADER "Frame 13\nSprite number = 1\n",
      HEADER "Initial Health = 5\nAmmo 0\nPer ammo = 12\nThing 2\nHit points = 42\nFrame 13\nDuration = 2\nFrame 14\nDuration = 0\n"};
     for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){REQUIRE(!Load(invalid[i]));REQUIRE(states[S_PISTOL1].tics==30&&states[S_PISTOL2].tics==31);
      REQUIRE(deh_initial_health==100&&clipammo[0]==10&&mobjinfo[MT_POSSESSED].spawnhealth==20);DPT_DehChecksum(b);REQUIRE(!memcmp(a,b,20));}}
    {char patch[256];
     snprintf(patch,sizeof(patch),HEADER "Frame %d\nDuration = 1\n",NUMSTATES);REQUIRE(!Load(patch));
     snprintf(patch,sizeof(patch),HEADER "Frame %d\nDuration = 2\n",S_SARG_RUN1);REQUIRE(!Load(patch));
     snprintf(patch,sizeof(patch),HEADER "Frame %d\nDuration = 2\n",S_SARG_PAIN2);REQUIRE(!Load(patch));
     snprintf(patch,sizeof(patch),HEADER "Frame %d\nDuration = 32767\n",NUMSTATES-1);REQUIRE(Load(patch));REQUIRE(states[NUMSTATES-1].tics==32767);}
    REQUIRE(Load(HEADER "Frame 13\nDuration = 60\n"));REQUIRE(states[S_PISTOL1].tics==60);
    DPT_DehReset();REQUIRE(states[S_PISTOL1].tics==4&&states[S_NULL].tics==-1);
    REQUIRE(states[S_PISTOL1].action.acv==ActionSentinel&&states[S_PISTOL1].nextstate==S_PISTOL2);
    REQUIRE(!DPT_DehLoadLump(-1));REQUIRE(!DPT_DehLoadFile("missing.deh"));
    puts("DeHackEd Misc/Ammo/Thing/Frame: classic grammar, strict ranges, atomic rollback, limits, repeated digest and ordering OK");return 0;
}
