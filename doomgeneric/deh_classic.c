// DPT.DOOM bounded classic subset, written for this port.
// No imported ACE/csDoom code, BEX, pointers or executable offsets.
// GPL-2.0-or-later; see LICENSE.
#include "deh_classic.h"
#include "deh_misc.h"
#include "w_wad.h"
#include "d_items.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

enum { MAX_LINE=2047, MAX_SOURCE=262144, MAX_PATCHES=16, FIELD_COUNT=15 };
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
    X("BFG Cells/Shot",bfg_cells_per_shot,40,1,1000)
#define DEFINE(name,field,def,low,high) int deh_##field=def;
MISC_FIELDS(DEFINE)
#undef DEFINE
int deh_species_infighting=DEH_DEFAULT_SPECIES_INFIGHTING;
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

static boolean Error(unsigned int line,const char *message)
{
    snprintf(last_error,sizeof(last_error),"line %u: %s",line,message);
    return false;
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
void DPT_DehReset(void)
{
    unsigned int i;
    if(!initialized)
    {
        memcpy(base_maxammo,maxammo,sizeof(base_maxammo));memcpy(base_clipammo,clipammo,sizeof(base_clipammo));
        memcpy(base_weapons,weaponinfo,sizeof(base_weapons));
    }
    memcpy(maxammo,base_maxammo,sizeof(base_maxammo));memcpy(clipammo,base_clipammo,sizeof(base_clipammo));
    memcpy(weaponinfo,base_weapons,sizeof(base_weapons));
    for(i=0;i<FIELD_COUNT;++i)*fields[i].destination=fields[i].initial;
    deh_species_infighting=DEH_DEFAULT_SPECIES_INFIGHTING;
    loaded=bytes_total=0;last_error[0]=0;
    SHA1_Init(&digest_context);SHA1_UpdateString(&digest_context,DPT_DEH_SUBSET);initialized=true;
}
boolean DPT_DehLoadBytes(const unsigned char *data,size_t size)
{
    int staged[FIELD_COUNT];unsigned int i,line_number=0,assignments=0;
    int staged_max[NUMAMMO],staged_clip[NUMAMMO],staged_weapon[NUMWEAPONS];
    size_t offset=0;boolean signature=false,doom=false,format=false;
    enum { NONE, MISC, AMMO, WEAPON } section=NONE;
    int index=0;
    char line[MAX_LINE+1];
    if(!initialized)DPT_DehReset();
    last_error[0]=0;
    if(!data||!size||size>MAX_SOURCE||size>MAX_SOURCE-bytes_total)return Error(0,"source/total patch bytes exceed 256 KiB or empty source");
    if(loaded>=MAX_PATCHES)return Error(0,"maximum 16 patch sources exceeded");
    if(memchr(data,0,size))return Error(0,"embedded NUL is not valid text");
    for(i=0;i<FIELD_COUNT;++i)staged[i]=*fields[i].destination;
    memcpy(staged_max,maxammo,sizeof(staged_max));memcpy(staged_clip,clipammo,sizeof(staged_clip));
    for(i=0;i<NUMWEAPONS;++i)staged_weapon[i]=weaponinfo[i].ammo;
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
        if(!strchr(text,'='))
        {
            char *separator=strchr(text,' ');
            if(separator)
            {
                int ammo,weapon;
                *separator=0;ammo=Equal(text,"Ammo");weapon=Equal(text,"Weapon");
                if(ammo||weapon)
                {
                    if(!doom||!format)return Error(line_number,"headers required before indexed section");
                    if(!Number(Trim(separator+1),&index)||index<0||index>=(ammo?NUMAMMO:NUMWEAPONS))
                        return Error(line_number,"Ammo/Weapon section index out of range");
                    section=ammo?AMMO:WEAPON;continue;
                }
            }
            return Error(line_number,"unsupported section (Misc 0, Ammo and Weapon subset only)");
        }
        equals=strchr(text,'=');
        if(!equals)return Error(line_number,"unsupported section (Misc 0, Ammo and Weapon subset only)");
        *equals=0;text=Trim(text);
        if(!Number(Trim(equals+1),&value))return Error(line_number,"invalid decimal integer / overflow");
        if(!section)
        {
            if(Equal(text,"Doom version")&&!doom&&value==19)doom=true;
            else if(Equal(text,"Patch format")&&!format&&value==6)format=true;
            else return Error(line_number,"expected Doom version = 19 and Patch format = 6 (no duplicates)");
            continue;
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
            if(!Equal(text,"Ammo type"))return Error(line_number,"unsupported Weapon field (only Ammo type)");
            // Historical overflow/noammo tricks intentionally not supported.
            // In this engine ammo type 5 on a shooting action targets maxammo;
            // do not advertise that memory-layout behavior as safe compatibility.
            if(value<0||value>=NUMAMMO)return Error(line_number,"Weapon Ammo type must be 0..3");
            staged_weapon[index]=value;++assignments;continue;
        }
        for(i=0;i<FIELD_COUNT;++i)if(Equal(text,fields[i].name))break;
        if(i==FIELD_COUNT)return Error(line_number,"unsupported Misc field in " DPT_DEH_SUBSET);
        if(value<fields[i].low||value>fields[i].high)return Error(line_number,"Misc field outside supported safe range");
        staged[i]=value;++assignments;
    }
    if(!section||!assignments)return Error(line_number,"patch has no supported assignments");
    if(staged[1]>staged_max[am_clip])return Error(line_number,"Initial Bullets exceeds Max ammo for bullets");
    // Commit only after the entire source has passed validation.
    for(i=0;i<FIELD_COUNT;++i)*fields[i].destination=staged[i];
    memcpy(maxammo,staged_max,sizeof(staged_max));memcpy(clipammo,staged_clip,sizeof(staged_clip));
    for(i=0;i<NUMWEAPONS;++i)weaponinfo[i].ammo=(ammotype_t)staged_weapon[i];
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
