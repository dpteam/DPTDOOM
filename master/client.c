// Shared by engine publisher and launcher browser; no blocking reads/writes.
// GPL-2.0-or-later; see LICENSE.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#define CLOSE closesocket
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
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define CLOSE close
#endif
#include "client.h"
#include "directory.h"
#include <stdio.h>

static int SocketError(void)
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}
const char *MC_StatusName(unsigned int status)
{
    switch(status) { case DM_OK:return "OK";case DM_BAD_REQUEST:return "BAD_REQUEST(metadata/protocol)";
    case DM_UNAUTHORIZED:return "UNAUTHORIZED(expired/wrong-token/source-IP)";
    case DM_CONFLICT:return "CONFLICT(endpoint-already-listed/id-collision)";
    case DM_CAPACITY:return "CAPACITY(global-or-per-IP)";default:return "UNKNOWN"; }
}
const char *MC_OperationName(unsigned int op)
{
    switch(op) { case DM_LIST:return "LIST";case DM_REGISTER:return "REGISTER";
    case DM_HEARTBEAT:return "HEARTBEAT";case DM_REMOVE:return "REMOVE";default:return "UNKNOWN"; }
}
static void LocalAddress(dm_client_query_t *q)
{
    struct sockaddr_in address;
#ifdef _WIN32
    int size=sizeof(address);
#else
    socklen_t size=sizeof(address);
#endif
    if(q->socket!=(uintptr_t)INVALID_SOCKET && !getsockname((SOCKET)q->socket,(struct sockaddr*)&address,&size))
    { inet_ntop(AF_INET,&address.sin_addr,q->local,sizeof(q->local)); q->local_port=ntohs(address.sin_port); }
}
void MC_Diagnostic(const dm_client_query_t *q,char *text,size_t capacity)
{
    const char *stage=q->stage==1?"connect":q->stage==2?"send":q->stage==3?(q->received<DM_HEADER?"receive-header":"receive-body"):"setup";
    const char *hint="";
    if(q->failure && !strcmp(q->failure,"deadline"))hint=q->stage==1?
        "; hint=no TCP completion; check route/VPN/firewall/forwarding (not proof of which one)":
        "; hint=TCP opened but DPTM reply incomplete; check tunnel/proxy/wrong service and server log";
#ifdef _WIN32
    if(q->os_error==10061)hint="; hint=connection refused (no listener or active reject)";
    else if(q->os_error==10060)hint="; hint=socket timeout; route/filter/peer must be checked";
    else if(q->os_error==10054)hint="; hint=connection reset by peer/intermediary";
    else if(q->os_error==10051||q->os_error==10065)hint="; hint=network/host unreachable";
    else if(q->os_error==10013)hint="; hint=socket access denied; local policy/security software possible";
#endif
    snprintf(text,capacity,"op=%s endpoint=%s:%u local=%s:%u stage=%s result=%s reason=%s os_error=%d status=%u/%s elapsed_ms=%llu sent=%u/%u received=%u/%u%s",
        MC_OperationName(q->op),q->remote[0]?q->remote:"unset",q->remote_port,q->local[0]?q->local:"unknown",q->local_port,
        stage,q->result<0?"FAILED":q->result>0?(q->status?"REJECTED":"OK"):"PENDING",q->failure?q->failure:"none",
        q->os_error,q->status,MC_StatusName(q->status),(unsigned long long)((q->ended?q->ended:q->began)-q->began),
        q->sent,q->request_size,q->received,q->wanted,hint);
}

static int Blocked(void)
{
#ifdef _WIN32
    int e=WSAGetLastError(); return e==WSAEWOULDBLOCK || e==WSAEINPROGRESS || e==WSAEINTR;
#else
    return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINPROGRESS || errno==EINTR;
#endif
}
static void Disconnect(dm_client_query_t *q)
{
    if(q->socket!=(uintptr_t)INVALID_SOCKET)CLOSE((SOCKET)q->socket);
    q->socket=(uintptr_t)INVALID_SOCKET;
#ifdef _WIN32
    if(q->started)WSACleanup();
#endif
    q->started=0;
}
void MC_Init(dm_client_query_t *q) { memset(q,0,sizeof(*q)); q->socket=(uintptr_t)INVALID_SOCKET; }
void MC_Close(dm_client_query_t *q) { Disconnect(q); MC_Init(q); }
static int Finish(dm_client_query_t *q,int result) { q->result=result; LocalAddress(q); Disconnect(q); return result; }
static int Fail(dm_client_query_t *q,const char *reason,int error)
{ q->failure=reason;q->os_error=error;return Finish(q,-1); }
int MC_Endpoint(const char *text,char host[254],unsigned int *port)
{
    unsigned int length=0,value=0, label=0;
    const char *p=text;
    while(*p && *p!=':')
    {
        unsigned char ch=(unsigned char)*p++;
        if(length>=253 || !((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='.'))return 0;
        if(ch=='.') { if(!label || host[length-1]=='-')return 0; label=0; }
        else { if(!label && ch=='-')return 0; if(++label>63)return 0; }
        host[length++]=(char)ch;
    }
    if(!length || !label || host[length-1]=='-' || *p!=':')return 0;
    ++p;
    host[length]=0;
    if(!*p)return 0;
    while(*p) { if(*p<'0'||*p>'9'||value>6553)return 0; value=value*10+(unsigned int)(*p++-'0'); if(value>65535)return 0; }
    if(!value)return 0;
    *port=value; return 1;
}
int MC_Begin(dm_client_query_t *q,const char *ip,unsigned int port,unsigned int op,const unsigned char *body,unsigned int size,uint64_t now)
{
    struct sockaddr_in address;
    SOCKET s;
    MC_Close(q);
    q->op=op;q->began=now;q->remote_port=port;
    snprintf(q->remote,sizeof(q->remote),"%.63s",ip);
    if(!port || port>65535 || size>DM_INFO)return Fail(q,"invalid-arguments",0);
    DM_Header(q->request,op,size,0);
    if(DM_RequestLength(q->request)<0 || (size&&!body))return Fail(q,"invalid-request",0);
    if(size)memcpy(q->request+DM_HEADER,body,size);
    memset(&address,0,sizeof(address)); address.sin_family=AF_INET; address.sin_port=htons((uint16_t)port);
    if(inet_pton(AF_INET,ip,&address.sin_addr)!=1)return Fail(q,"invalid-IPv4",0);
#ifdef _WIN32
    { WSADATA data; int error=WSAStartup(MAKEWORD(2,2),&data);if(error)return Fail(q,"WSAStartup",error); }
#endif
    q->started=1; q->op=op; q->request_size=DM_HEADER+size; q->wanted=DM_HEADER; q->deadline=now+2500;
    s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); q->socket=(uintptr_t)s;
    if(s==INVALID_SOCKET)return Fail(q,"socket",SocketError());
#ifdef _WIN32
    { u_long on=1; if(ioctlsocket(s,FIONBIO,&on))return Fail(q,"nonblocking",SocketError()); }
#else
    { int flags=fcntl(s,F_GETFL,0); if(s>=FD_SETSIZE)return Fail(q,"fd-capacity",0);if(flags<0 || fcntl(s,F_SETFL,flags|O_NONBLOCK))return Fail(q,"nonblocking",SocketError()); }
#endif
#ifdef SO_NOSIGPIPE
    { int on=1; setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on)); }
#endif
    q->stage=1;
    if(connect(s,(struct sockaddr*)&address,sizeof(address))==0)q->stage=2;
    else if(!Blocked())return Fail(q,"connect",SocketError());
    LocalAddress(q);
    return 0;
}
int MC_ValidList(const unsigned char *p,unsigned int size)
{
    unsigned int count,i;
    if(size<4 || (count=DM_Get32(p))>DM_MAX_SERVERS || size!=4+count*DM_ROW)return 0;
    for(i=0;i<count;++i)
    {
        const unsigned char *row=p+4+i*DM_ROW;
        if(!DM_ValidInfo(row+24) || DM_Get32(row+156) || DM_Get32(row+20)>300
            || row[16]==0 || row[16]>=224 || (row[16]==255))return 0;
    }
    return 1;
}
int MC_Poll(dm_client_query_t *q,uint64_t now)
{
    SOCKET s=(SOCKET)q->socket;
    int n;
    if(q->result)return q->result;
    q->ended=now;
    if(!q->stage)return Fail(q,"query-not-started",0);
    if(now>=q->deadline)return Fail(q,"deadline",0);
    if(q->stage==1)
    {
        fd_set writes,errors; struct timeval zero={0,0};
        FD_ZERO(&writes); FD_ZERO(&errors); FD_SET(s,&writes); FD_SET(s,&errors);
#ifdef _WIN32
        n=select(0,NULL,&writes,&errors,&zero);
#else
        n=select(s+1,NULL,&writes,&errors,&zero);
#endif
        if(n<0) { if(Blocked())return 0; return Fail(q,"select-connect",SocketError()); }
        if(!n)return 0;
        { int error=0;
#ifdef _WIN32
          int length=sizeof(error);
#else
          socklen_t length=sizeof(error);
#endif
          if(getsockopt(s,SOL_SOCKET,SO_ERROR,(char*)&error,&length))return Fail(q,"getsockopt-connect",SocketError());
          if(error)return Fail(q,"connect",error); }
        q->stage=2;
    }
    if(q->stage==2)
    {
#ifdef MSG_NOSIGNAL
        n=(int)send(s,(const char*)q->request+q->sent,(int)(q->request_size-q->sent),MSG_NOSIGNAL);
#else
        n=(int)send(s,(const char*)q->request+q->sent,(int)(q->request_size-q->sent),0);
#endif
        if(n<0) { if(Blocked())return 0; return Fail(q,"send",SocketError()); }
        if(!n)return Fail(q,"send-zero",0);
        q->sent+=(unsigned int)n;
        if(q->sent<q->request_size)return 0;
        q->stage=3;
    }
    n=(int)recv(s,(char*)q->response+q->received,(int)(q->wanted-q->received),0);
    if(n<0) { if(Blocked())return 0; return Fail(q,"receive",SocketError()); }
    if(!n)return Fail(q,"peer-closed-before-complete-reply",0);
    q->received+=(unsigned int)n;
    if(q->received!=q->wanted)return 0;
    if(q->wanted==DM_HEADER)
    {
        const unsigned char *h=q->response; unsigned int length=DM_Get32(h+8);
        q->status=DM_Get16(h+12);
        if(memcmp(h,"DPTM",4))return Fail(q,"reply-magic-not-DPTM(wrong-service/proxy)",0);
        if(DM_Get16(h+4)!=DM_VERSION)return Fail(q,"reply-directory-version",0);
        if(DM_Get16(h+6)!=q->op)return Fail(q,"reply-operation",0);
        if(DM_Get16(h+14)||q->status>DM_CAPACITY)return Fail(q,"reply-reserved/status",0);
        if(length>DM_MAX_BODY || (q->status&&length))return Fail(q,"reply-length",0);
        if(q->status)return Finish(q,1);
        if((q->op==DM_REGISTER && length!=DM_LEASE) || (q->op>=DM_HEARTBEAT && length!=4)
            || (q->op==DM_LIST && length<4))return Fail(q,"reply-body-size",0);
        q->wanted=DM_HEADER+length; return 0;
    }
    { const unsigned char *body=q->response+DM_HEADER;
      if(q->op==DM_LIST && !MC_ValidList(body,q->wanted-DM_HEADER))return Fail(q,"reply-list-schema",0);
      if(q->op==DM_REGISTER && (DM_Get32(body+40)<3 || DM_Get32(body+40)>300
          || !DM_Get32(body+44) || DM_Get32(body+44)>=DM_Get32(body+40)))return Fail(q,"reply-lease-schema",0); }
    return Finish(q,1);
}
