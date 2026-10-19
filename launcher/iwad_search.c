// No full-disk recursion, downloads or writes. Steam VDF is bounded token data.
// GPL-2.0-or-later; see LICENSE.
#include "iwad_search.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static BOOL Candidate(launch_profile_t *p,const wchar_t *path)
{
    HANDLE file;unsigned char h[12];DWORD got;LARGE_INTEGER size;uint32_t count,offset;
    if(wcslen(path)>=MAX_PATH || p->iwad[0])return FALSE;
    file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return FALSE;
    if(!GetFileSizeEx(file,&size)||!ReadFile(file,h,12,&got,NULL)||got!=12||memcmp(h,"IWAD",4))
    {CloseHandle(file);return FALSE;}
    CloseHandle(file);
#define LE32(a) ((uint32_t)(a)[0]|(uint32_t)(a)[1]<<8|(uint32_t)(a)[2]<<16|(uint32_t)(a)[3]<<24)
    count=LE32(h+4);offset=LE32(h+8);
#undef LE32
    if(!count||count>1000000||offset<12||size.QuadPart>INT32_MAX
        ||(uint64_t)offset+(uint64_t)count*16>(uint64_t)size.QuadPart)return FALSE;
    wcscpy(p->iwad,path);return TRUE;
}
static BOOL InDirectory(launch_profile_t *p,const wchar_t *dir)
{
    static const wchar_t *names[]={L"doom2.wad",L"doom.wad",L"doom1.wad",L"tnt.wad",L"plutonia.wad"};
    wchar_t path[LP_PATH];unsigned i;
    if(!*dir||wcslen(dir)>MAX_PATH-20)return FALSE;
    for(i=0;i<sizeof(names)/sizeof(names[0]);++i)
    {swprintf(path,LP_PATH,L"%ls\\%ls",dir,names[i]);if(Candidate(p,path))return TRUE;}
    return FALSE;
}
static BOOL SteamLibrary(launch_profile_t *p,const wchar_t *root)
{
    static const wchar_t *dirs[]={L"DOOM + DOOM II\\base",L"DOOM + DOOM II\\rerelease",L"DOOM + DOOM II",
        L"Doom 2\\base",L"Doom 2\\rerelease",L"Doom 2",L"Ultimate Doom\\base",L"Ultimate Doom\\rerelease",
        L"DOOM\\base",L"DOOM\\rerelease",L"DOOM",L"DOOM 3 BFG Edition\\base\\wads",L"Final Doom\\base",L"Final Doom"};
    wchar_t path[LP_PATH];unsigned i;
    if(wcslen(root)>MAX_PATH-90)return FALSE;
    for(i=0;i<sizeof(dirs)/sizeof(dirs[0]);++i)
    {swprintf(path,LP_PATH,L"%ls\\steamapps\\common\\%ls",root,dirs[i]);if(InDirectory(p,path))return TRUE;}
    return FALSE;
}
static int Token(const char **cursor,char *out,size_t capacity)
{
    const char *p=*cursor;size_t n=0;
    while(*p&&*p!='"')
    {
        if(p[0]=='/'&&p[1]=='/')while(*p&&*p!='\n')++p;
        else ++p;
    }
    if(!*p){*cursor=p;return 0;}++p;
    while(*p&&*p!='"')
    {
        char ch=*p++;
        if(ch=='\\'&&(*p=='\\'||*p=='"'))ch=*p++;
        if(n+1>=capacity)return -1;
        out[n++]=ch;
    }
    if(*p!='"')return -1;
    out[n]=0;*cursor=p+1;return 1;
}
BOOL LI_SearchSteam(launch_profile_t *p,const wchar_t *root)
{
    wchar_t path[LP_PATH],library[LP_PATH];FILE *file;long length;char *data,*end;const char *cursor;
    char previous[LP_PATH]="",token[LP_PATH];unsigned tokens=0,libs=0;BOOL found=FALSE;
    if(p->iwad[0]||wcslen(root)>MAX_PATH-40)return FALSE;
    if(SteamLibrary(p,root))return TRUE;
    swprintf(path,LP_PATH,L"%ls\\steamapps\\libraryfolders.vdf",root);
    file=_wfopen(path,L"rb");if(!file)return FALSE;
    if(fseek(file,0,SEEK_END)||(length=ftell(file))<0||length>262144||fseek(file,0,SEEK_SET)){fclose(file);return FALSE;}
    data=(char*)malloc((size_t)length+1);if(!data){fclose(file);return FALSE;}
    if(fread(data,1,(size_t)length,file)!=(size_t)length){fclose(file);free(data);return FALSE;}
    fclose(file);data[length]=0;cursor=data;
    while(tokens++<8192&&libs<64&&Token(&cursor,token,sizeof(token))>0)
    {
        int numeric=*previous!=0;
        for(end=previous;*end;++end)if(*end<'0'||*end>'9')numeric=0;
        if(!strcmp(previous,"path")||numeric)
        {
            if(((strlen(token)>2&&token[1]==':')||(token[0]=='\\'&&token[1]=='\\'))
                &&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,token,-1,library,LP_PATH))
            {++libs;if(SteamLibrary(p,library)){found=TRUE;break;}}
        }
        strcpy(previous,token);
    }
    free(data);return found;
}
static BOOL RegistrySteam(launch_profile_t *p,HKEY root,const wchar_t *key,const wchar_t *value,REGSAM flags)
{
    HKEY handle;wchar_t path[MAX_PATH];DWORD type,size=sizeof(path);BOOL found=FALSE;
    if(RegOpenKeyExW(root,key,0,KEY_READ|flags,&handle)!=ERROR_SUCCESS)return FALSE;
    if(RegQueryValueExW(handle,value,NULL,&type,(BYTE*)path,&size)==ERROR_SUCCESS&&type==REG_SZ&&size>=sizeof(wchar_t)&&size<=sizeof(path))
    {path[MAX_PATH-1]=0;if(path[size/sizeof(wchar_t)-1]==0)found=LI_SearchSteam(p,path);}
    RegCloseKey(handle);return found;
}
BOOL LI_Find(launch_profile_t *p,const wchar_t *base,BOOL system_paths)
{
    wchar_t full[LP_PATH],dir[LP_PATH],env[LP_PATH];DWORD n;unsigned i;
    if(p->iwad[0])return FALSE;
    if(p->executable[0]&&(n=GetFullPathNameW(p->executable,LP_PATH,full,NULL))&&n<LP_PATH)
    {wchar_t *slash=wcsrchr(full,L'\\');if(slash){*slash=0;if(InDirectory(p,full))return TRUE;}}
    if(InDirectory(p,base))return TRUE;
    if(!system_paths)return FALSE;
    n=GetEnvironmentVariableW(L"DOOMWADDIR",env,LP_PATH);
    if(n&&n<LP_PATH&&(Candidate(p,env)||InDirectory(p,env)))return TRUE;
    if(RegistrySteam(p,HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",0)
        ||RegistrySteam(p,HKEY_LOCAL_MACHINE,L"SOFTWARE\\Valve\\Steam",L"InstallPath",KEY_WOW64_32KEY))return TRUE;
    n=GetEnvironmentVariableW(L"ProgramFiles(x86)",env,LP_PATH);
    if(n&&n<LP_PATH-10){swprintf(dir,LP_PATH,L"%ls\\Steam",env);if(LI_SearchSteam(p,dir))return TRUE;}
    for(i=0;i<26;++i)
    {
        wchar_t drive[4]={(wchar_t)(L'A'+i),L':',L'\\',0};
        if(GetDriveTypeW(drive)!=DRIVE_FIXED)continue;
        swprintf(dir,LP_PATH,L"%lsSteamLibrary",drive);if(LI_SearchSteam(p,dir))return TRUE;
        swprintf(dir,LP_PATH,L"%lsSteam",drive);if(LI_SearchSteam(p,dir))return TRUE;
    }
    return FALSE;
}
