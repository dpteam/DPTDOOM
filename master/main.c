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
#include <ctype.h>
#include <time.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define DM_Close close
#endif
#include "directory.h"
#include "log.h"
#include "public_address.h"
#include "relay.h"
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
    uint64_t began, id;
    unsigned int peer_port;
} dm_client_t;
typedef struct { unsigned char host[4]; uint64_t time; unsigned int credit; int used; } dm_bucket_t;
static dm_directory_t directory;
static dm_client_t clients[DM_CLIENTS];
static dm_bucket_t rate_ips[DM_RATE_IPS], global_rate;
static volatile sig_atomic_t stopping;
static uint64_t next_client_id;
static pa_state_t public_address;
static int relay_enabled;

static int LastSocketError(void)
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}
static void ClientEvent(dm_client_t *c,unsigned int event,const char *reason,unsigned int status,int error,uint64_t now)
{
    char detail[352];
    unsigned int op=c->received>=DM_HEADER?DM_Get16(c->request+6):0;
    snprintf(detail,sizeof(detail),"cid=%llu op=%u status=%u reason=%s os_error=%d elapsed_ms=%llu received=%u/%u sent=%u/%u",
        (unsigned long long)c->id,op,status,reason,error,(unsigned long long)(now-c->began),
        c->received,c->wanted,c->sent,c->response_size);
    ML_Detail(event,c->host,c->peer_port,now,detail);
}

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
        if(op==DM_RELAY_HOST||op==DM_RELAY_JOIN)
        {
            status=relay_enabled?MR_Attach(&directory,(uintptr_t)client->socket,op,client->host,client->request+DM_HEADER,now):DM_CAPACITY;
            if(status==DM_OK){client->socket=INVALID_SOCKET;Drop(client);return 1;}
        }
        else if(op==DM_LIST) { length=DM_List(&directory,now,body); status=DM_OK; }
        else if(op==DM_REGISTER)
        {
            unsigned char entropy[DM_AUTH];
            if(!Entropy(entropy,sizeof(entropy))) { ClientEvent(client,ML_IO,"OS-random-unavailable",0,0,now); return 0; }
            status=DM_Register(&directory,client->host,client->request+DM_HEADER,entropy,now,body);
            if(status==DM_OK)length=DM_LEASE;
            memset(entropy,0,sizeof(entropy));
        }
        else
        {
            status=DM_Renew(&directory,client->host,client->request+DM_HEADER,now,op==DM_REMOVE,body);
            if(status==DM_OK)length=4;
        }
    }
    DM_Header(client->response,op,length,status);
    client->response_size=DM_HEADER+length;
    {
        const char *reason="accepted";
        unsigned int event=op==DM_LIST?ML_LIST:op==DM_REGISTER?ML_REGISTER:op==DM_REMOVE?ML_REMOVE:ML_HEARTBEAT;
        char metadata[200];
        if(status==DM_BAD_REQUEST)reason=op==DM_REGISTER?DM_InfoError(client->request+DM_HEADER):"bad-request";
        else if(status==DM_CONFLICT)reason=op>=DM_RELAY_HOST?"relay-channel-already-attached/not-waiting":"endpoint-already-registered-or-ID-collision;wait-TTL";
        else if(status==DM_CAPACITY)
        {
            unsigned int j,count=0,ip_count=0;
            for(j=0;j<DM_MAX_SERVERS;++j)if(directory.entries[j].active)
            {++count;if(!memcmp(directory.entries[j].host,client->host,4))++ip_count;}
            reason=op>=DM_RELAY_HOST?(relay_enabled?"relay-session-capacity":"relay-disabled-by-operator"):!DM_MappingReady(&directory,client->host)?"public-address-discovery-not-ready(retry)":ip_count>=DM_MAX_IP_SERVERS?"per-IP-registration-limit(4)":count>=DM_MAX_SERVERS?"directory-capacity(128)":"registration-capacity";
        }
        else if(status==DM_UNAUTHORIZED)
        {
            unsigned int j;reason="unknown-or-expired-lease";
            for(j=0;j<DM_MAX_SERVERS;++j)
            {
                const dm_entry_t *entry=&directory.entries[j];
                if(entry->active&&!memcmp(entry->id,client->request+DM_HEADER,16))
                {reason=memcmp(entry->host,client->host,4)?"lease-source-IP-changed(check-VPN-egress)":"lease-token-mismatch";break;}
            }
        }
        if(status!=DM_OK)event=status==DM_UNAUTHORIZED?ML_AUTH:ML_INVALID;
        if(op==DM_REGISTER&&status==DM_OK)
        {
            const unsigned char *p=client->request+DM_HEADER;
            unsigned char advertised[4];DM_AdvertisedAddress(&directory,client->host,advertised);
            snprintf(metadata,sizeof(metadata),"accepted-game-port=%u-skill=%u-flags=%u-game-protocol=%u-TTL=%u advertised=%u.%u.%u.%u:%u mapped=%s",
                DM_Get16(p+124),p[126],p[127],p[128],directory.lease_seconds,
                advertised[0],advertised[1],advertised[2],advertised[3],DM_Get16(p+124),memcmp(advertised,client->host,4)?"yes":"no");reason=metadata;
        }
        ClientEvent(client,event,reason?reason:"invalid-metadata",status,0,now);
    }
    return 1;
}
static void ReadClient(dm_client_t *client, uint64_t now)
{
    int got=(int)recv(client->socket,(char*)client->request+client->received,(int)(client->wanted-client->received),0);
    if(got<=0) { if(!got || !WouldBlock()) { int error=got<0?LastSocketError():0;ClientEvent(client,ML_IO,got?"receive-failed":client->received?"peer-closed-truncated-request":"peer-closed-no-DPTM-request(TCP-probe)",0,error,now); Drop(client); } return; }
    client->received+=(unsigned int)got;
    if(client->received!=client->wanted)return;
    if(client->wanted==DM_HEADER)
    {
        int length=DM_RequestLength(client->request);
        if(length>0) { client->wanted=DM_HEADER+(unsigned int)length; return; }
        if(length<0)
        {
            ClientEvent(client,ML_INVALID,DM_HeaderError(client->request),DM_BAD_REQUEST,0,now);
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
    if(sent<=0) { if(!sent || !WouldBlock()) { int error=sent<0?LastSocketError():0;ClientEvent(client,ML_IO,"response-send-failed",0,error,Now()); Drop(client); } return; }
    client->sent+=(unsigned int)sent;
    if(client->sent==client->response_size) { ClientEvent(client,ML_RESPONSE,"response-delivered-to-TCP",DM_Get16(client->response+12),0,Now());Drop(client); }
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
        if(!Rate(host,now)) { ML_Detail(ML_LIMIT,host,ntohs(peer.sin_port),now,"reason=connection-rate-limit"); DM_Close(socket); continue; }
        // Checked before anything else is done with the connection, and by
        // source address only: the advertised/listing address is peer-supplied
        // and must never be able to lift a ban.
        if(DM_IsBanned(&directory,host)) { ML_Detail(ML_LIMIT,host,ntohs(peer.sin_port),now,"reason=banned-source"); DM_Close(socket); continue; }
        if(!Nonblocking(socket)) { ML_Event(ML_IO,host,0,now); DM_Close(socket); continue; }
        for(i=0;i<DM_CLIENTS;++i)
            if(clients[i].socket!=INVALID_SOCKET && !memcmp(clients[i].host,host,4))++ip_clients;
        if(ip_clients>=DM_IP_CLIENTS) { ML_Detail(ML_LIMIT,host,ntohs(peer.sin_port),now,"reason=per-IP-active-connection-limit"); DM_Close(socket); continue; }
        for(i=0;i<DM_CLIENTS;++i)if(clients[i].socket==INVALID_SOCKET)break;
        if(i==DM_CLIENTS) { ML_Detail(ML_LIMIT,host,ntohs(peer.sin_port),now,"reason=global-active-connection-limit"); DM_Close(socket); continue; }
        clients[i].socket=socket; clients[i].wanted=DM_HEADER; clients[i].deadline=now+DM_DEADLINE;
        memcpy(clients[i].host,host,4);
        clients[i].began=now;clients[i].id=++next_client_id;clients[i].peer_port=ntohs(peer.sin_port);
        ClientEvent(&clients[i],ML_CONNECT,"TCP-accepted-awaiting-DPTM",0,0,now);
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
        PA_Tick(&public_address,&directory,now);
        Prune(now); ML_Flush(now,0);
        FD_ZERO(&reads); FD_ZERO(&writes); FD_SET(listener,&reads);
        for(i=0;i<DM_CLIENTS;++i)
        {
            dm_client_t *client=&clients[i];
            if(client->socket==INVALID_SOCKET)continue;
            if(now>=client->deadline) { ClientEvent(client,ML_TIMEOUT,client->response_size?"response-send-deadline":client->received<DM_HEADER?"receive-header-deadline":"receive-body-deadline",0,0,now); Drop(client); continue; }
            if(client->response_size)FD_SET(client->socket,&writes);
            else FD_SET(client->socket,&reads);
            if(client->socket>maximum)maximum=client->socket;
        }
        {uintptr_t high=(uintptr_t)maximum;MR_Prepare(&directory,&reads,&writes,&high,now);maximum=(SOCKET)high;}
        timeout.tv_sec=0; timeout.tv_usec=10000;
        if(select((int)(maximum+1),&reads,&writes,NULL,&timeout)==SOCKET_ERROR)
        { if(WouldBlock() || stopping)continue; ML_Message("Master: select failed"); return 1; }
        now=Now();
        MR_Poll(&reads,&writes,now);
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
// Config-supplied bind address. Static because the line buffer is reused for
// every config line, and the address must outlive LoadConfig.
static char config_bind[64];

// Template written when --config points at a file that does not exist yet, so an
// operator gets something to edit instead of an error. Every key is commented
// out, so the file is inert until the operator changes it.
static const char *ConfigTemplate =
"# dptdoom-master configuration.\n"
"# Generated automatically because this file did not exist.\n"
"# Every line is commented out, so this file does nothing until you edit it.\n"
"# Restart the master after editing. Lines starting with # are ignored.\n"
"\n"
"# Address to listen on. Default is loopback, which only this machine can reach.\n"
"# Use 0.0.0.0 for all interfaces, or one specific interface address.\n"
"# A --bind command line argument overrides this value.\n"
"# bind=0.0.0.0\n"
"\n"
"# Public address rewriting for NAT hairpin / SNAT. Off unless set here.\n"
"# Source must be a canonical RFC1918 or loopback CIDR; destination numeric IPv4.\n"
"# Up to 8 non-overlapping ranges. No game-port rewriting, no NAT traversal.\n"
"# address-map=192.168.50.0/24=203.0.113.10\n"
"\n"
"# Ban list file. One rule per line: an IPv4 address, or IPv4/prefix (CIDR).\n"
"# Lines starting with # are comments; blank lines are ignored.\n"
"# Matching is against the TCP source address, never the advertised address,\n"
"# so a peer cannot escape a ban by advertising something else.\n"
"# A malformed rule stops startup instead of loading a partial list.\n"
"# banlist=banlist.txt\n"
"\n"
"# How the master learns its own public address: off | manual | dns | api.\n"
"#   off     - advertise the observed source address (default)\n"
"#   manual  - use public-ip\n"
"#   dns     - resolve public-host\n"
"#   api     - query public-api over HTTPS (explicit opt-in)\n"
"# public-address-mode=off\n"
"# public-host=games.example.org\n"
"# public-ip=203.0.113.10\n"
"# public-api=https://example.org/myip\n"
"# Seconds between public address refreshes (30..86400).\n"
"# public-refresh-seconds=300\n"
"\n"
"# Clients on this subnet are treated as hairpin (see address-map) so they get\n"
"# the public address of peers on the same LAN. Optional.\n"
"# hairpin-subnet=192.168.50.0/24\n";

static int WriteConfigTemplate(const char *path)
{
    FILE *file=fopen(path,"wb");
    if(!file)return 0;
    if(fputs(ConfigTemplate,file)==EOF){fclose(file);return 0;}
    return fclose(file)==0;
}

// Ban list, reloaded from disk. Parsed into a scratch directory first and only
// committed when the whole file is valid, so a typo cannot leave the server
// enforcing half a list (which could ban the wrong people).
static char ban_path[512];

static int LoadBans(const char *path, int announce)
{
    FILE *file=fopen(path,"rb");
    char line[256];unsigned int number=0,accepted=0;
    dm_directory_t scratch;

    if(!file)
    {
        // No ban file is a normal state: nothing is banned.
        if(announce) ML_Message("Master: no ban list found; nothing is banned");
        return 1;
    }

    memcpy(&scratch,&directory,sizeof(scratch));
    DM_ClearBans(&scratch);

    while(fgets(line,sizeof(line),file))
    {
        char *start=line,*end;size_t length=strlen(line);++number;
        if(length==sizeof(line)-1 && line[length-1]!='\n')
        {
            fprintf(stderr,"Master: ban list line %u is too long\n",number);
            fclose(file);return 0;
        }
        while(*start==' '||*start=='\t')++start;
        end=start+strlen(start);while(end>start && isspace((unsigned char)end[-1]))*--end=0;
        if(!*start || *start=='#')continue;
        if(!DM_AddBan(&scratch,start))
        {
            // Fail the whole reload instead of applying the valid prefix: a
            // partially loaded ban list is worse than the previous one.
            fprintf(stderr,"Master: invalid ban rule on line %u: %s\n",number,start);
            fclose(file);return 0;
        }
        ++accepted;
    }
    if(ferror(file)){fclose(file);fprintf(stderr,"Master: ban list read failed\n");return 0;}
    fclose(file);

    memcpy(directory.bans,scratch.bans,sizeof(scratch.bans));
    directory.ban_count=scratch.ban_count;
    if(announce)
    {
        char message[160];
        snprintf(message,sizeof(message),"Master: %u ban rule(s) loaded from %s",accepted,path);
        ML_Message(message);
    }
    return 1;
}

static int LoadConfig(const char *path, const char **host, int host_from_cli)
{
    FILE *file=fopen(path,"rb");
    char line[768];unsigned int number=0;
    if(!file)
    {
        // A missing file is almost always a first run rather than a typo. A
        // typo in the directory still fails here, so a mistyped path cannot
        // silently produce a "configured" master that ignores an existing
        // config elsewhere.
        if(!WriteConfigTemplate(path))
        { fprintf(stderr,"Master: cannot open or create config: %s\n",path); return 0; }
        ML_Message("Master: config did not exist, wrote a commented template (all defaults)");
        { char message[320]; snprintf(message,sizeof(message),
            "Master: edit %s to set bind=, address-map= and public address; every line in it is commented out, so nothing is active yet",path);
          ML_Message(message); }
        return 1;
    }
    while(fgets(line,sizeof(line),file))
    {
        char *start=line,*end;size_t length=strlen(line);++number;
        if(length==sizeof(line)-1 && line[length-1]!='\n')goto invalid;
        while(*start==' '||*start=='\t')++start;
        end=start+strlen(start);while(end>start && isspace((unsigned char)end[-1]))*--end=0;
        if(!*start || *start=='#')continue;
        if(!strncmp(start,"address-map=",12))
        {if(!DM_AddAddressMap(&directory,start+12))goto invalid;}
        else if(!strncmp(start,"banlist=",8))
        {
            // Relative to the config file's directory is friendlier, but the
            // master has no path helper here; keep it literal and document that
            // it is relative to the working directory or absolute.
            if(strlen(start+8)>=sizeof(ban_path))goto invalid;
            strcpy(ban_path,start+8);
        }
        else if(!strncmp(start,"bind=",5))
        {
            // Loopback is the safe default, so making the master reachable has
            // to be an explicit choice. It still is: this only lets an operator
            // persist that choice instead of retyping --bind on every start.
            if(strlen(start+5)>=sizeof(config_bind))goto invalid;
            strcpy(config_bind,start+5);
            if(!host_from_cli)*host=config_bind;
        }
        else
        {
            char *separator=strchr(start,'=');if(!separator)goto invalid;
            *separator=0;if(!PA_Config(&public_address,start,separator+1))goto invalid;
        }
    }
    if(ferror(file)){fclose(file);fputs("Master: config read failed\n",stderr);return 0;}
    fclose(file);return 1;
invalid:
    fprintf(stderr,"Master: invalid config line %u (bind/banlist/address-map/public-address-mode/public-host/public-ip/public-api/hairpin-subnet/public-refresh-seconds; see docs/MASTER.md)\n",number);
    fclose(file);return 0;
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
    // Command line wins over the config file for the bind address, so a shared
    // config cannot silently override an explicit --bind.
    int host_from_cli=0;
    const char *log_path="dptdoom-master.log";
    const char *config_path=NULL;
    unsigned int port=25135,i;
    unsigned int log_size=1048576;
    struct sockaddr_in address;
    SOCKET listener;
    int result;
    directory.lease_seconds=60;
    MR_Init();
    PA_Init(&public_address);
    for(i=1;i<(unsigned int)argc;++i)
    {
        if(!strcmp(argv[i],"--version")) { puts("DPT.DOOM master 0.1.0-dev"); return 0; }
        if(!strcmp(argv[i],"--help"))
        { puts("dptdoom-master [--bind numeric-IPv4] [--port 1..65535] [--lease-seconds 3..300]\n  [--log path] [--log-max-bytes 4096..16777216] [--config path] [--relay]\nDefault loopback:25135, TTL 60s, no address rewriting, relay OFF.\nLoopback means only this machine can connect; for LAN/Internet clients pass\n  --bind 0.0.0.0 (or a specific interface address), or bind=0.0.0.0 in --config.\n--relay: opt-in 8 bounded session pairs over this port, no arbitrary proxy or TLS.\nConfig: see docs/MASTER.md. No game-port probing or router changes."); return 0; }
        if(!strcmp(argv[i],"--relay")){relay_enabled=1;continue;}
        if(i+1>=(unsigned int)argc)goto usage;
        if(!strcmp(argv[i],"--bind")){host=argv[++i];host_from_cli=1;}
        else if(!strcmp(argv[i],"--config")){if(config_path)goto usage;config_path=argv[++i];}
        else if(!strcmp(argv[i],"--log"))log_path=argv[++i];
        else if(!strcmp(argv[i],"--log-max-bytes")) { if(!Number(argv[++i],4096,16777216,&log_size))goto usage; }
        else if(!strcmp(argv[i],"--port")) { if(!Number(argv[++i],1,65535,&port))goto usage; }
        else if(!strcmp(argv[i],"--lease-seconds")) { if(!Number(argv[++i],3,300,&directory.lease_seconds))goto usage; }
        else goto usage;
    }
    if(config_path && !LoadConfig(config_path,&host,host_from_cli))return 2;
    if(ban_path[0] && !LoadBans(ban_path,1))return 2;
    if(!config_path)
        ML_Message("Master: no --config given; pass --config dptdoom-master.conf to get a commented template you can edit (it is created if missing)");
    if(!PA_Prepare(&public_address,&directory)){fputs("Master: invalid public address mode/config combination or subnet overlap\n",stderr);return 2;}
    if(!ML_Open(log_path,log_size)) { fputs("Master: cannot open event log (check --log path/permissions)\n",stderr); return 1; }
#ifdef _WIN32
    { WSADATA data; if(WSAStartup(MAKEWORD(2,2),&data)) { ML_Message("Master: Winsock startup failed"); ML_Close(); return 1; } }
    SetConsoleCtrlHandler(Stop,TRUE);
#else
    signal(SIGINT,Stop); signal(SIGTERM,Stop); signal(SIGPIPE,SIG_IGN);
#endif
    memset(&address,0,sizeof(address)); address.sin_family=AF_INET; address.sin_port=htons((uint16_t)port);
    if(inet_pton(AF_INET,host,&address.sin_addr)!=1) { ML_Message("Master: bind address must be numeric IPv4 (from --bind or bind= in --config)"); result=2; goto cleanup; }
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
    // A loopback bind is the safe default, but it also means remote clients get
    // connection refused with no hint why. Say so explicitly, with the fix.
    if(!strcmp(host,"127.0.0.1") || !strcmp(host,"localhost"))
        ML_Message("Master: bound to loopback, so only this machine can reach it. "
                   "For LAN/Internet clients start with --bind 0.0.0.0 (or a specific "
                   "interface address), or put bind=0.0.0.0 in the --config file. "
                   "Reachability still depends on your firewall/router, which this "
                   "program does not change.");
    for(i=0;i<directory.address_map_count;++i)
    {
        const dm_address_map_t *rule=&directory.address_maps[i];char message[320];
        snprintf(message,sizeof(message),"Operator address-map: source=%u.%u.%u.%u/%u advertised=%u.%u.%u.%u dynamic=%s ready=%s; game port unchanged; lease auth remains actual source; reachability unverified",
            rule->network[0],rule->network[1],rule->network[2],rule->network[3],rule->prefix,
            rule->advertised[0],rule->advertised[1],rule->advertised[2],rule->advertised[3],rule->dynamic?"yes":"no",rule->enabled?"yes":"no");ML_Message(message);
    }
    {char message[360];snprintf(message,sizeof(message),"Public address discovery: mode=%s host=%s refresh_seconds=%u (API URL omitted; HTTPS request only in explicit api mode)",public_address.mode,public_address.host[0]?public_address.host:"none",public_address.refresh_seconds);ML_Message(message);}
    ML_Message("Master diagnostic v2: status 0=OK 1=BAD_REQUEST 2=UNAUTHORIZED 3=CONFLICT 4=CAPACITY; cid is local to this process, peer port is TCP source port, not game port");
    ML_Message(relay_enabled?"Relay ENABLED: 8 session pairs, 4096-byte buffers/side, 64KiB/s/side, 120s idle, 6h lifetime; registered peers only; NO encryption":"Relay disabled (operator must explicitly pass --relay)");
    ML_Message("Requests are logged with byte progress and failure reason; no names/tokens/payload dumps. Missing remote events means no observed request, not proof of firewall cause.");
    result=Run(listener);
    MR_Close();
    for(i=0;i<DM_CLIENTS;++i)Drop(&clients[i]);
    DM_Close(listener);
cleanup:
    PA_Close(&public_address);
#ifdef _WIN32
    WSACleanup();
#endif
    ML_Message("Master: stopped"); ML_Close();
    return result;
usage:
    fputs("Invalid/missing option. Use dptdoom-master --help\n",stderr); return 2;
}
