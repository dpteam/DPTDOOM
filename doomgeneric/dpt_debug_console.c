// Tee CRT stdout/stderr to a Debug console, unique file and inherited launcher
// log. Do not replace/discard redirected stdout by freopen("CONOUT$").
// GPL-2.0-or-later; see LICENSE.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <io.h>
#include <fcntl.h>
#include "dpt_debug_console.h"

static HANDLE readers[2], workers[2], console, log_file, inherited[2];
static CRITICAL_SECTION output_lock;
static int own_console;

static void WriteAll(HANDLE sink, const char *data, DWORD count)
{
    while(sink && sink!=INVALID_HANDLE_VALUE && count)
    {
        DWORD done=0;
        if(!WriteFile(sink,data,count,&done,NULL) || !done)return;
        data+=done; count-=done;
    }
}
static DWORD WINAPI Relay(void *argument)
{
    char data[4096], line[4096];
    DWORD got, used=0,i;
    unsigned int index=(unsigned int)(uintptr_t)argument;
    while(ReadFile(readers[index],data,sizeof(data),&got,NULL) && got)
    {
        for(i=0;i<got;++i)
        {
            line[used++]=data[i];
            if(data[i]=='\n' || used==sizeof(line))
            {
                EnterCriticalSection(&output_lock);
                WriteAll(log_file,line,used); WriteAll(console,line,used); WriteAll(inherited[index],line,used);
                LeaveCriticalSection(&output_lock); used=0;
            }
        }
    }
    if(used)
    {
        EnterCriticalSection(&output_lock);
        WriteAll(log_file,line,used); WriteAll(console,line,used); WriteAll(inherited[index],line,used);
        LeaveCriticalSection(&output_lock);
    }
    return 0;
}
static void Shutdown(void)
{
    unsigned int i;
    fflush(NULL);
    _close(1); _close(2); // close both writers so the reader observes EOF
    WaitForMultipleObjects(2,workers,TRUE,INFINITE);
    for(i=0;i<2;++i) { CloseHandle(workers[i]); CloseHandle(readers[i]); }
    DeleteCriticalSection(&output_lock);
    if(log_file!=INVALID_HANDLE_VALUE) { FlushFileBuffers(log_file); CloseHandle(log_file); }
    if(console!=INVALID_HANDLE_VALUE)CloseHandle(console);
    for(i=0;i<2;++i)if(inherited[i])CloseHandle(inherited[i]);
    if(own_console)FreeConsole();
}
void DPT_DebugConsoleInit(void)
{
    HANDLE writers[2]={NULL,NULL};
    int fd;
    unsigned int i;
    wchar_t path[96];
    FILE *stream;
    static int initialized;
    if(initialized)return;
    initialized=1;
    // Preserve launcher file/pipe handles BEFORE AllocConsole changes std handles.
    for(i=0;i<2;++i)
    {
        HANDLE handle=GetStdHandle(i?STD_ERROR_HANDLE:STD_OUTPUT_HANDLE);
        DWORD type=GetFileType(handle);
        if(type==FILE_TYPE_DISK || type==FILE_TYPE_PIPE)
            DuplicateHandle(GetCurrentProcess(),handle,GetCurrentProcess(),&inherited[i],0,FALSE,DUPLICATE_SAME_ACCESS);
    }
    if(!GetConsoleWindow())
    {
        // CREATE_NO_WINDOW gives console-subsystem programs an invisible console;
        // AllocConsole otherwise fails with ERROR_ACCESS_DENIED. Detach only this
        // windowless console, keeping saved launcher output handles intact.
        if(GetConsoleCP())FreeConsole();
        own_console=AllocConsole()!=FALSE;
    }
    console=CreateFileW(L"CONOUT$",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(own_console)SetConsoleTitleW(L"DPT.DOOM Debug diagnostics");
    swprintf(path,96,L"dptdoom-debug-%lu-%llu.log",GetCurrentProcessId(),GetTickCount64());
    log_file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(!CreatePipe(&readers[0],&writers[0],NULL,0) || !CreatePipe(&readers[1],&writers[1],NULL,0))goto failure;
    // CREATE_NO_WINDOW may give the CRT invalid (-2) standard descriptors.
    // Reinitialize them without touching the duplicates saved above.
    stream=freopen("NUL","w",stdout);
    if(!stream)goto failure;
    stream=freopen("NUL","w",stderr);
    if(!stream)goto failure;
    for(i=0;i<2;++i)
    {
        fd=_open_osfhandle((intptr_t)writers[i],_O_WRONLY|_O_BINARY);
        if(fd<0)goto failure;
        writers[i]=NULL;
        if(_dup2(fd,(int)i+1)) { _close(fd); goto failure; }
        _close(fd);
    }
    setvbuf(stdout,NULL,_IONBF,0); setvbuf(stderr,NULL,_IONBF,0);
    InitializeCriticalSection(&output_lock);
    for(i=0;i<2;++i)workers[i]=CreateThread(NULL,0,Relay,(void*)(uintptr_t)i,0,NULL);
    if(!workers[0] || !workers[1])
    {
        _close(1); _close(2);
        for(i=0;i<2;++i)if(workers[i]) { WaitForSingleObject(workers[i],INFINITE); CloseHandle(workers[i]); }
        DeleteCriticalSection(&output_lock); goto failure;
    }
    atexit(Shutdown);
    printf("DPT.DOOM Debug: console=%s, file=%s, inherited-log=%s\n",
        console!=INVALID_HANDLE_VALUE?"ready":"unavailable",log_file!=INVALID_HANDLE_VALUE?"ready":"unavailable",
        inherited[0]||inherited[1]?"preserved":"none");
    if(log_file==INVALID_HANDLE_VALUE)fprintf(stderr,"Debug log file unavailable; console/inherited output remain enabled\n");
    return;
failure:
    OutputDebugStringA("DPT.DOOM: Debug console tee initialization failed\n");
    for(i=0;i<2;++i) { if(writers[i])CloseHandle(writers[i]); if(readers[i])CloseHandle(readers[i]); }
    // Partial pipe/thread initialization must not leave stdout/stderr pointing
    // at a pipe whose reader was closed. Fall back to the inherited output.
    for(i=0;i<2;++i)
    {
        HANDLE fallback=NULL;
        HANDLE source=inherited[i]?inherited[i]:console;
        if(source && source!=INVALID_HANDLE_VALUE
            && DuplicateHandle(GetCurrentProcess(),source,GetCurrentProcess(),&fallback,0,FALSE,DUPLICATE_SAME_ACCESS))
        {
            fd=_open_osfhandle((intptr_t)fallback,_O_WRONLY|_O_BINARY);
            if(fd>=0) { _dup2(fd,(int)i+1); _close(fd); }
            else CloseHandle(fallback);
        }
    }
    fputs("DPT.DOOM: Debug tee unavailable; using fallback output\n",stderr);
    if(log_file!=INVALID_HANDLE_VALUE)CloseHandle(log_file);
    if(console!=INVALID_HANDLE_VALUE)CloseHandle(console);
    for(i=0;i<2;++i)if(inherited[i])CloseHandle(inherited[i]);
    if(own_console)FreeConsole();
}
