// Real config load/save keycode roundtrip, no IWAD. GPL-2.0-or-later.
#include "doomkeys.h"
#include "m_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#define REQUIRE(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
int M_CheckParmWithArgs(char *name,int count){(void)name;(void)count;return 0;}
char **myargv;
char *M_StringJoin(const char *first,...)
{va_list ap;const char *p;size_t n=strlen(first);char *out;va_start(ap,first);while((p=va_arg(ap,const char*)))n+=strlen(p);va_end(ap);out=malloc(n+1);strcpy(out,first);va_start(ap,first);while((p=va_arg(ap,const char*)))strcat(out,p);va_end(ap);return out;}
void M_MakeDirectory(char *p){(void)p;}
void I_Error(char *s,...){fputs(s,stderr);exit(1);}
int main(void)
{
    int fire=KEY_FIRE,use=KEY_USE;FILE *f;
    M_SetConfigDir("");M_SetConfigFilenames("keys.cfg","extra.cfg");
    M_BindVariable("key_fire",&fire);M_BindVariable("key_use",&use);
    M_LoadDefaults();M_SaveDefaults();fire=use=0;M_LoadDefaults();
    REQUIRE(fire==KEY_FIRE&&use==KEY_USE);
    f=fopen("keys.cfg","w");REQUIRE(f);fputs("key_fire 29\nkey_use 57\n",f);fclose(f);
    M_LoadDefaults();REQUIRE(fire==KEY_FIRE&&use==KEY_USE);
    f=fopen("keys.cfg","w");REQUIRE(f);fputs("key_fire 163\nkey_use 162\n",f);fclose(f);
    M_LoadDefaults();REQUIRE(fire==KEY_FIRE&&use==KEY_USE);M_SaveDefaults();
    fire=use=0;M_LoadDefaults();REQUIRE(fire==KEY_FIRE&&use==KEY_USE);
    // An intentional nondefault binding is preserved, not reset to Ctrl/Space.
    f=fopen("keys.cfg","w");REQUIRE(f);fputs("key_fire 33\nkey_use 18\n",f);fclose(f);
    M_LoadDefaults();REQUIRE(fire=='f'&&use=='e');M_SaveDefaults();M_LoadDefaults();REQUIRE(fire=='f'&&use=='e');
    puts("Config keys: fresh/vanilla/raw-legacy/custom bindings survive load-save-load OK");return 0;
}
