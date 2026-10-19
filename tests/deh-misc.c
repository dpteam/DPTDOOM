// Synthetic parser/transaction/digest tests, no proprietary data.
// GPL-2.0-or-later; see LICENSE.
#include "deh_classic.h"
#include "deh_misc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) do { if(!(x)) { fprintf(stderr,"line %d: %s; %s\n",__LINE__,#x,DPT_DehLastError());exit(1); } } while(0)
#define HEADER "Patch File for DeHackEd v3.0\nDoom version = 19\nPatch format = 6\n\nMisc 0\n"
unsigned int numlumps;
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
        HEADER "Thing 1\nHit points = 20\n", HEADER "[CODEPTR]\n", HEADER "Initial Health = 12junk\n",
        "Patch File for DeHackEd v3.0\nDoom version = 109\nPatch format = 6\nMisc 0\nInitial Health = 12\n",
        "Patch File for DeHackEd v3.0\nDoom version = 19\nMisc 0\nInitial Health = 12\n"};
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
    REQUIRE(!DPT_DehLoadLump(-1));REQUIRE(!DPT_DehLoadFile("missing.deh"));
    puts("DeHackEd Misc: classic grammar, strict ranges, atomic rollback, limits, repeated digest and ordering OK");return 0;
}
