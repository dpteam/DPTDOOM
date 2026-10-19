// DPT.DOOM native experimental directory. GPL-2.0-or-later; see LICENSE.
// One bounded request/response per TCP connection. No blocking peer I/O,
// outgoing port probes, UDP reflection, engine linkage or resource downloads.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#define DM_Close closesocket
#else
#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define DM_Close close
#endif
#include "directory.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <signal.h>

#define DM_CLIENTS 32
#define DM_IP_CLIENTS 8
#define DM_RATE_IPS 256
#define DM_DEADLINE 3000
typedef struct
{
    SOCKET socket;
    unsigned char host[4], request[DM_HEADER+DM_INFO], response[DM_HEADER+DM_MAX_BODY];
    unsigned int received, wanted, response_size, sent;
    uint64_t deadline;
} dm_client_t;
typedef struct { unsigned char host[4]; uint64_t time; unsigned int credit; int used; } dm_bucket_t;
static dm_directory_t directory;
static dm_client_t clients[DM_CLIENTS];
static dm_bucket_t rate_ips[DM_RATE_IPS], global_rate;
static volatile sig_atomic_t stopping;

static void Prune(uint64_t now)
{
    unsigned int i;
    for(i=0;i<DM_MAX_SERVERS;++i)
        if(directory.entries[i].active && directory.entries[i].expires<=now)
            ML_Event(ML_EXPIRE,directory.entries[i].host,DM_Get16(directory.entries[i].info+124),now);
    DM_Prune(&directory,now);
}

static uint64_t Now(void)
{
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC,&ts)) { perror("clock_gettime"); exit(1); }
    return (uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
#endif
}
static int Nonblocking(SOCKET socket)
{
#ifdef _WIN32
    u_long enabled=1;
    return ioctlsocket(socket,FIONBIO,&enabled)==0;
#else
    int flags=fcntl(socket,F_GETFL,0);
    return flags>=0 && !fcntl(socket,F_SETFL,flags|O_NONBLOCK);
#endif
}
static int WouldBlock(void)
{
#ifdef _WIN32
    return WSAGetLastError()==WSAEWOULDBLOCK || WSAGetLastError()==WSAEINTR;
#else
    return errno==EWOULDBLOCK || errno==EAGAIN || errno==EINTR;
#endif
}
static int Entropy(unsigned char *buffer, unsigned int size)
{
#ifdef _WIN32
    return BCryptGenRandom(NULL,buffer,size,BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0;
#else
    FILE *file=fopen("/dev/urandom","rb");
    int ok;
    if(!file)return 0;
    ok=fread(buffer,1,size,file)==size;
    if(fclose(file))ok=0;
    return ok;
#endif
}
static int Credit(dm_bucket_t *bucket, uint64_t now, unsigned int burst, unsigned int rate)
{
    uint64_t refill;
    if(!bucket->used) { bucket->used=1; bucket->time=now; bucket->credit=burst*1000; }
    refill=(now-bucket->time)*rate;
    if(refill>burst*1000)refill=burst*1000;
    bucket->credit=(unsigned int)(bucket->credit+refill>burst*1000?burst*1000:bucket->credit+refill);
    bucket->time=now;
    if(bucket->credit<1000)return 0;
    bucket->credit-=1000;
    return 1;
}
static int Rate(const unsigned char host[4], uint64_t now)
{
    unsigned int i;
    dm_bucket_t *bucket=NULL,*oldest=&rate_ips[0];
    if(!Credit(&global_rate,now,128,64))return 0;
    for(i=0;i<DM_RATE_IPS;++i)
    {
        dm_bucket_t *entry=&rate_ips[i];
        if(entry->used && !memcmp(entry->host,host,4)) { bucket=entry; break; }
        if(!entry->used)oldest=entry;
        else if(oldest->used && entry->time<oldest->time)oldest=entry;
    }
    if(!bucket) { bucket=oldest; memset(bucket,0,sizeof(*bucket)); memcpy(bucket->host,host,4); }
    return Credit(bucket,now,24,4);
}
static void Drop(dm_client_t *client)
{
    if(client->socket!=INVALID_SOCKET)DM_Close(client->socket);
    // Includes tokens: do not retain a completed request in the client pool.
    memset(client,0,sizeof(*client)); client->socket=INVALID_SOCKET;
}
static int Reply(dm_client_t *client, uint64_t now)
{
    unsigned int op=DM_Get16(client->request+6),status=DM_BAD_REQUEST,length=0;
    unsigned char *body=client->response+DM_HEADER;
    Prune(now);
    if(DM_RequestLength(client->request)>=0)
    {
        if(op==DM_LIST) { length=DM_List(&directory,now,body); status=DM_OK; ML_Event(ML_LIST,client->host,0,now); }
        else if(op==DM_REGISTER)
        {
            unsigned char entropy[DM_AUTH];
            if(!Entropy(entropy,sizeof(entropy))) { ML_Event(ML_IO,client->host,0,now); return 0; }
            status=DM_Register(&directory,client->host,client->request+DM_HEADER,entropy,now,body);
            if(status==DM_OK) { length=DM_LEASE; ML_Event(ML_REGISTER,client->host,DM_Get16(client->request+DM_HEADER+124),now); }
            memset(entropy,0,sizeof(entropy));
        }
        else
        {
            status=DM_Renew(&directory,client->host,client->request+DM_HEADER,now,op==DM_REMOVE,body);
            if(status==DM_OK) { length=4; ML_Event(op==DM_REMOVE?ML_REMOVE:ML_HEARTBEAT,client->host,0,now); }
        }
    }
    if(status!=DM_OK)ML_Event(status==DM_UNAUTHORIZED?ML_AUTH:ML_INVALID,client->host,0,now);
    DM_Header(client->response,op,length,status);
    client->response_size=DM_HEADER+length;
    return 1;
}
static void ReadClient(dm_client_t *client, uint64_t now)
{
    int got=(int)recv(client->socket,(char*)client->request+client->received,(int)(client->wanted-client->received),0);
    if(got<=0) { if(!got || !WouldBlock()) { ML_Event(ML_IO,client->host,0,now); Drop(client); } return; }
    client->received+=(unsigned int)got;
    if(client->received!=client->wanted)return;
    if(client->wanted==DM_HEADER)
    {
        int length=DM_RequestLength(client->request);
        if(length>0) { client->wanted=DM_HEADER+(unsigned int)length; return; }
        if(length<0)
        {
            ML_Event(ML_INVALID,client->host,0,now);
            DM_Header(client->response,DM_Get16(client->request+6),0,DM_BAD_REQUEST);
            client->response_size=DM_HEADER; return;
        }
    }
    if(!Reply(client,now))Drop(client);
}
static void WriteClient(dm_client_t *client)
{
    int sent;
#ifdef MSG_NOSIGNAL
    sent=(int)send(client->socket,(const char*)client->response+client->sent,(int)(client->response_size-client->sent),MSG_NOSIGNAL);
#else
    sent=(int)send(client->socket,(const char*)client->response+client->sent,(int)(client->response_size-client->sent),0);
#endif
    if(sent<=0) { if(!sent || !WouldBlock()) { ML_Event(ML_IO,client->host,0,Now()); Drop(client); } return; }
    client->sent+=(unsigned int)sent;
    if(client->sent==client->response_size)Drop(client);
}
static void Accept(SOCKET listener, uint64_t now)
{
    // Bound accept work per loop so floods cannot starve existing connections.
    unsigned int accepted;
    for(accepted=0;accepted<8;++accepted)
    {
        struct sockaddr_in peer;
#ifdef _WIN32
        int length=sizeof(peer);
#else
        socklen_t length=sizeof(peer);
#endif
        unsigned char host[4];
        SOCKET socket=accept(listener,(struct sockaddr*)&peer,&length);
        unsigned int i,ip_clients=0;
        if(socket==INVALID_SOCKET)return;
        memcpy(host,&peer.sin_addr,4);
#ifndef _WIN32
        if(socket>=FD_SETSIZE) { DM_Close(socket); continue; }
#endif
        if(!Rate(host,now)) { ML_Event(ML_LIMIT,host,0,now); DM_Close(socket); continue; }
        if(!Nonblocking(socket)) { ML_Event(ML_IO,host,0,now); DM_Close(socket); continue; }
        for(i=0;i<DM_CLIENTS;++i)
            if(clients[i].socket!=INVALID_SOCKET && !memcmp(clients[i].host,host,4))++ip_clients;
        if(ip_clients>=DM_IP_CLIENTS) { ML_Event(ML_LIMIT,host,0,now); DM_Close(socket); continue; }
        for(i=0;i<DM_CLIENTS;++i)if(clients[i].socket==INVALID_SOCKET)break;
        if(i==DM_CLIENTS) { ML_Event(ML_LIMIT,host,0,now); DM_Close(socket); continue; }
        clients[i].socket=socket; clients[i].wanted=DM_HEADER; clients[i].deadline=now+DM_DEADLINE;
        memcpy(clients[i].host,host,4);
        ML_Event(ML_CONNECT,host,0,now);
    }
}
static int Run(SOCKET listener)
{
    while(!stopping)
    {
        fd_set reads,writes;
        struct timeval timeout;
        SOCKET maximum=listener;
        unsigned int i;
        uint64_t now=Now();
        Prune(now); ML_Flush(now,0);
        FD_ZERO(&reads); FD_ZERO(&writes); FD_SET(listener,&reads);
        for(i=0;i<DM_CLIENTS;++i)
        {
            dm_client_t *client=&clients[i];
            if(client->socket==INVALID_SOCKET)continue;
            if(now>=client->deadline) { ML_Event(ML_TIMEOUT,client->host,0,now); Drop(client); continue; }
            if(client->response_size)FD_SET(client->socket,&writes);
            else FD_SET(client->socket,&reads);
            if(client->socket>maximum)maximum=client->socket;
        }
        timeout.tv_sec=0; timeout.tv_usec=100000;
        if(select((int)(maximum+1),&reads,&writes,NULL,&timeout)==SOCKET_ERROR)
        { if(WouldBlock() || stopping)continue; ML_Message("Master: select failed"); return 1; }
        now=Now();
        // Process old clients before accepting new ones: newly accepted sockets
        // are not in the ready fd_sets and must not be read speculatively.
        for(i=0;i<DM_CLIENTS;++i)
        {
            dm_client_t *client=&clients[i];
            if(client->socket==INVALID_SOCKET)continue;
            if(FD_ISSET(client->socket,&writes))WriteClient(client);
            else if(FD_ISSET(client->socket,&reads))ReadClient(client,now);
        }
        if(FD_ISSET(listener,&reads))Accept(listener,now);
    }
    return 0;
}
static int Number(const char *text, unsigned int low, unsigned int high, unsigned int *out)
{
    unsigned int value=0;
    if(!*text)return 0;
    while(*text)
    {
        if(*text<'0' || *text>'9' || value>high/10)return 0;
        value=value*10+(unsigned int)(*text++-'0'); if(value>high)return 0;
    }
    if(value<low)return 0;
    *out=value; return 1;
}
#ifdef _WIN32
static BOOL WINAPI Stop(DWORD event)
{
    if(event==CTRL_C_EVENT || event==CTRL_BREAK_EVENT) { stopping=1; return TRUE; }
    return FALSE;
}
#else
static void Stop(int event) { (void)event; stopping=1; }
#endif
int main(int argc, char **argv)
{
    const char *host="127.0.0.1";
    const char *log_path="dptdoom-master.log";
    unsigned int port=25135,i;
    unsigned int log_size=1048576;
    struct sockaddr_in address;
    SOCKET listener;
    int result;
    directory.lease_seconds=60;
    for(i=1;i<(unsigned int)argc;++i)
    {
        if(!strcmp(argv[i],"--version")) { puts("DPT.DOOM master 0.1.0-dev"); return 0; }
        if(!strcmp(argv[i],"--help"))
        { puts("dptdoom-master [--bind numeric-IPv4] [--port 1..65535] [--lease-seconds 3..300]\n  [--log path] [--log-max-bytes 4096..16777216]\nDefault loopback:25135, TTL 60s, log dptdoom-master.log (1 MiB + one backup).\nExperimental directory v1; no TLS, game relay or port probing."); return 0; }
        if(i+1>=(unsigned int)argc)goto usage;
        if(!strcmp(argv[i],"--bind"))host=argv[++i];
        else if(!strcmp(argv[i],"--log"))log_path=argv[++i];
        else if(!strcmp(argv[i],"--log-max-bytes")) { if(!Number(argv[++i],4096,16777216,&log_size))goto usage; }
        else if(!strcmp(argv[i],"--port")) { if(!Number(argv[++i],1,65535,&port))goto usage; }
        else if(!strcmp(argv[i],"--lease-seconds")) { if(!Number(argv[++i],3,300,&directory.lease_seconds))goto usage; }
        else goto usage;
    }
    if(!ML_Open(log_path,log_size)) { fputs("Master: cannot open event log (check --log path/permissions)\n",stderr); return 1; }
#ifdef _WIN32
    { WSADATA data; if(WSAStartup(MAKEWORD(2,2),&data)) { ML_Message("Master: Winsock startup failed"); ML_Close(); return 1; } }
    SetConsoleCtrlHandler(Stop,TRUE);
#else
    signal(SIGINT,Stop); signal(SIGTERM,Stop); signal(SIGPIPE,SIG_IGN);
#endif
    memset(&address,0,sizeof(address)); address.sin_family=AF_INET; address.sin_port=htons((uint16_t)port);
    if(inet_pton(AF_INET,host,&address.sin_addr)!=1) { ML_Message("Master: --bind must be numeric IPv4"); result=2; goto cleanup; }
    for(i=0;i<DM_CLIENTS;++i)clients[i].socket=INVALID_SOCKET;
    listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(listener==INVALID_SOCKET) { ML_Message("Master: socket failed"); result=1; goto cleanup; }
#ifdef _WIN32
    // No SO_REUSEADDR on Windows: do not share/hijack an existing bind.
    { BOOL exclusive=TRUE; if(setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(const char*)&exclusive,sizeof(exclusive)))
      { ML_Message("Master: exclusive bind option failed"); DM_Close(listener); result=1; goto cleanup; } }
#else
    if(listener>=FD_SETSIZE) { DM_Close(listener); result=1; goto cleanup; }
#endif
    if(!Nonblocking(listener) || bind(listener,(struct sockaddr*)&address,sizeof(address))==SOCKET_ERROR || listen(listener,DM_CLIENTS)==SOCKET_ERROR)
    { ML_Message("Master: nonblocking/bind/listen failed; check address and port"); DM_Close(listener); result=1; goto cleanup; }
    { char message[256]; snprintf(message,sizeof(message),"DPT.DOOM master: listening on %s:%u (directory v1, TTL %us, experimental LAN; no TLS)",host,port,directory.lease_seconds); ML_Message(message); }
    result=Run(listener);
    for(i=0;i<DM_CLIENTS;++i)Drop(&clients[i]);
    DM_Close(listener);
cleanup:
#ifdef _WIN32
    WSACleanup();
#endif
    ML_Message("Master: stopped"); ML_Close();
    return result;
usage:
    fputs("Invalid/missing option. Use dptdoom-master --help\n",stderr); return 2;
}
