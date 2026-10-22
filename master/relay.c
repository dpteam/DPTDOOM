// Two outgoing TCP peers, authenticated host lease, public session-id guest.
// No destinations/URLs/resource transfers accepted. GPL-2.0-or-later.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#define CLOSE closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <errno.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define CLOSE close
#endif
#include "relay.h"
#include "log.h"
#include <stdio.h>

#define MR_DEFAULT_RATE 65536
#define MR_DEFAULT_PER_SOURCE 4
static mr_limits_t limits = { MR_SESSIONS, MR_DEFAULT_PER_SOURCE, MR_DEFAULT_RATE,
                              MR_BUFFER, 120, 21600 };
typedef struct
{
    SOCKET socket;
    unsigned char data[MR_BUFFER];
    unsigned int have, sent, reply_sent, allowance;
    int eof,write_closed;
    unsigned char reply[DM_HEADER];
} mr_peer_t;
typedef struct
{
    int used,paired;
    unsigned char id[16];
    unsigned char source[4];
    mr_peer_t peer[2];
    uint64_t began,activity,window;
    uint64_t bytes[2];
} mr_session_t;
static mr_session_t sessions[MR_SESSIONS];
static int Blocked(void)
{
#ifdef _WIN32
    return WSAGetLastError()==WSAEWOULDBLOCK || WSAGetLastError()==WSAEINTR;
#else
    return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR;
#endif
}
static void Drop(unsigned int index,const char *reason,uint64_t now)
{
    mr_session_t *s=&sessions[index];unsigned int i;char text[256];
    if(!s->used)return;
    snprintf(text,sizeof(text),"Relay: slot=%u closed reason=%s paired=%d duration_ms=%llu bytes_host=%llu bytes_guest=%llu",
        index,reason,s->paired,(unsigned long long)(now-s->began),(unsigned long long)s->bytes[0],(unsigned long long)s->bytes[1]);
    ML_Detail(ML_RELAY,NULL,0,now,text);
    for(i=0;i<2;++i)if(s->peer[i].socket!=INVALID_SOCKET)CLOSE(s->peer[i].socket);
    memset(s,0,sizeof(*s));s->peer[0].socket=s->peer[1].socket=INVALID_SOCKET;
}
void MR_Init(void)
{unsigned int i;memset(sessions,0,sizeof(sessions));for(i=0;i<MR_SESSIONS;++i)sessions[i].peer[0].socket=sessions[i].peer[1].socket=INVALID_SOCKET;}
int MR_SetLimits(const mr_limits_t *wanted)
{
    // Every bound is checked here rather than at each use site, so there is one
    // place to see what the relay can be configured to do. Rejecting the whole
    // struct keeps a bad config from applying half a change.
    if(!wanted)return 0;
    if(wanted->sessions<1 || wanted->sessions>MR_SESSIONS)return 0;
    if(wanted->per_source<1 || wanted->per_source>4)return 0;
    if(wanted->rate<4096 || wanted->rate>1048576)return 0;
    if(wanted->buffer<512 || wanted->buffer>MR_BUFFER)return 0;
    if(wanted->idle_seconds<10 || wanted->idle_seconds>3600)return 0;
    if(wanted->lifetime_seconds<60 || wanted->lifetime_seconds>86400)return 0;
    // A per-source cap above the session count would be unreachable, which reads
    // as a configured limit that silently does nothing.
    if(wanted->per_source>wanted->sessions)return 0;
    // Lifetime below idle would make the idle limit unreachable in the same way.
    if(wanted->lifetime_seconds<wanted->idle_seconds)return 0;
    limits=*wanted;
    return 1;
}
const mr_limits_t *MR_Limits(void){return &limits;}
static dm_entry_t *Entry(dm_directory_t *d,const unsigned char id[16])
{unsigned int i;for(i=0;i<DM_MAX_SERVERS;++i)if(d->entries[i].active&&!memcmp(d->entries[i].id,id,16))return &d->entries[i];return NULL;}
unsigned int MR_Attach(dm_directory_t *d,uintptr_t socket,unsigned int op,const unsigned char source[4],const unsigned char *body,uint64_t now)
{
    unsigned int i,free_slot=MR_SESSIONS,source_sessions=0;unsigned char reply[4];dm_entry_t *entry=Entry(d,body);mr_session_t *s;
    if(!entry)return DM_UNAUTHORIZED;
    if(op==DM_RELAY_HOST && DM_Renew(d,source,body,now,0,reply)!=DM_OK)return DM_UNAUTHORIZED;
    for(i=0;i<MR_SESSIONS;++i)
    {
        // Only the first limits.sessions slots are usable, so a lowered limit
        // really lowers capacity instead of just being reported as lower.
        if(!sessions[i].used){if(i<limits.sessions && free_slot==MR_SESSIONS)free_slot=i;continue;}
        if(!memcmp(sessions[i].id,body,16))break;
    }
    if(op==DM_RELAY_HOST)
    {
        if(i<MR_SESSIONS)return DM_CONFLICT;
        for(i=0;i<MR_SESSIONS;++i)if(sessions[i].used&&!memcmp(sessions[i].source,source,4))++source_sessions;
        if(source_sessions>=limits.per_source)return DM_CAPACITY;
        if(free_slot==MR_SESSIONS)return DM_CAPACITY;
        s=&sessions[free_slot];s->used=1;s->began=s->activity=s->window=now;
        memcpy(s->id,body,16);s->peer[0].socket=(SOCKET)socket;entry->relay_available=1;
        memcpy(s->source,source,4);
        ML_Detail(ML_RELAY,source,0,now,"relay-host-attached; waiting for guest; payload/auth omitted");return DM_OK;
    }
    if(i==MR_SESSIONS||sessions[i].paired)return DM_CONFLICT;
    s=&sessions[i];s->peer[1].socket=(SOCKET)socket;s->paired=1;s->began=s->activity=now;
    for(i=0;i<2;++i){s->peer[i].allowance=limits.rate;DM_Header(s->peer[i].reply,i?DM_RELAY_JOIN:DM_RELAY_HOST,0,DM_OK);}
    // The listing is consumed atomically with pairing. Raw game handshake still
    // validates build/resources/rules; no other guest may enter this pair.
    memset(entry,0,sizeof(*entry));
    ML_Detail(ML_RELAY,source,0,now,"relay-paired; two registered session peers; no outbound destination");return DM_OK;
}
void MR_Prepare(dm_directory_t *d,fd_set *reads,fd_set *writes,uintptr_t *maximum,uint64_t now)
{
    unsigned int i,j;
    for(i=0;i<DM_MAX_SERVERS;++i)d->entries[i].relay_available=0;
    for(i=0;i<MR_SESSIONS;++i)
    {
        mr_session_t *s=&sessions[i];dm_entry_t *entry;
        if(!s->used)continue;
        entry=Entry(d,s->id);
        if(!s->paired && !entry){Drop(i,"lease-removed/expired",now);continue;}
        if(!s->paired&&entry)entry->relay_available=1;
        // A waiting authenticated host is kept alive by its directory lease,
        // not by game data. Its idle/lifetime clock starts only after pairing.
        if(s->paired&&(now-s->activity>=(uint64_t)limits.idle_seconds*1000 || now-s->began>=(uint64_t)limits.lifetime_seconds*1000)){Drop(i,"idle/lifetime-limit",now);continue;}
        if(s->paired && s->peer[0].eof && s->peer[1].eof && !s->peer[0].have && !s->peer[1].have)
        {Drop(i,"both-peers-finished",now);continue;}
        if(now-s->window>=1000){s->window=now;for(j=0;j<2;++j)s->peer[j].allowance=limits.rate;}
        for(j=0;j<(s->paired?2u:1u);++j)
        {
            mr_peer_t *p=&s->peer[j],*other=&s->peer[1-j];
            if((uintptr_t)p->socket>*maximum)*maximum=(uintptr_t)p->socket;
            if(!s->paired){FD_SET(p->socket,reads);continue;}
            if(p->reply_sent<DM_HEADER || other->have>other->sent)FD_SET(p->socket,writes);
            // Backpressure: a peer's buffer is the data waiting for the other
            // side; never read more than one bounded buffer or rate allowance.
            if(p->reply_sent==DM_HEADER && !p->have && p->allowance && !p->eof)FD_SET(p->socket,reads);
            if(other->eof && !other->have && !p->write_closed && p->reply_sent==DM_HEADER)
            {
#ifdef _WIN32
                shutdown(p->socket,SD_SEND);
#else
                shutdown(p->socket,SHUT_WR);
#endif
                p->write_closed=1;
            }
        }
    }
}
void MR_Poll(fd_set *reads,fd_set *writes,uint64_t now)
{
    unsigned int i,j;
    for(i=0;i<MR_SESSIONS;++i)
    {
        mr_session_t *s=&sessions[i];if(!s->used)continue;
        for(j=0;j<(s->paired?2u:1u)&&s->used;++j)
        {
            mr_peer_t *p=&s->peer[j],*other=&s->peer[1-j];int n;
            if(FD_ISSET(p->socket,writes))
            {
                unsigned char *data;unsigned int remaining;
                int header=p->reply_sent<DM_HEADER;
                if(header){data=p->reply+p->reply_sent;remaining=DM_HEADER-p->reply_sent;}
                else{data=other->data+other->sent;remaining=other->have-other->sent;}
                if(remaining)
                {
#ifdef MSG_NOSIGNAL
                    n=(int)send(p->socket,(char*)data,remaining,MSG_NOSIGNAL);
#else
                    n=(int)send(p->socket,(char*)data,remaining,0);
#endif
                    if(n<=0){if(!n||!Blocked())Drop(i,"send-failed",now);continue;}
                    s->activity=now;
                    if(header)p->reply_sent+=(unsigned)n;
                    else {other->sent+=(unsigned)n;if(other->sent==other->have)other->sent=other->have=0;}
                }
            }
            if(s->used&&FD_ISSET(p->socket,reads))
            {
                unsigned int capacity=p->allowance<limits.buffer?p->allowance:limits.buffer;
                if(!s->paired)capacity=1;
                n=(int)recv(p->socket,(char*)p->data,capacity,0);
                if(n<=0)
                {
                    if(!n && s->paired){p->eof=1;s->activity=now;}
                    else if(!n||!Blocked())Drop(i,"peer-left/transport-loss",now);
                    continue;
                }
                if(!s->paired){Drop(i,"unexpected-data-before-pair",now);continue;}
                p->have=(unsigned)n;p->sent=0;p->allowance-=(unsigned)n;s->bytes[j]+=(unsigned)n;s->activity=now;
            }
        }
    }
}
void MR_Close(void)
{unsigned int i;for(i=0;i<MR_SESSIONS;++i)Drop(i,"master-shutdown",sessions[i].activity);}
