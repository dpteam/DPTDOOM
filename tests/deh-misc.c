// Synthetic parser/transaction/digest tests, no proprietary data.
// GPL-2.0-or-later; see LICENSE.
#include "deh_classic.h"
#include "deh_misc.h"
#include "d_items.h"
#include "info.h"
#include "m_fixed.h"
#include "d_englsh.h"
#include "deh_str.h"
#include "sounds.h"
#include "p_pspr.h"
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
static int LoadData(const unsigned char *data,size_t size){return DPT_DehLoadBytes(data,size);}
int main(void)
{
    sha1_digest_t a,b,c;unsigned int i;unsigned char *data;
    const char *bad[]={
        "", "[Misc]\nInitial Health = 42\n", HEADER "Initial Health = 0\n",
        HEADER "Initial Health = 50\nInitial Bullets = 9999999999999999999999999999999\n",
        HEADER "Initial Health = 50\nBlue Armor Class = 3\n", HEADER "BFG Cells/Shot = 0\n",
        HEADER "Monsters Infight = 2\n", HEADER "Unknown field = 1\n",
        HEADER "Thing 1\nBits = 1073741824\n", HEADER "[CODEPTR]\n", HEADER "Initial Health = 12junk\n",
        "Patch File for DeHackEd v3.0\nDoom version = 109\nPatch format = 6\nMisc 0\nInitial Health = 12\n",
        "Patch File for DeHackEd v3.0\nDoom version = 19\nMisc 0\nInitial Health = 12\n"};
    for(i=0;i<NUMMOBJTYPES;++i)mobjinfo[i].doomednum=-1;
    mobjinfo[MT_POSSESSED].doomednum=3004;mobjinfo[MT_SHOTGUY].doomednum=9;
    mobjinfo[MT_POSSESSED].spawnhealth=20;mobjinfo[MT_POSSESSED].speed=8;mobjinfo[MT_ROCKET].speed=20*FRACUNIT;
    mobjinfo[MT_POSSESSED].spawnstate=42;mobjinfo[MT_POSSESSED].flags=123;
    states[S_NULL].tics=-1;states[S_PISTOL1].tics=4;states[S_PISTOL1].nextstate=S_PISTOL2;
    states[S_PISTOL2].nextstate=S_PISTOL3;states[S_PISTOL3].nextstate=S_PISTOL4;
    states[S_PISTOL4].nextstate=S_PISTOL;
    states[S_PISTOL1].sprite=SPR_PISG;states[S_PISTOL1].frame=7;
    states[S_PISTOL1].action.acv=ActionSentinel;
    DPT_DehReset();DPT_DehChecksum(a);DPT_DehChecksum(b);REQUIRE(!memcmp(a,b,20));
    REQUIRE(Load(HEADER "Initial Health = 77\nInitial Bullets = 23\nBFG Cells/Shot = 20\nMonsters Infight = 1\n"));
    REQUIRE(deh_initial_health==77&&deh_initial_bullets==23&&deh_bfg_cells_per_shot==20);
    REQUIRE(deh_species_infighting==1);
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
    REQUIRE(Load(HEADER "Weapon 1\nDeselect frame = 11\nSelect frame = 12\nBobbing frame = 10\nShooting frame = 13\nFiring frame = 17\n"));
    REQUIRE(weaponinfo[wp_pistol].downstate==S_PISTOLDOWN&&weaponinfo[wp_pistol].upstate==S_PISTOLUP);
    REQUIRE(weaponinfo[wp_pistol].readystate==S_PISTOL&&weaponinfo[wp_pistol].atkstate==S_PISTOL1);
    REQUIRE(weaponinfo[wp_pistol].flashstate==S_PISTOLFLASH);
    REQUIRE(!Load(HEADER "Weapon 1\nShooting frame = 999999\n"));
    {char invalid_weapon[128];
     snprintf(invalid_weapon,sizeof(invalid_weapon),HEADER "Weapon 1\nShooting frame = %d\n",S_BLOOD1);
     REQUIRE(!Load(invalid_weapon));}
    REQUIRE(!Load(HEADER "Weapon 1\nDeselect frame = 0\n"));
    REQUIRE(!Load(HEADER "Weapon 1\nShooting frame = 1\n"));
    DPT_DehReset();REQUIRE(weaponinfo[wp_pistol].ammo==am_clip&&weaponinfo[wp_pistol].upstate==S_PISTOLUP);
    {
        const char old[]=GOTARMOR,replacement[]="Custom armor message!\n";
        unsigned char text_patch[512];size_t head,n=strlen(old),r=strlen(replacement);
        head=(size_t)snprintf((char *)text_patch,sizeof(text_patch),HEADER "Text %u %u\n",(unsigned)n,(unsigned)r);
        memcpy(text_patch+head,old,n);memcpy(text_patch+head+n,replacement,r);
        REQUIRE(LoadData(text_patch,head+n+r));
        REQUIRE(!strcmp(DPT_DehString(GOTARMOR),replacement));
        REQUIRE(!strcmp(DPT_DehString(GOTMEGA),GOTMEGA));
        DPT_DehReset();REQUIRE(!strcmp(DPT_DehString(GOTARMOR),GOTARMOR));
    }
    {
        const char old[]=GGSAVED;unsigned char patch[256];size_t head,n=strlen(old);
        head=(size_t)snprintf((char *)patch,sizeof(patch),HEADER "Text %u 0\n",(unsigned)n);
        memcpy(patch+head,old,n);REQUIRE(LoadData(patch,head+n));
        REQUIRE(!strcmp(DPT_DehString(GGSAVED),""));DPT_DehReset();
    }
    {
        const char old[]=GOTARMOR;const unsigned char bad[]={ 'X','%','Y' };unsigned char patch[256];
        size_t head=(size_t)snprintf((char *)patch,sizeof(patch),HEADER "Text %u 3\n",(unsigned)strlen(old));
        memcpy(patch+head,old,strlen(old));memcpy(patch+head+strlen(old),bad,sizeof(bad));
        REQUIRE(!LoadData(patch,head+strlen(old)+sizeof(bad)));REQUIRE(!strcmp(DPT_DehString(GOTARMOR),GOTARMOR));
        head=(size_t)snprintf((char *)patch,sizeof(patch),HEADER "Text %u 4\n",(unsigned)strlen(old));
        memcpy(patch+head,old,strlen(old));memcpy(patch+head+strlen(old),"tiny",4);
        REQUIRE(!LoadData(patch,head+strlen(old)+3));REQUIRE(!strcmp(DPT_DehString(GOTARMOR),GOTARMOR));
        head=(size_t)snprintf((char *)patch,sizeof(patch),HEADER "Text %u 4\n",(unsigned)strlen(old));
        memcpy(patch+head,"wrong",5);memcpy(patch+head+5,"tiny",4);
    REQUIRE(!LoadData(patch,head+9));REQUIRE(!strcmp(DPT_DehString(GOTARMOR),GOTARMOR));
    }
    REQUIRE(Load(HEADER "Thing 2\nID # = 4000\nBits = 38\n"));
    REQUIRE(mobjinfo[MT_POSSESSED].doomednum==4000&&mobjinfo[MT_POSSESSED].flags==38);
    {char state_patch[512];
     snprintf(state_patch,sizeof(state_patch),HEADER "Thing 2\nInitial frame = %d\nFirst moving frame = %d\nInjury frame = 0\nClose attack frame = %d\nFar attack frame = %d\nDeath frame = 0\nExploding frame = %d\nRespawn frame = 0\n",
         S_BLOOD1,S_BLOOD2,S_BLOOD1,S_BLOOD2,S_BLOOD1);
     REQUIRE(Load(state_patch));}
    REQUIRE(mobjinfo[MT_POSSESSED].spawnstate==S_BLOOD1&&mobjinfo[MT_POSSESSED].seestate==S_BLOOD2);
    REQUIRE(mobjinfo[MT_POSSESSED].painstate==S_NULL&&mobjinfo[MT_POSSESSED].meleestate==S_BLOOD1);
    REQUIRE(mobjinfo[MT_POSSESSED].missilestate==S_BLOOD2&&mobjinfo[MT_POSSESSED].deathstate==S_NULL);
    REQUIRE(mobjinfo[MT_POSSESSED].xdeathstate==S_BLOOD1&&mobjinfo[MT_POSSESSED].raisestate==S_NULL);
    REQUIRE(!Load(HEADER "Thing 2\nInitial frame = 13\n"));
    REQUIRE(!Load(HEADER "Thing 2\nDeath frame = 999999\n"));
    REQUIRE(mobjinfo[MT_POSSESSED].spawnstate==S_BLOOD1);
    REQUIRE(!Load(HEADER "Thing 2\nID # = 9\n"));
    REQUIRE(!Load(HEADER "Thing 2\nID # = -2\n"));
    REQUIRE(!Load(HEADER "Thing 2\nBits = -1\n"));
    REQUIRE(!Load(HEADER "Thing 2\nBits = 1073741824\n"));
    REQUIRE(mobjinfo[MT_POSSESSED].doomednum==4000&&mobjinfo[MT_POSSESSED].flags==38);
    REQUIRE(Load(HEADER "Thing 2 (Trooper)\nHit points = 123\nSpeed = 12\nWidth = 1572864\nHeight = 3932160\nMass = 250\nMissile damage = 7\nPain chance = 0\nReaction time = 20\nThing 34\nSpeed = 1966080\n"));
    REQUIRE(mobjinfo[MT_POSSESSED].spawnhealth==123&&mobjinfo[MT_POSSESSED].speed==12);
    REQUIRE(mobjinfo[MT_POSSESSED].radius==24*FRACUNIT&&mobjinfo[MT_POSSESSED].mass==250);
    REQUIRE(mobjinfo[MT_ROCKET].speed==30*FRACUNIT);
    REQUIRE(mobjinfo[MT_POSSESSED].spawnstate==S_BLOOD1&&mobjinfo[MT_POSSESSED].flags==38);
    REQUIRE(Load(HEADER "Thing 31\nAlert sound = 1\nAttack sound = 2\nPain sound = 3\nDeath sound = 16\nAction sound = 8\n"));
    REQUIRE(mobjinfo[MT_BARREL].seesound==sfx_pistol&&mobjinfo[MT_BARREL].attacksound==sfx_shotgn);
    REQUIRE(mobjinfo[MT_BARREL].painsound==sfx_sgcock&&mobjinfo[MT_BARREL].deathsound==sfx_firsht);
    REQUIRE(mobjinfo[MT_BARREL].activesound==sfx_plasma);
    REQUIRE(!Load(HEADER "Thing 31\nDeath sound = 999999\n"));
    REQUIRE(mobjinfo[MT_BARREL].deathsound==sfx_firsht);
    DPT_DehChecksum(a);
    {const char *invalid[]={HEADER "Thing 0\nHit points = 1\n",HEADER "Thing 999\nHit points = 1\n",
      HEADER "Thing 2 (Unclosed\nHit points = 1\n",HEADER "Thing 2 (OK) trailing\nHit points = 1\n",
      HEADER "Initial Health = 5\nAmmo 0\nPer ammo = 50\nThing 2\nHit points = 3\nMass = 0\n",
      HEADER "Thing 2\nSpeed = 0\n",HEADER "Thing 2\nSpeed = 129\n",HEADER "Thing 34\nSpeed = 4194305\n",
      HEADER "Thing 2\nWidth = -1\n",HEADER "Thing 2\nHeight = 2147483647\n",
      HEADER "Thing 2\nMissile damage = 101\n",HEADER "Thing 2\nPain chance = 256\n",
       HEADER "Thing 2\nInitial frame = 13\n",HEADER "Thing 2\nHit points = 999999999999999999999999\n"};
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
      HEADER "Frame 13\nNext frame = 999999\n",HEADER "Frame 13\nUnknown 3 = 1\n",
      HEADER "Initial Health = 5\nAmmo 0\nPer ammo = 12\nThing 2\nHit points = 42\nFrame 13\nDuration = 2\nFrame 14\nDuration = 0\n"};
     for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){REQUIRE(!Load(invalid[i]));REQUIRE(states[S_PISTOL1].tics==30&&states[S_PISTOL2].tics==31);
      REQUIRE(deh_initial_health==100&&clipammo[0]==10&&mobjinfo[MT_POSSESSED].spawnhealth==20);DPT_DehChecksum(b);REQUIRE(!memcmp(a,b,20));}}
    {char patch[256];
     snprintf(patch,sizeof(patch),HEADER "Frame %d\nDuration = 1\n",NUMSTATES);REQUIRE(!Load(patch));
     snprintf(patch,sizeof(patch),HEADER "Frame %d\nDuration = 2\n",S_SARG_RUN1);REQUIRE(!Load(patch));
     snprintf(patch,sizeof(patch),HEADER "Frame %d\nDuration = 2\n",S_SARG_PAIN2);REQUIRE(!Load(patch));
     snprintf(patch,sizeof(patch),HEADER "Frame %d\nDuration = 32767\n",NUMSTATES-1);REQUIRE(Load(patch));REQUIRE(states[NUMSTATES-1].tics==32767);}
    REQUIRE(Load(HEADER "Frame 13\nNext frame = 15\n"));
    REQUIRE(states[S_PISTOL1].nextstate==S_PISTOL3&&states[S_PISTOL1].action.acv==ActionSentinel);
    REQUIRE(Load(HEADER "Frame 13\nNext frame = 0\n"));REQUIRE(states[S_PISTOL1].nextstate==S_NULL);
    REQUIRE(!Load(HEADER "Frame 1\nNext frame = 1\n"));
    REQUIRE(states[S_LIGHTDONE].nextstate==S_NULL);
    REQUIRE(Load(HEADER "Frame 13\nDuration = 60\n"));REQUIRE(states[S_PISTOL1].tics==60);
    DPT_DehReset();REQUIRE(states[S_PISTOL1].tics==4&&states[S_NULL].tics==-1);
    REQUIRE(states[S_PISTOL1].action.acv==ActionSentinel&&states[S_PISTOL1].nextstate==S_PISTOL2);
    {
        int counts[NUMSPRITES];char patch[256];unsigned int loaded_before;
        memset(counts,0,sizeof(counts));counts[SPR_TROO]=2;
        REQUIRE(DPT_DehValidateSpriteFrames(NULL,0)); // untouched tables need no resources
        REQUIRE(Load(HEADER "Frame 13\nSprite subnumber = 32769\nSprite number = 0\n"));
        REQUIRE(states[S_PISTOL1].sprite==SPR_TROO&&states[S_PISTOL1].frame==(FF_FULLBRIGHT|1));
        REQUIRE(states[S_PISTOL1].tics==4&&states[S_PISTOL1].action.acv==ActionSentinel);
        REQUIRE(states[S_PISTOL1].nextstate==S_PISTOL2);
        REQUIRE(DPT_DehValidateSpriteFrames(counts,NUMSPRITES));
        counts[SPR_TROO]=1;REQUIRE(!DPT_DehValidateSpriteFrames(counts,NUMSPRITES));
        REQUIRE(strstr(DPT_DehLastError(),"Frame 13"));
        REQUIRE(!DPT_DehValidateSpriteFrames(NULL,0));
        counts[SPR_TROO]=2;REQUIRE(DPT_DehValidateSpriteFrames(counts,NUMSPRITES));
        DPT_DehChecksum(a);loaded_before=DPT_DehLoadedCount();
        {const char *invalid[]={HEADER "Frame 13\nSprite number = -1\n",
          HEADER "Frame 13\nSprite subnumber = -1\n",HEADER "Frame 13\nSprite subnumber = 29\n",
          HEADER "Frame 13\nSprite subnumber = 32797\n",HEADER "Frame 13\nSprite subnumber = 65536\n",
          HEADER "Frame 13\nSprite number = 99999999999999999999999\n",
          HEADER "Initial Health = 5\nFrame 13\nSprite subnumber = 0\nFrame 14\nSprite number = -1\n"};
         for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i)
         {REQUIRE(!Load(invalid[i]));REQUIRE(states[S_PISTOL1].frame==(FF_FULLBRIGHT|1));
          REQUIRE(deh_initial_health==100&&DPT_DehLoadedCount()==loaded_before);
          DPT_DehChecksum(b);REQUIRE(!memcmp(a,b,20));}}
        snprintf(patch,sizeof(patch),HEADER "Frame 13\nSprite number = %d\n",NUMSPRITES);REQUIRE(!Load(patch));
        REQUIRE(Load(HEADER "Frame 13\nSprite subnumber = 0\n"));
        REQUIRE(states[S_PISTOL1].frame==0);REQUIRE(DPT_DehValidateSpriteFrames(counts,NUMSPRITES));
        REQUIRE(Load(HEADER "Frame 13\nSprite subnumber = 32796\n")); // renderer bound accepted; resources checked later
        REQUIRE(!DPT_DehValidateSpriteFrames(counts,NUMSPRITES));
        DPT_DehReset();REQUIRE(states[S_PISTOL1].sprite==SPR_PISG&&states[S_PISTOL1].frame==7);
        REQUIRE(states[S_PISTOL1].action.acv==ActionSentinel&&DPT_DehValidateSpriteFrames(NULL,0));
    }
    {
        char patch[256];
        REQUIRE(Load(HEADER "Frame 13\nUnknown 1 = -8\nUnknown 2 = 16\n"));
        REQUIRE(states[S_PISTOL1].misc1==-8&&states[S_PISTOL1].misc2==16);
        REQUIRE(states[S_PISTOL1].sprite==SPR_PISG&&states[S_PISTOL1].frame==7);
        REQUIRE(states[S_PISTOL1].tics==4&&states[S_PISTOL1].action.acv==ActionSentinel);
        DPT_DehChecksum(a);
        {const char *invalid[]={HEADER "Frame 13\nUnknown 1 = 129\n",
          HEADER "Frame 13\nUnknown 2 = -129\n",HEADER "Frame 13\nUnknown 1 = 99999999999999999999\n",
          HEADER "Initial Health = 5\nFrame 13\nUnknown 1 = 4\nFrame 14\nUnknown 2 = -129\n"};
         for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i)
         {REQUIRE(!Load(invalid[i]));REQUIRE(states[S_PISTOL1].misc1==-8&&states[S_PISTOL1].misc2==16);
          REQUIRE(deh_initial_health==100);DPT_DehChecksum(b);REQUIRE(!memcmp(a,b,20));}}
        // Mobj states never consume misc1/misc2 in this engine, so reject them.
        snprintf(patch,sizeof(patch),HEADER "Frame %d\nUnknown 1 = 4\n",S_BLOOD1);
        REQUIRE(!Load(patch));REQUIRE(strstr(DPT_DehLastError(),"weapon states"));
        REQUIRE(Load(HEADER "Frame 13\nUnknown 1 = 128\nUnknown 2 = -128\n"));
        REQUIRE(states[S_PISTOL1].misc1==128&&states[S_PISTOL1].misc2==-128);
        REQUIRE(Load(HEADER "Frame 13\nUnknown 1 = 0\n"));
        REQUIRE(states[S_PISTOL1].misc1==0&&states[S_PISTOL1].misc2==-128);
        DPT_DehReset();REQUIRE(states[S_PISTOL1].misc1==0&&states[S_PISTOL1].misc2==0);
    }
    REQUIRE(!DPT_DehLoadLump(-1));REQUIRE(!DPT_DehLoadFile("missing.deh"));
    puts("DeHackEd Misc/Ammo/Weapon-frame/Thing/Frame: classic grammar, strict ranges, atomic rollback, limits, repeated digest and ordering OK");return 0;
}
