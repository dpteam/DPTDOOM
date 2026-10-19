// DPT.DOOM local HTML launcher / WebView2 host. GPL-2.0-or-later.
#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include "launch_model.h"
#include "storage.h"
#include "../master/client.h"
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <WebView2.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <deque>
#include <cstdio>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
static HWND window;
static ComPtr<ICoreWebView2Controller> controller;
static ComPtr<ICoreWebView2> web;
static launch_profile_t profile;
static std::wstring base, data, ini, appdata, logPath, workDir;
static HANDLE child, gameLock;
static int slot = 1;
static BOOL portable;
static bool importOffered;
static const wchar_t *uiOrigin=L"https://dptdoom.local.invalid/";
static std::deque<std::wstring> pendingMessages;
static bool processingMessage;
static std::wstring masterEndpoint=LP_MASTER_ENDPOINT;
static std::wstring networkLogPath, resolvedEndpoint;
static void NetworkLog(const std::string &text)
{
    if(networkLogPath.empty())return;
    SYSTEMTIME t;GetSystemTime(&t);char stamp[64];
    sprintf(stamp,"[%04u-%02u-%02uT%02u:%02u:%02uZ] ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    HANDLE file=CreateFileW(networkLogPath.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    LARGE_INTEGER size;
    if(GetFileSizeEx(file,&size)&&size.QuadPart>1047552)
    {
        CloseHandle(file);
        if(!MoveFileExW(networkLogPath.c_str(),(networkLogPath+L".1").c_str(),MOVEFILE_REPLACE_EXISTING))return;
        file=CreateFileW(networkLogPath.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return;
    }
    DWORD written;std::string line=stamp+text+"\r\n";WriteFile(file,line.data(),static_cast<DWORD>(line.size()),&written,nullptr);CloseHandle(file);
}
static std::string resolvedMaster;
static bool publishHost;
static bool masterSettingsValid=true;
static std::wstring sessionName=L"DPT.DOOM COOP";
static std::vector<unsigned char> serverRows;
static std::wstring relayMaster,relaySession;
struct DirectoryJob
{
    std::string host;
    unsigned int port=0;
    bool list=false,ok=false;
    std::string ip;
    std::string diagnostic;
    std::vector<unsigned char> rows;
};
static DirectoryJob *directoryJob;
static HANDLE directoryThread;
static ULONGLONG directoryDeadline;
static bool directoryTimedOut, launchAfterResolve;
static ULONGLONG rowsReceived;
static bool masterSettingsMessageFailed;
static launch_profile_t pendingLaunchProfile;
static std::wstring pendingLaunchEndpoint,pendingLaunchName;

static void Error(const wchar_t *text) { MessageBoxW(window, text, L"DPT.DOOM Launcher", MB_OK | MB_ICONWARNING); }
static std::wstring Quote(const std::wstring &s)
{
    std::wstring out = L"\"";
    for (wchar_t ch : s)
    {
        if (ch == L'"' || ch == L'\\') { out += L'\\'; out += ch; }
        else if (ch < 32 || ch == L'<' || ch == 0x2028 || ch == 0x2029)
        { wchar_t hex[8]; swprintf(hex, 8, L"\\u%04x", static_cast<unsigned>(ch)); out += hex; }
        else out += ch;
    }
    return out + L'"';
}
static void Send(const std::wstring &json) { if (web) web->PostWebMessageAsJson(json.c_str()); }
static void Status(const std::wstring &text, bool error = false)
{
    Send(L"{\"status\":" + Quote(text) + L",\"error\":" + (error ? L"true" : L"false")
        + L",\"running\":" + (child ? L"true" : L"false") + L",\"hasLog\":" + (logPath.empty() ? L"false" : L"true") + L"}");
}
static void State(bool withProfile = true)
{
    wchar_t command[LP_COMMAND], error[512];
    LP_Build(&profile, command, LP_COMMAND, error, 512);
    std::wstring json = L"{\"preview\":" + Quote(error[0] ? error : command)
        + L",\"storage\":" + Quote(data) + L",\"portable\":" + (portable ? L"true" : L"false")
        + L",\"running\":" + (child ? L"true" : L"false") + L",\"hasLog\":" + (logPath.empty() ? L"false" : L"true");
    if (withProfile)
    {
        json += L",\"profile\":{\"executable\":" + Quote(profile.executable) + L",\"iwad\":" + Quote(profile.iwad)
            + L",\"address\":" + Quote(profile.address);
#define NUMBER(field) json += L",\"" L#field L"\":" + std::to_wstring(profile.field)
        NUMBER(mode); NUMBER(episode); NUMBER(map); NUMBER(skill); NUMBER(port); NUMBER(lan);
        NUMBER(nomonsters); NUMBER(fast); NUMBER(respawn);
#undef NUMBER
        json += L",\"extra\":" + std::to_wstring(profile.dmflags & 1) + L",\"pass\":" + std::to_wstring(profile.dmflags & 2) + L",\"mods\":[";
        for (unsigned int i=0; i<profile.mod_count; ++i) { if (i) json += L","; json += Quote(profile.mods[i]); }
        json += L"]}";
    }
    if(withProfile)json+=L",\"masterEndpoint\":"+Quote(masterEndpoint)+L",\"publishHost\":"+(publishHost?L"true":L"false")+L",\"sessionName\":"+Quote(sessionName);
    Send(json + L"}");
}
static DWORD WINAPI DirectoryWorker(void *argument)
{
    auto job=static_cast<DirectoryJob*>(argument);
    WSADATA wsa;
    int startup=WSAStartup(MAKEWORD(2,2),&wsa);
    if(startup){job->diagnostic="WSAStartup error="+std::to_string(startup);return 0;}
    addrinfo hints={},*addresses=nullptr;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
    int dns=getaddrinfo(job->host.c_str(),nullptr,&hints,&addresses);
    if(!dns&&addresses)
    {
        char ip[INET_ADDRSTRLEN];
        auto address=reinterpret_cast<sockaddr_in*>(addresses->ai_addr);
        if(inet_ntop(AF_INET,&address->sin_addr,ip,sizeof(ip)))job->ip=ip;
        freeaddrinfo(addresses);
    }
    job->diagnostic="dns_host="+job->host+" port="+std::to_string(job->port)+" dns_error="+std::to_string(dns)
        +" resolved_IPv4="+(job->ip.empty()?"none":job->ip)+" (first IPv4 result; OS route may use VPN/tunnel)";
    if(!job->ip.empty())
    {
        if(!job->list)job->ok=true;
        else
        {
            dm_client_query_t query;MC_Init(&query);
            MC_Begin(&query,job->ip.c_str(),job->port,DM_LIST,nullptr,0,GetTickCount64());
            int result=0;
            while(!result){result=MC_Poll(&query,GetTickCount64());if(!result)Sleep(5);}
            char diagnostic[1024];MC_Diagnostic(&query,diagnostic,sizeof(diagnostic));job->diagnostic+="; ";job->diagnostic+=diagnostic;
            if(result>0&&!query.status)
            {
                unsigned count=DM_Get32(query.response+DM_HEADER);
                job->rows.assign(query.response+DM_HEADER+4,query.response+DM_HEADER+4+count*DM_ROW);job->ok=true;
            }
            MC_Close(&query);
        }
    }
    WSACleanup();return 0;
}
static bool MasterFields(const std::vector<std::wstring> &f)
{
    if(f.size()!=6 || f[2].size()>260 || f[4].empty() || f[4].size()>63 || (f[3]!=L"0"&&f[3]!=L"1"))return false;
    std::string endpoint;
    for(wchar_t ch:f[2]){if(ch>127)return false;endpoint+=static_cast<char>(ch);}
    char host[254];unsigned port;
    if(!MC_Endpoint(endpoint.c_str(),host,&port))return false;
    for(wchar_t ch:f[4])if(ch<32||ch>126)return false;
    if(f[4].front()==L' '||f[4].back()==L' ')return false;
    if(masterEndpoint!=f[2]){serverRows.clear();resolvedMaster.clear();relayMaster.clear();relaySession.clear();if(directoryThread){directoryTimedOut=true;launchAfterResolve=false;}}
    masterEndpoint=f[2];publishHost=f[3]==L"1";sessionName=f[4];return true;
}
static bool StartDirectory(bool list)
{
    if(directoryThread){Status(L"Предыдущий запрос master ещё завершается.",true);return false;}
    std::string endpoint(masterEndpoint.begin(),masterEndpoint.end());char host[254];unsigned port;
    if(!MC_Endpoint(endpoint.c_str(),host,&port)){Status(L"Master: нужен IP/домен:порт без URL, пути и пробелов.",true);return false;}
    directoryJob=new DirectoryJob;directoryJob->host=host;directoryJob->port=port;directoryJob->list=list;
    NetworkLog(std::string(list?"LIST requested ":"publication DNS requested ")+host+":"+std::to_string(port));
    directoryThread=CreateThread(nullptr,0,DirectoryWorker,directoryJob,0,nullptr);
    if(!directoryThread){delete directoryJob;directoryJob=nullptr;Status(L"Не удалось создать запрос master.",true);return false;}
    directoryDeadline=GetTickCount64()+5000;directoryTimedOut=false;
    if(list){serverRows.clear();Send(L"{\"servers\":[]}");}
    Status(list?L"Master: запрашиваю список…":L"Master: разрешаю адрес для публикации…");return true;
}
static void DirectoryState()
{
    std::wstring json=L"{\"servers\":[";
    for(size_t offset=0;offset<serverRows.size();offset+=DM_ROW)
    {
        const auto row=serverRows.data()+offset;const auto info=row+24;
        std::wstring name,map;for(unsigned i=0;i<64&&info[i];++i)name+=info[i];for(unsigned i=116;i<124&&info[i];++i)map+=info[i];
        if(offset)json+=L",";
        json+=L"{\"name\":"+Quote(name)+L",\"map\":"+Quote(map)+L",\"address\":"
            +Quote(std::to_wstring(row[16])+L"."+std::to_wstring(row[17])+L"."+std::to_wstring(row[18])+L"."+std::to_wstring(row[19]))
            +L",\"port\":"+std::to_wstring(DM_Get16(info+124))+L",\"skill\":"+std::to_wstring(info[126])
            +L",\"flags\":"+std::to_wstring(info[127])+L",\"ttl\":"+std::to_wstring(DM_Get32(row+20))
            +L",\"relay\":"+((DM_Get32(row+156)&DM_RELAY_AVAILABLE)?L"true":L"false")+L"}";
    }
    Send(json+L"]}");
}

// Bridge is a versioned, bounded list of encodeURIComponent fields. No JS
// evaluation, shell strings, arbitrary filesystem operations or host objects.
static bool Decode(const std::wstring &s, std::wstring &out)
{
    std::string bytes;
    for (size_t i=0; i<s.size(); ++i)
    {
        if (s[i] == L'%')
        {
            auto hex=[](wchar_t c)->int { if(c>=L'0'&&c<=L'9')return c-L'0';if(c>=L'A'&&c<=L'F')return c-L'A'+10;if(c>=L'a'&&c<=L'f')return c-L'a'+10;return -1; };
            if (i+2>=s.size()) return false;
            int a=hex(s[i+1]), b=hex(s[i+2]);
            if(a<0||b<0||!(a*16+b))return false;
            bytes += static_cast<char>(a*16+b); i+=2;
        }
        else { if(s[i]<32 || s[i]>126)return false; bytes+=static_cast<char>(s[i]); }
    }
    int n=MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (!n && !bytes.empty()) return false;
    out.resize(n);
    if(n)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), &out[0], n);
    return true;
}
static bool Parse(const wchar_t *message, std::vector<std::wstring> &fields)
{
    if (wcslen(message)>150000) return false;
    std::wstring text(message);
    size_t start=0;
    for(;;)
    {
        size_t end=text.find(L'\n',start);
        std::wstring value;
        if(!Decode(text.substr(start,end==std::wstring::npos?end:end-start),value))return false;
        fields.push_back(value);
        if(fields.size()>17+LP_MODS)return false;
        if(end==std::wstring::npos)break;
        start=end+1;
    }
    return (fields.size()>=17 && fields[0]==L"1") || (fields.size()==6 && fields[0]==L"M1");
}
static bool Read(const std::vector<std::wstring> &f)
{
    launch_profile_t p = profile;
    unsigned int value;
    if(!LP_Number(f[2].c_str(),1,3,&value))return false;
    int newSlot=static_cast<int>(value);
    if(!LP_Number(f[3].c_str(),0,2,&value))return false;
    p.mode=static_cast<int>(value);
    if(f[4].size()>=LP_PATH || f[5].size()>=LP_PATH || f[9].size()>=64)return false;
    wcscpy(p.executable,f[4].c_str());wcscpy(p.iwad,f[5].c_str());wcscpy(p.address,f[9].c_str());
    if(!LP_Number(f[6].c_str(),1,4,&p.episode)||!LP_Number(f[7].c_str(),1,32,&p.map)
        ||!LP_Number(f[8].c_str(),1,5,&p.skill)||!LP_Number(f[10].c_str(),1,65535,&p.port))return false;
    for(size_t i=11;i<17;++i)if(f[i]!=L"0"&&f[i]!=L"1")return false;
    p.lan=f[11]==L"1";p.nomonsters=f[12]==L"1";p.fast=f[13]==L"1";p.respawn=f[14]==L"1";
    p.dmflags=(f[15]==L"1"?1:0)|(f[16]==L"1"?2:0);
    p.mod_count=static_cast<unsigned>(f.size()-17);
    for(unsigned i=0;i<p.mod_count;++i){if(f[17+i].empty()||f[17+i].size()>=LP_PATH)return false;wcscpy(p.mods[i],f[17+i].c_str());}
    slot=newSlot;profile=p;return true;
}

static bool Browse(wchar_t *path, bool exe)
{
    OPENFILENAMEW info={};info.lStructSize=sizeof(info);info.hwndOwner=window;
    info.lpstrFilter=exe?L"DPT.DOOM executable\0*.exe\0\0":L"Doom WAD\0*.wad\0\0";
    info.lpstrFile=path;info.nMaxFile=LP_PATH;info.lpstrTitle=L"DPT.DOOM — выбрать файл";
    info.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&info)!=FALSE;
}
static void LoadProfile()
{
    LP_Load(ini.c_str(),slot,&profile);
    if(!profile.executable[0])wcscpy(profile.executable,L".\\dptdoom.exe");
    // Relative profile paths are anchored to the launcher, not its caller's CWD.
}
static bool SaveProfile()
{
    launch_profile_t stored=profile;
    auto relative=[](wchar_t *path){
        if(wcslen(path)>base.size() && !_wcsnicmp(path,base.c_str(),base.size()) && path[base.size()]==L'\\')
        {std::wstring value=L"."+std::wstring(path+base.size());wcscpy(path,value.c_str());}
    };
    if(portable){relative(stored.executable);relative(stored.iwad);for(unsigned i=0;i<stored.mod_count;++i)relative(stored.mods[i]);}
    if(!LP_Save(ini.c_str(),slot,&stored))return false;
    return WritePrivateProfileStringW(L"Master",L"Endpoint",masterEndpoint.c_str(),ini.c_str())
        &&WritePrivateProfileStringW(L"Master",L"Publish",publishHost?L"1":L"0",ini.c_str())
        &&WritePrivateProfileStringW(L"Master",L"SessionName",sessionName.c_str(),ini.c_str());
}
static void OpenLog()
{
    if(logPath.empty())return;
    wchar_t editor[LP_PATH], command[LP_COMMAND]=L"";
    if(!GetSystemDirectoryW(editor,LP_PATH-16))return;
    wcscat(editor,L"\\notepad.exe");LP_QuoteAppend(command,LP_COMMAND,editor);LP_QuoteAppend(command,LP_COMMAND,logPath.c_str());
    STARTUPINFOW startup={};startup.cb=sizeof(startup);PROCESS_INFORMATION process={};
    if(!CreateProcessW(editor,command,nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)){Status(L"Не удалось открыть Notepad. Лог: "+logPath,true);return;}
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
}
static bool DataPathSupported(const std::wstring &path)
{
    BOOL substituted=FALSE;char buffer[LP_PATH*4];
    if(path.size()>=MAX_PATH-40)return false;
    if(GetACP()==CP_UTF8)return WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path.c_str(),-1,buffer,sizeof(buffer),nullptr,nullptr)!=0;
    return WideCharToMultiByte(CP_ACP,WC_NO_BEST_FIT_CHARS,path.c_str(),-1,buffer,sizeof(buffer),nullptr,&substituted)&&!substituted;
}
static void Launch()
{
    if(child)return;
    if(profile.mode==1&&publishHost&&!masterSettingsValid){Status(L"Исправь настройки master перед публикацией.",true);return;}
    if(profile.mode==1&&publishHost&&resolvedMaster.empty())
    {
        pendingLaunchProfile=profile;pendingLaunchEndpoint=masterEndpoint;pendingLaunchName=sessionName;
        launchAfterResolve=StartDirectory(false);return;
    }
    wchar_t error[512], command[LP_COMMAND];
    if(!LP_ValidateFiles(&profile,error,512)||!LP_Build(&profile,command,LP_COMMAND,error,512)){Status(error,true);return;}
    if(profile.mode==1&&publishHost)
    {
        std::string endpoint(masterEndpoint.begin(),masterEndpoint.end());char host[254];unsigned port;
        if(!MC_Endpoint(endpoint.c_str(),host,&port))return;
        std::wstring numeric(resolvedMaster.begin(),resolvedMaster.end());numeric+=L":"+std::to_wstring(port);
        if(!LP_QuoteAppend(command,LP_COMMAND,L"-net-relay")||!LP_QuoteAppend(command,LP_COMMAND,L"-master")||!LP_QuoteAppend(command,LP_COMMAND,numeric.c_str())
            ||!LP_QuoteAppend(command,LP_COMMAND,L"-session-name")||!LP_QuoteAppend(command,LP_COMMAND,sessionName.c_str()))
        {Status(L"Слишком длинная команда.",true);return;}
    }
    if(profile.mode==2&&!relaySession.empty())
    {
        if(!LP_QuoteAppend(command,LP_COMMAND,L"-relay-master")||!LP_QuoteAppend(command,LP_COMMAND,relayMaster.c_str())
            ||!LP_QuoteAppend(command,LP_COMMAND,L"-relay-session")||!LP_QuoteAppend(command,LP_COMMAND,relaySession.c_str()))
        {Status(L"Слишком длинная команда relay.",true);return;}
    }
    State();
    if(profile.mode==1&&profile.lan&&MessageBoxW(window,L"Разрешить подключения со всех IPv4-интерфейсов?\n\nТолько доверенная LAN: без пароля, шифрования и NAT traversal. Не открывайте порт в публичный Интернет.",L"DPT.DOOM — LAN host",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
    workDir=data+L"\\game";
    if(!LS_Writable(workDir.c_str())){Status(L"Не удалось создать папку игровых данных.",true);return;}
    workDir+=L"\\profile"+std::to_wstring(slot);
    if(!DataPathSupported(workDir)||!LS_Writable(workDir.c_str())){Status(L"Каталог игры недоступен, слишком длинный или не представим в ANSI-кодировке движка.",true);return;}
    // Exclusive lock is inherited by the engine: closing the launcher must not
    // permit another instance to overwrite the running profile's saves/config.
    SECURITY_ATTRIBUTES security={sizeof(security),nullptr,TRUE};
    gameLock=CreateFileW((workDir+L"\\.running").c_str(),GENERIC_READ|GENERIC_WRITE,0,&security,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(gameLock==INVALID_HANDLE_VALUE){gameLock=nullptr;Status(L"Этот профиль уже используется другой игрой либо недоступен для записи.",true);return;}
    auto unlock=[](){if(gameLock){CloseHandle(gameLock);gameLock=nullptr;}};
    if(!SaveProfile()){unlock();Status(L"Не удалось сохранить профиль.",true);return;}
    std::wstring sessions=data+L"\\sessions";
    if(!LS_Writable(sessions.c_str())){unlock();Status(L"Не удалось создать каталог логов.",true);return;}
    SYSTEMTIME now;GetLocalTime(&now);wchar_t name[128];
    swprintf(name,128,L"\\%04u%02u%02u-%02u%02u%02u-%03u-%lu",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,GetCurrentProcessId());
    std::wstring session=sessions+name;
    if(!CreateDirectoryW(session.c_str(),nullptr)){unlock();Status(L"Не удалось создать уникальный каталог сессии. Повторите запуск.",true);return;}
    logPath=session+L"\\game.log";
    HANDLE log=CreateFileW(logPath.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(log==INVALID_HANDLE_VALUE||input==INVALID_HANDLE_VALUE){if(log!=INVALID_HANDLE_VALUE)CloseHandle(log);if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);unlock();Status(L"Не удалось открыть лог сессии.",true);return;}
    STARTUPINFOEXW startup={};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput=startup.StartupInfo.hStdError=log;startup.StartupInfo.hStdInput=input;
    SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);
    std::vector<unsigned char> attributes(size);startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    HANDLE inherit[]={log,input,gameLock};PROCESS_INFORMATION process={};
    if(profile.mode==1&&publishHost)
    {
        std::string context="Launcher master DNS: "+std::string(resolvedEndpoint.begin(),resolvedEndpoint.end())+" resolved_IPv4="+resolvedMaster+"\r\n";
        DWORD written;WriteFile(log,context.data(),static_cast<DWORD>(context.size()),&written,nullptr);
    }
    bool initialized=InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&size)!=FALSE;
    BOOL created=FALSE;
    if(initialized&&UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherit,sizeof(inherit),nullptr,nullptr))
        created=CreateProcessW(profile.executable,command,nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW,nullptr,workDir.c_str(),&startup.StartupInfo,&process);
    DWORD failure=GetLastError();
    if(initialized)DeleteProcThreadAttributeList(startup.lpAttributeList);
    CloseHandle(log);CloseHandle(input);
    if(!created){unlock();Status(L"Ошибка запуска Windows "+std::to_wstring(failure)+L". Проверьте EXE и DLL. Лог: "+logPath,true);return;}
    child=process.hProcess;CloseHandle(process.hThread);
    NetworkLog("game PID="+std::to_string(process.dwProcessId)+" mode="+std::to_string(profile.mode)+" publish="+std::to_string(publishHost));
    Status(L"Игра запущена · PID "+std::to_wstring(process.dwProcessId)+L"\nНастройки и saves: "+workDir);
}

static void Message(const wchar_t *text)
{
    std::vector<std::wstring> f;
    if(!Parse(text,f)){Status(L"Отклонено некорректное сообщение интерфейса.",true);return;}
    const auto &action=f[1];
    if(f[0]==L"M1")
    {
        if(!MasterFields(f)){masterSettingsValid=false;masterSettingsMessageFailed=action==L"settings"&&f[3]==L"1";Status(L"Проверь master IP/домен:порт и имя сессии (ASCII 1–63).",true);return;}
        masterSettingsMessageFailed=false;
        masterSettingsValid=true;
        if(action==L"settings")return;
        if(action==L"masterSave"){bool ok=SaveProfile();Status(ok?L"Настройки master сохранены.":L"Не удалось сохранить master.",!ok);return;}
        if(action==L"refresh"){StartDirectory(true);return;}
        if(action==L"select")
        {
            unsigned value;
            if(!LP_Number(f[5].c_str(),0,DM_MAX_SERVERS-1,&value)||value*DM_ROW>=serverRows.size())return;
            const auto row=serverRows.data()+value*DM_ROW;const auto info=row+24;
            if(GetTickCount64()-rowsReceived>=static_cast<ULONGLONG>(DM_Get32(row+20))*1000)
            {Status(L"Запись устарела. Обнови список.",true);return;}
            profile.mode=2;profile.port=DM_Get16(info+124);profile.skill=info[126];profile.dmflags=info[127]&3;
            relaySession.clear();relayMaster.clear();
            if(DM_Get32(row+156)&DM_RELAY_AVAILABLE)
            {
                static const wchar_t hex[]=L"0123456789abcdef";
                for(unsigned i=0;i<16;++i){relaySession+=hex[row[i]>>4];relaySession+=hex[row[i]&15];}
                std::string endpoint(masterEndpoint.begin(),masterEndpoint.end());char host[254];unsigned port;
                if(MC_Endpoint(endpoint.c_str(),host,&port))relayMaster=std::wstring(resolvedMaster.begin(),resolvedMaster.end())+L":"+std::to_wstring(port);
            }
            profile.nomonsters=(info[127]&4)!=0;profile.fast=(info[127]&8)!=0;profile.respawn=(info[127]&16)!=0;
            swprintf(profile.address,64,L"%u.%u.%u.%u",row[16],row[17],row[18],row[19]);
            if(info[116]=='M'){profile.commercial=1;profile.map=(info[119]-'0')*10+info[120]-'0';}
            else{profile.commercial=0;profile.episode=info[117]-'0';profile.map=info[119]-'0';}
            State();Status(L"Сессия выбрана. Проверь IWAD/PWAD/порядок и нажми запуск. Доступность порта не проверена.");return;
        }
        Status(L"Неизвестная команда master.",true);return;
    }
    unsigned int selected;
    if(!LP_Number(f[2].c_str(),1,3,&selected))return;
    if(action==L"ready")
    {
        if(portable&&!importOffered)
        {
            importOffered=true;
            if(GetFileAttributesW(ini.c_str())==INVALID_FILE_ATTRIBUTES&&!appdata.empty()
                &&GetFileAttributesW((appdata+L"\\DPTDOOM\\launcher.ini").c_str())!=INVALID_FILE_ATTRIBUTES
                &&MessageBoxW(window,L"Portable-режим: скопировать профили, игровые настройки и сохранения из AppData в user?\n\nОригиналы останутся на месте. Существующие файлы не перезаписываются. Кэш браузера и логи не копируются.",L"DPT.DOOM — перенос настроек",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES)
            {
                if(!LS_Import((appdata+L"\\DPTDOOM").c_str(),data.c_str()))Error(L"Часть настроек не скопирована. Оригиналы сохранены, существующие файлы не перезаписаны.");
                LoadProfile();
            }
        }
        State();Status(L"Выбери ресурсы и запускай. Список серверов обновляется только по запросу.");return;
    }
    if(action==L"load"){relaySession.clear();relayMaster.clear();slot=selected;LoadProfile();State();Status(L"Профиль загружен.");return;}
    if(action==L"log"){OpenLog();return;}
    {launch_profile_t previous=profile;
     if(!Read(f)){Status(L"Проверь числа: эпизод 1–4, карта 1–32, сложность 1–5, порт 1–65535.",true);return;}
     if(previous.mode!=profile.mode||previous.port!=profile.port||wcscmp(previous.address,profile.address))
     {relaySession.clear();relayMaster.clear();}}
    if(action==L"preview"){State(false);return;}
    if(action==L"save"){bool ok=SaveProfile();Status(ok?L"Профиль сохранён.":L"Не удалось сохранить профиль.",!ok);return;}
    if(action==L"launch"){if(masterSettingsMessageFailed&&profile.mode==1)return;Launch();return;}
    if(action==L"browseExe"||action==L"browseIwad"||action==L"addMod")
    {
        wchar_t path[LP_PATH]=L"";
        if(action==L"addMod"&&profile.mod_count>=LP_MODS){Status(L"Максимум 16 PWAD.",true);return;}
        if(Browse(path,action==L"browseExe"))
        {
            if(action==L"browseExe")wcscpy(profile.executable,path);
            else if(action==L"browseIwad")wcscpy(profile.iwad,path);
            else wcscpy(profile.mods[profile.mod_count++],path);
            State();
        }
        return;
    }
    Status(L"Неизвестная команда интерфейса отклонена.",true);
}

static HRESULT InitWeb(ICoreWebView2Controller *value, ICoreWebView2Environment *environment)
{
    controller=value;controller->get_CoreWebView2(&web);
    RECT rect;GetClientRect(window,&rect);controller->put_Bounds(rect);
    ComPtr<ICoreWebView2Settings> settings;web->get_Settings(&settings);
    settings->put_AreDevToolsEnabled(FALSE);settings->put_AreDefaultContextMenusEnabled(FALSE);settings->put_IsStatusBarEnabled(FALSE);
    settings->put_AreDefaultScriptDialogsEnabled(FALSE);
    ComPtr<ICoreWebView2Settings3> settings3;if(SUCCEEDED(settings.As(&settings3)))settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
    EventRegistrationToken token;
    web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs *args)->HRESULT{
        LPWSTR uri=nullptr;args->get_Uri(&uri);args->put_Cancel(!uri||wcscmp(uri,uiOrigin));CoTaskMemFree(uri);return S_OK;
    }).Get(),&token);
    web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs *args)->HRESULT{args->put_Handled(TRUE);return S_OK;}).Get(),&token);
    web->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs *args)->HRESULT{args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);return S_OK;}).Get(),&token);
    web->AddWebResourceRequestedFilter(L"*",COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    ComPtr<ICoreWebView2Environment> retainedEnvironment=environment;
    web->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>([retainedEnvironment](ICoreWebView2*,ICoreWebView2WebResourceRequestedEventArgs *args)->HRESULT{
        ComPtr<ICoreWebView2WebResourceResponse> response;
        ComPtr<ICoreWebView2WebResourceRequest> request;args->get_Request(&request);
        LPWSTR uri=nullptr;request->get_Uri(&uri);
        bool page=uri&&!wcscmp(uri,uiOrigin);
        bool css=uri&&!wcscmp(uri,L"https://dptdoom.local.invalid/theme.css");
        bool trusted=page||css;
        CoTaskMemFree(uri);
        ComPtr<IStream> content;
        if(trusted)
        {
            HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(page?101:102),RT_RCDATA);
            HGLOBAL loaded=resource?LoadResource(nullptr,resource):nullptr;
            if(!loaded)return E_FAIL;
            content.Attach(SHCreateMemStream(static_cast<const BYTE*>(LockResource(loaded)),SizeofResource(nullptr,resource)));
            if(!content)return E_OUTOFMEMORY;
        }
        const wchar_t *headers=page?L"Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store"
            :css?L"Content-Type: text/css; charset=utf-8\r\nCache-Control: no-store":L"Content-Type: text/plain";
        HRESULT hr=retainedEnvironment->CreateWebResourceResponse(content.Get(),trusted?200:403,trusted?L"OK":L"Blocked",headers,&response);
        return FAILED(hr)?hr:args->put_Response(response.Get());
    }).Get(),&token);
    ComPtr<ICoreWebView2_4> web4;if(SUCCEEDED(web.As(&web4)))web4->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>([](ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs *args)->HRESULT{args->put_Cancel(TRUE);return S_OK;}).Get(),&token);
    web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs *args)->HRESULT{
        LPWSTR source=nullptr,text=nullptr;args->get_Source(&source);
        if(source&&!wcscmp(source,uiOrigin)&&SUCCEEDED(args->TryGetWebMessageAsString(&text)))
        {
            // Native dialogs must run after the COM callback returns; nested
            // modal loops inside WebView2 event callbacks cause reentrancy bugs.
            if(wcslen(text)<=150000 && pendingMessages.size()<32)
            {pendingMessages.emplace_back(text);PostMessageW(window,WM_APP+1,0,0);}
        }
        CoTaskMemFree(source);CoTaskMemFree(text);return S_OK;
    }).Get(),&token);
    // Reserved .invalid origin is fulfilled above from EXE bytes, never DNS/HTTP.
    return web->Navigate(uiOrigin);
}
static LRESULT CALLBACK WindowProc(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam)
{
    switch(message)
    {
    case WM_APP+1:
        if(!processingMessage&&!pendingMessages.empty())
        {
            processingMessage=true;
            std::wstring text=std::move(pendingMessages.front());pendingMessages.pop_front();
            Message(text.c_str());processingMessage=false;
            if(!pendingMessages.empty())PostMessageW(hwnd,WM_APP+1,0,0);
        }
        return 0;
    case WM_SIZE:if(controller){RECT rect;GetClientRect(hwnd,&rect);controller->put_Bounds(rect);}return 0;
    case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(lparam);info->ptMinTrackSize.x=850;info->ptMinTrackSize.y=650;return 0;}
    case WM_TIMER:
        if(directoryThread)
        {
            if(WaitForSingleObject(directoryThread,0)==WAIT_OBJECT_0)
            {
                CloseHandle(directoryThread);directoryThread=nullptr;
                auto job=directoryJob;directoryJob=nullptr;
                NetworkLog(job->diagnostic+(directoryTimedOut?"; discarded-after-UI-deadline":""));
                bool launch=launchAfterResolve;launchAfterResolve=false;
                if(!directoryTimedOut&&job->ok)
                {
                    resolvedMaster=job->ip;
                    resolvedEndpoint=masterEndpoint;
                    if(job->list){serverRows=std::move(job->rows);rowsReceived=GetTickCount64();DirectoryState();Status(L"Список получен: "+std::to_wstring(serverRows.size()/DM_ROW)+L". Все записи unverified; ресурсы проверяет игра.");}
                    delete job;
                    if(launch)
                    {
                        if(memcmp(&pendingLaunchProfile,&profile,sizeof(profile))||pendingLaunchEndpoint!=masterEndpoint
                            ||pendingLaunchName!=sessionName||!publishHost)
                            Status(L"Настройки изменились во время DNS-запроса. Нажми запуск заново.");
                        else Launch();
                    }
                }
                else{std::wstring detail(job->diagnostic.begin(),job->diagnostic.end());delete job;Status(L"Master недоступен. "+detail+L"\nДиагностика: "+networkLogPath,true);}
            }
            else if(!directoryTimedOut&&GetTickCount64()>=directoryDeadline)
            {directoryTimedOut=true;launchAfterResolve=false;NetworkLog("UI deadline 5000ms: DNS/query worker still running; late result will be discarded");Status(L"Master: истёк срок запроса; DNS ещё завершается. Лог: "+networkLogPath,true);}
        }
        if(child&&WaitForSingleObject(child,0)==WAIT_OBJECT_0){DWORD code=0;GetExitCodeProcess(child,&code);CloseHandle(child);child=nullptr;CloseHandle(gameLock);gameLock=nullptr;Status(L"Игра завершена · код "+std::to_wstring(code)+(code?L". Проверь лог сессии.":L". Настройки сохранены."),code!=0);}return 0;
    case WM_CLOSE:if(child&&MessageBoxW(hwnd,L"Закрыть лаунчер? Игра продолжит работать.",L"DPT.DOOM",MB_YESNO|MB_DEFBUTTON2)!=IDYES)return 0;DestroyWindow(hwnd);return 0;
    case WM_DESTROY:if(controller)controller->Close();if(child)CloseHandle(child);if(gameLock)CloseHandle(gameLock);if(directoryThread)CloseHandle(directoryThread);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,message,wparam,lparam);
}
int WINAPI wWinMain(HINSTANCE app,HINSTANCE,PWSTR,int show)
{
    wchar_t path[LP_PATH], directory[LP_PATH];
    DWORD n=GetModuleFileNameW(nullptr,path,LP_PATH);
    if(!n||n>=LP_PATH)return 1;
    base=path;base=base.substr(0,base.find_last_of(L'\\'));
    if(base.size()>=LP_PATH-32)return 1;
    if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,0,path)))appdata=path;
    std::wstring overrideDir;
    int argc=0;LPWSTR *argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!argv)return 1;
    for(int i=1;i<argc;++i)
    {
        if(wcscmp(argv[i],L"--data-dir")||i+1>=argc){LocalFree(argv);Error(L"Usage: dptdoom-launcher.exe [--data-dir directory]");return 1;}
        overrideDir=argv[++i];
    }
    LocalFree(argv);
    if(!LS_Select(base.c_str(),appdata.c_str(),overrideDir.c_str(),directory,&portable)||!LS_Writable(directory))
    {Error(L"Каталог данных недоступен для записи. Portable требует user рядом с EXE. Переместите лаунчер в доступный каталог; скрытого перехода в AppData нет.");return 1;}
    if(!overrideDir.empty())portable=FALSE;
    data=directory;ini=data+L"\\launcher.ini";
    networkLogPath=data+L"\\launcher-network-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".log";
    NetworkLog("DPT.DOOM launcher diagnostic v2; explicit master actions only; no tokens/names logged");
    {wchar_t endpoint[261],name[64];GetPrivateProfileStringW(L"Master",L"Endpoint",LP_MASTER_ENDPOINT,endpoint,261,ini.c_str());masterEndpoint=endpoint;
     GetPrivateProfileStringW(L"Master",L"SessionName",L"DPT.DOOM COOP",name,64,ini.c_str());sessionName=name;
     publishHost=GetPrivateProfileIntW(L"Master",L"Publish",0,ini.c_str())!=0;}
    SetCurrentDirectoryW(base.c_str());LoadProfile();
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 1;
    HMODULE loader=LoadLibraryExW((base+L"\\WebView2Loader.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!loader){Error(L"Нет WebView2Loader.dll рядом с лаунчером. Распакуйте весь пакет.");CoUninitialize();return 1;}
    using CreateEnvironment=HRESULT(STDAPICALLTYPE*)(PCWSTR,PCWSTR,ICoreWebView2EnvironmentOptions*,ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create=reinterpret_cast<CreateEnvironment>(GetProcAddress(loader,"CreateCoreWebView2EnvironmentWithOptions"));
    if(!create){Error(L"Неверный WebView2Loader.dll.");FreeLibrary(loader);CoUninitialize();return 1;}
    WNDCLASSEXW wc={};wc.cbSize=sizeof(wc);wc.lpfnWndProc=WindowProc;wc.hInstance=app;
    wc.hIcon=LoadIconW(app,MAKEINTRESOURCEW(1));wc.hIconSm=wc.hIcon;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));wc.lpszClassName=L"DPTDOOMLauncher";
    if(!RegisterClassExW(&wc)){FreeLibrary(loader);CoUninitialize();return 1;}
    window=CreateWindowW(wc.lpszClassName,L"DPT.DOOM Launcher",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1180,940,nullptr,nullptr,app,nullptr);
    if(!window){FreeLibrary(loader);CoUninitialize();return 1;}
    HRESULT hr=create(nullptr,(data+L"\\webview").c_str(),nullptr,Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([](HRESULT result,ICoreWebView2Environment *environment)->HRESULT{
        if(FAILED(result)||!environment){Error(L"Не удалось запустить WebView2. Установите Microsoft Edge WebView2 Evergreen Runtime с сайта Microsoft. Лаунчер ничего не устанавливает автоматически.");PostMessageW(window,WM_CLOSE,0,0);return S_OK;}
        ComPtr<ICoreWebView2Environment> retainedEnvironment=environment;
        environment->CreateCoreWebView2Controller(window,Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([retainedEnvironment](HRESULT result,ICoreWebView2Controller *value)->HRESULT{
            if(FAILED(result)||!value||FAILED(InitWeb(value,retainedEnvironment.Get()))){Error(L"Ошибка инициализации HTML-интерфейса.");PostMessageW(window,WM_CLOSE,0,0);}return S_OK;
        }).Get());return S_OK;
    }).Get());
    if(FAILED(hr)){Error(L"WebView2 Runtime не найден. Нужен Microsoft Edge WebView2 Evergreen Runtime; автоматической установки нет.");DestroyWindow(window);web.Reset();controller.Reset();FreeLibrary(loader);CoUninitialize();return 1;}
    SetTimer(window,1,250,nullptr);ShowWindow(window,show);
    MSG message={};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    web.Reset();controller.Reset();FreeLibrary(loader);CoUninitialize();return static_cast<int>(message.wParam);
}
