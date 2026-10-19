// No request strings, names, packet dumps, session IDs or lease secrets.
// GPL-2.0-or-later; see LICENSE.
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <errno.h>

static FILE *file;
static char log_path[2048], backup[2052];
static unsigned int max_size, bytes;
static int failed;
static struct { uint64_t start; unsigned int shown, suppressed; } limits[ML_EVENTS];
static const char *names[ML_EVENTS]={"connect", "list", "register", "heartbeat", "remove", "expire",
    "invalid-request", "unauthorized", "limit-rejected", "deadline", "transport-error", "response"};

static void FileFailure(void)
{
    if(file)fclose(file);
    file=NULL;
    if(!failed) { fputs("Master log: file write/rotation failed; console logging continues\n",stderr); fflush(stderr); }
    failed=1;
}
static void Line(const char *message)
{
    char line[512], stamp[32];
    time_t wall=time(NULL);
    struct tm *utc=gmtime(&wall);
    size_t size;
    if(!utc || !strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%SZ",utc))strcpy(stamp,"time-unavailable");
    snprintf(line,sizeof(line),"[%s] %s\n",stamp,message);
    size=strlen(line);
    fputs(line,stdout); fflush(stdout);
    if(!file)return;
    if(bytes+size>max_size)
    {
        if(fclose(file)) { file=NULL; FileFailure(); return; }
        file=NULL;
        // Exactly one backup, never a directory sweep or unbounded log series.
        if(remove(backup) && errno!=ENOENT) { FileFailure(); return; }
        if(rename(log_path,backup)) { FileFailure(); return; }
        file=fopen(log_path,"wb"); bytes=0;
        if(!file) { FileFailure(); return; }
    }
    if(fwrite(line,1,size,file)!=size || fflush(file)) { FileFailure(); return; }
    bytes+=(unsigned int)size;
}
int ML_Open(const char *path, unsigned int max_bytes)
{
    long size;
    if(!*path || strlen(path)>=sizeof(log_path) || max_bytes<4096)return 0;
    strcpy(log_path,path); snprintf(backup,sizeof(backup),"%s.1",path);
    max_size=max_bytes; failed=0; memset(limits,0,sizeof(limits));
    file=fopen(path,"ab");
    if(!file)return 0;
    if(fseek(file,0,SEEK_END) || (size=ftell(file))<0) { fclose(file); file=NULL; return 0; }
    bytes=(unsigned int)((unsigned long)size>max_size?max_size:(unsigned long)size);
    return 1;
}
void ML_Message(const char *message) { Line(message); }
void ML_Flush(uint64_t now, int force)
{
    unsigned int i;
    for(i=0;i<ML_EVENTS;++i)
    {
        if(force || now-limits[i].start>=5000)
        {
            if(limits[i].suppressed)
            {
                char message[128];
                snprintf(message,sizeof(message),"%s: suppressed=%u in event window",names[i],limits[i].suppressed);
                Line(message);
            }
            limits[i].start=now; limits[i].shown=0; limits[i].suppressed=0;
        }
    }
}
void ML_Detail(unsigned int event, const unsigned char *host, unsigned int port, uint64_t now, const char *detail)
{
    char message[448];
    if(event>=ML_EVENTS)return;
    ML_Flush(now,0);
    if(limits[event].shown>=4)
    { if(limits[event].suppressed<UINT_MAX)++limits[event].suppressed; return; }
    ++limits[event].shown;
    if(host)snprintf(message,sizeof(message),"%s: peer=%u.%u.%u.%u port=%u %s",names[event],host[0],host[1],host[2],host[3],port,detail?detail:"");
    else snprintf(message,sizeof(message),"%s %s",names[event],detail?detail:"");
    Line(message);
}
void ML_Event(unsigned int event, const unsigned char *host, unsigned int port, uint64_t now)
{ ML_Detail(event,host,port,now,NULL); }
void ML_Close(void)
{
    ML_Flush(0,1);
    if(file && fclose(file)) { file=NULL; FileFailure(); }
    file=NULL;
}
