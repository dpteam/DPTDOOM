// Bounded IWAD HUD glyphs drawn into RGB before I_InitGraphics/HU_Init.
// Do not initialize gameplay video/input twice. GPL-2.0-or-later.
#include "dpt_connect_screen.h"
#include "doomgeneric.h"
#include "w_wad.h"
#include "z_zone.h"
#include <string.h>
#include <stdio.h>

static unsigned char screen[320*200];
static pixel_t palette[256];
static const unsigned char *glyph[95];
static unsigned int sizes[95];
static int initialized;
static unsigned int U16(const unsigned char *p){return (unsigned int)p[0]|(unsigned int)p[1]<<8;}
static unsigned int U32(const unsigned char *p){return (unsigned int)p[0]|(unsigned int)p[1]<<8|(unsigned int)p[2]<<16|(unsigned int)p[3]<<24;}
static void Init(void)
{
    int lump,i;const unsigned char *colors;
    initialized=1;
    lump=W_CheckNumForName("PLAYPAL");if(lump<0||W_LumpLength(lump)<768)return;
    colors=W_CacheLumpNum(lump,PU_STATIC);
    for(i=0;i<256;++i)palette[i]=(pixel_t)((unsigned)colors[i*3]<<16|(unsigned)colors[i*3+1]<<8|colors[i*3+2]);
    for(i=0;i<95;++i)
    {
        char name[9];snprintf(name,sizeof(name),"STCFN%03d",i+33);lump=W_CheckNumForName(name);
        if(lump>=0){sizes[i]=(unsigned)W_LumpLength(lump);if(sizes[i]>=8)glyph[i]=W_CacheLumpNum(lump,PU_STATIC);}
    }
}
static int Width(unsigned char ch)
{
    unsigned int width;
    if(ch<33||ch>127||!glyph[ch-33])return 4;
    width=U16(glyph[ch-33]);return width>=1&&width<=16?(int)width:4;
}
static void Text(int y,const char *text)
{
    int width=0,x;const char *p;
    for(p=text;*p;++p)width+=Width((unsigned char)*p)+1;
    x=(320-width)/2;
    for(p=text;*p;++p)
    {
        unsigned char ch=(unsigned char)*p;int w=Width(ch),col;
        if(ch>=33&&ch<=127&&glyph[ch-33]&&x>=0&&x+w<=320)
        {
            const unsigned char *patch=glyph[ch-33];unsigned size=sizes[ch-33];
            unsigned height=U16(patch+2);
            if(height<=16&&8u+(unsigned)w*4<=size)
            for(col=0;col<w;++col)
            {
                unsigned offset=U32(patch+8+col*4),posts=0;
                while(offset<size&&patch[offset]!=255&&++posts<=32)
                {
                    unsigned top,count,k;
                    if(size-offset<4)break;
                    top=patch[offset];count=patch[offset+1];
                    if(count>size-offset-4)break;
                    for(k=0;k<count;++k)if(y+top+k<200)
                        screen[(y+top+k)*320+x+col]=patch[offset+3+k];
                    offset+=count+4;
                }
            }
        }
        x+=w+1;
    }
}
void DPT_ConnectScreen(const char *status,const char *transport)
{
    unsigned int x,y;
    if(!initialized)Init();
    memset(screen,0,sizeof(screen));
    Text(55,"DPT.DOOM");Text(88,status);Text(112,transport);Text(153,"WAITING FOR TWO PLAYERS");
    for(y=0;y<DOOMGENERIC_RESY;++y)for(x=0;x<DOOMGENERIC_RESX;++x)
        DG_ScreenBuffer[y*DOOMGENERIC_RESX+x]=palette[screen[(y*200/DOOMGENERIC_RESY)*320+x*320/DOOMGENERIC_RESX]];
    DG_DrawFrame();
}
