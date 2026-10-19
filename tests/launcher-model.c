// Data-free model/storage tests. GPL-2.0-or-later; see LICENSE.
#include "launch_model.h"
#include "storage.h"
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#define REQUIRE(expr) do { if (!(expr)) { fprintf(stderr, "line %d: %s\n", __LINE__, #expr); exit(1); } } while (0)
static void File(const wchar_t *path, const char *text)
{
    HANDLE f=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    DWORD n;REQUIRE(f!=INVALID_HANDLE_VALUE);REQUIRE(WriteFile(f,text,(DWORD)strlen(text),&n,NULL));CloseHandle(f);
}
static void Wad(const wchar_t *path, int bad)
{
    unsigned char bytes[28]={'I','W','A','D',1,0,0,0,12,0,0,0,12,0,0,0,0,0,0,0,'M','A','P','0','1',0,0,0};
    HANDLE f;DWORD n;
    if(bad)bytes[8]=255;
    f=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);REQUIRE(f!=INVALID_HANDLE_VALUE);
    REQUIRE(WriteFile(f,bytes,sizeof(bytes),&n,NULL));CloseHandle(f);
}
int main(void)
{
    static launch_profile_t p, loaded;
    wchar_t command[LP_COMMAND]=L"",error[512],cwd[LP_PATH],ini[LP_PATH],selected[LP_PATH],from[LP_PATH],to[LP_PATH];
    const wchar_t *args[]={L"program.exe",L"",L"space here",L"C:\\trailing\\",L"embedded\"quote",L"\\\\\"x",L"& | ^ % test"};
    unsigned value,i;int argc;LPWSTR *argv;BOOL portable;
    REQUIRE(LP_Number(L"65535",1,65535,&value)&&value==65535);
    REQUIRE(!LP_Number(L"65536",1,65535,&value));REQUIRE(!LP_Number(L"-1",0,32,&value));
    REQUIRE(!LP_Number(L"999999999999999999999999",0,65535,&value));REQUIRE(!LP_Number(L"",1,5,&value));
    REQUIRE(LP_IPv4(L"127.0.0.1"));REQUIRE(!LP_IPv4(L"127.0.0.256"));REQUIRE(!LP_IPv4(L"127.0.0.1:2342"));REQUIRE(!LP_IPv4(L"1.2.3."));
    for(i=0;i<sizeof(args)/sizeof(args[0]);++i)REQUIRE(LP_QuoteAppend(command,LP_COMMAND,args[i]));
    argv=CommandLineToArgvW(command,&argc);REQUIRE(argv&&argc==7);
    for(i=0;i<7;++i)REQUIRE(!wcscmp(argv[i],args[i]));LocalFree(argv);
    command[0]=0;REQUIRE(!LP_QuoteAppend(command,3,L"too long"));
    REQUIRE(GetCurrentDirectoryW(LP_PATH,cwd));
    swprintf(ini,LP_PATH,L"%ls\\profiles.ini",cwd);
    LP_Defaults(&p);wcscpy(p.executable,L"engine with spaces.exe");wcscpy(p.iwad,L"doom2.wad");
    REQUIRE(LP_Build(&p,command,LP_COMMAND,error,512));REQUIRE(!wcsstr(command,L"-net-"));REQUIRE(!wcsstr(command,L"-dmflags"));
    p.mode=1;p.dmflags=3;p.lan=1;
    REQUIRE(LP_Build(&p,command,LP_COMMAND,error,512));REQUIRE(wcsstr(command,L"-net-host"));REQUIRE(wcsstr(command,L"0.0.0.0"));
    p.mode=2;REQUIRE(LP_Build(&p,command,LP_COMMAND,error,512));REQUIRE(wcsstr(command,L"-net-join"));REQUIRE(!wcsstr(command,L"-net-bind"));
    wcscpy(p.address,L"bad");REQUIRE(!LP_Build(&p,command,LP_COMMAND,error,512));wcscpy(p.address,L"127.0.0.1");
    p.mod_count=2;wcscpy(p.mods[0],L"first.wad");wcscpy(p.mods[1],L"second.wad");
    for(i=1;i<=3;++i){p.skill=i;REQUIRE(LP_Save(ini,i,&p));}
    { wchar_t endpoint[256];
      GetPrivateProfileStringW(L"Master",L"Endpoint",L"",endpoint,256,ini);
      REQUIRE(!wcscmp(endpoint,LP_MASTER_ENDPOINT));
      REQUIRE(WritePrivateProfileStringW(L"Master",L"Endpoint",L"localhost:9999",ini));
      p.skill=1;
      REQUIRE(LP_Save(ini,1,&p));
      GetPrivateProfileStringW(L"Master",L"Endpoint",L"",endpoint,256,ini);
      REQUIRE(!wcscmp(endpoint,L"localhost:9999")); }
    for(i=1;i<=3;++i){REQUIRE(LP_Load(ini,i,&loaded));REQUIRE(loaded.skill==i&&loaded.mod_count==2&&!wcscmp(loaded.mods[1],L"second.wad"));}
    p.mod_count=0;REQUIRE(LP_Save(ini,1,&p));REQUIRE(LP_Load(ini,1,&loaded));REQUIRE(!loaded.mod_count);
    REQUIRE(!LP_Load(ini,4,&loaded));
    wcscpy(p.iwad,L"Unicode-\x4e2d\x6587.wad");REQUIRE(LP_Save(ini,3,&p));
    REQUIRE(LP_Load(ini,3,&loaded)&&!wcscmp(p.iwad,loaded.iwad));
    LP_Defaults(&p);wcscpy(p.executable,L"engine.exe");wcscpy(p.iwad,L"valid.wad");File(L"engine.exe","fixture");Wad(L"valid.wad",0);
    REQUIRE(LP_ValidateFiles(&p,error,512)&&p.commercial);p.map=2;REQUIRE(!LP_ValidateFiles(&p,error,512));p.map=1;
    Wad(L"valid.wad",1);REQUIRE(!LP_ValidateFiles(&p,error,512));
    REQUIRE(LS_Select(cwd,L"X:\\FakeAppData",L"",selected,&portable));REQUIRE(!portable&&!wcscmp(selected,L"X:\\FakeAppData\\DPTDOOM"));
    File(L"portable.txt","");REQUIRE(LS_Select(cwd,NULL,L"",selected,&portable)&&portable);REQUIRE(wcsstr(selected,L"\\user"));REQUIRE(LS_Writable(selected));
    REQUIRE(LS_Select(cwd,NULL,L"override",selected,&portable));REQUIRE(wcsstr(selected,L"\\override"));
    REQUIRE(LS_Writable(L"source"));REQUIRE(LS_Writable(L"destination"));
    File(L"source\\launcher.ini","original");File(L"destination\\launcher.ini","keep");
    REQUIRE(LS_Writable(L"source\\game"));REQUIRE(LS_Writable(L"source\\game\\profile1"));
    File(L"source\\game\\profile1\\default.cfg","settings");File(L"source\\game\\profile1\\private.wad","no copy");
    swprintf(from,LP_PATH,L"%ls\\source",cwd);swprintf(to,LP_PATH,L"%ls\\destination",cwd);
    REQUIRE(LS_Import(from,to));REQUIRE(GetFileAttributesW(L"destination\\game\\profile1\\default.cfg")!=INVALID_FILE_ATTRIBUTES);
    REQUIRE(GetFileAttributesW(L"destination\\game\\profile1\\private.wad")==INVALID_FILE_ATTRIBUTES);
    {char text[8]={0};DWORD n;HANDLE f=CreateFileW(L"destination\\launcher.ini",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);REQUIRE(ReadFile(f,text,7,&n,NULL));CloseHandle(f);REQUIRE(!strcmp(text,"keep"));}
    REQUIRE(LS_Import(from,to));File(L"unwritable","file");REQUIRE(!LS_Writable(L"unwritable"));
    puts("Launcher model: quoting, arguments, WAD bounds, profiles, portable selection/import/no-overwrite OK");return 0;
}
