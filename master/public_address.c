// No external request unless explicitly enabled by the master operator config.
// Workers own independent input/result; slow DNS/TLS never blocks accept loop.
// GPL-2.0-or-later; see LICENSE.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#else
#define _POSIX_C_SOURCE 200809L
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>
#ifdef DPT_MASTER_CURL
#include <curl/curl.h>
#endif
#endif
#include "public_address.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct
{
    char mode[16],host[254],url[512],reason[128];
    unsigned char result[4];
    int ok;
#ifdef _WIN32
    HANDLE thread;
    CRITICAL_SECTION mutex;
#else
    pthread_t thread;
    pthread_mutex_t mutex;
#endif
    int done,abandoned;
} pa_job_t;
static void Lock(pa_job_t *j)
{
#ifdef _WIN32
    EnterCriticalSection(&j->mutex);
#else
    pthread_mutex_lock(&j->mutex);
#endif
}
static void Unlock(pa_job_t *j)
{
#ifdef _WIN32
    LeaveCriticalSection(&j->mutex);
#else
    pthread_mutex_unlock(&j->mutex);
#endif
}
static void DestroyMutex(pa_job_t *j)
{
#ifdef _WIN32
    DeleteCriticalSection(&j->mutex);
#else
    pthread_mutex_destroy(&j->mutex);
#endif
}

int PA_ParseIPv4(const char *text,unsigned char ip[4])
{
    unsigned int i;
    const char *p=text;
    while(*p==' '||*p=='\r'||*p=='\n'||*p=='\t')++p;
    for(i=0;i<4;++i)
    {
        unsigned int digits=0,value=0;
        while(*p>='0'&&*p<='9'){value=value*10+(unsigned)(*p++-'0');if(++digits>3||value>255)return 0;}
        if(!digits)return 0;
        ip[i]=(unsigned char)value;
        if(i<3&&*p++!='.')return 0;
    }
    while(*p==' '||*p=='\r'||*p=='\n'||*p=='\t')++p;
    return !*p&&DM_PublicIPv4(ip);
}
int PA_ValidHost(const char *p)
{
    unsigned int n=0,label=0;
    char last=0;
    if(!*p)return 0;
    while(*p)
    {
        char c=*p++;
        if(++n>253)return 0;
        if(c=='.'){if(!label||last=='-')return 0;label=0;}
        else {if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-')
            ||(!label&&c=='-')||++label>63)return 0;}
        last=c;
    }
    return label&&last!='-';
}
int PA_ValidAPI(const char *url)
{
    char host[254];size_t n;const char *p,*end;
    if(strncmp(url,"https://",8)||strlen(url)>=512)return 0;
    p=url+8;end=strchr(p,'/');n=end?(size_t)(end-p):strlen(p);
    if(!n||n>=sizeof(host))return 0;
    memcpy(host,p,n);host[n]=0;if(!PA_ValidHost(host))return 0;
    // Fixed HTTPS/443, no credentials, query tokens, fragment, redirects or
    // arbitrary headers. This is a simple plain-text IP endpoint, not JSON.
    for(p=end?end:p+n;*p;++p)if((unsigned char)*p<33||(unsigned char)*p>126||*p=='?'||*p=='#'||*p=='\\')return 0;
    return 1;
}
void PA_Init(pa_state_t *s)
{memset(s,0,sizeof(*s));strcpy(s->mode,"off");s->refresh_seconds=300;}
int PA_Config(pa_state_t *s,const char *key,const char *value)
{
    char *target=NULL;size_t size=0;unsigned int bit=0;
    if(!strcmp(key,"public-address-mode")){target=s->mode;size=sizeof(s->mode);bit=1;}
    else if(!strcmp(key,"public-host")){target=s->host;size=sizeof(s->host);bit=2;}
    else if(!strcmp(key,"public-ip")){target=s->ip;size=sizeof(s->ip);bit=4;}
    else if(!strcmp(key,"public-api")){target=s->api;size=sizeof(s->api);bit=8;}
    else if(!strcmp(key,"hairpin-subnet"))
    {
        if(!*value||strlen(value)>=32||s->subnet_count>=DM_MAX_ADDRESS_MAPS)return 0;
        strcpy(s->subnets[s->subnet_count++],value);return 1;
    }
    else if(!strcmp(key,"public-refresh-seconds"))
    {
        unsigned int n=0;const char *p=value;
        if(s->seen&16||!*p)return 0;
        while(*p){if(*p<'0'||*p>'9'||n>360)return 0;n=n*10+(unsigned)(*p++-'0');}
        if(n<30||n>3600)return 0;s->refresh_seconds=n;s->seen|=16;return 1;
    }
    else return 0;
    if(s->seen&bit||!*value||strlen(value)>=size)return 0;
    strcpy(target,value);s->seen|=bit;return 1;
}
int PA_Prepare(pa_state_t *s,dm_directory_t *d)
{
    unsigned int i;unsigned char ip[4];
    if(!strcmp(s->mode,"off"))return !s->subnet_count&&!(s->seen&14);
    if(!s->subnet_count)return 0;
    if(!strcmp(s->mode,"manual")){if((s->seen&14)!=4||!PA_ParseIPv4(s->ip,ip))return 0;}
    else if(!strcmp(s->mode,"dns")){if((s->seen&14)!=2||!PA_ValidHost(s->host))return 0;}
    else if(!strcmp(s->mode,"api"))
    {
        if((s->seen&14)!=8||!PA_ValidAPI(s->api))return 0;
#if !defined(_WIN32) && !defined(DPT_MASTER_CURL)
        fputs("Master: api mode requires libcurl at build time on this platform\n",stderr);return 0;
#endif
    }
    else return 0;
    for(i=0;i<s->subnet_count;++i)
    {
        if(!DM_AddHairpinSubnet(d,s->subnets[i]))return 0;
    }
    if(!strcmp(s->mode,"manual"))return PA_Apply(s,d,ip,0);
    return 1;
}
static void Invalidate(pa_state_t *s,dm_directory_t *d)
{
    unsigned int i;
    // Leases with old mapped addresses must not advertise an obsolete WAN IP.
    for(i=0;i<DM_MAX_SERVERS;++i)
        if(d->entries[i].active&&!DM_MappingReady(d,d->entries[i].host))memset(&d->entries[i],0,sizeof(d->entries[i]));
    s->have_current=0;
}
int PA_Apply(pa_state_t *s,dm_directory_t *d,const unsigned char ip[4],uint64_t now)
{
    unsigned int i;char message[240];int changed=!s->have_current||memcmp(s->current,ip,4);
    if(!DM_PublicIPv4(ip))return 0;
    if(changed)
    {
        for(i=0;i<d->address_map_count;++i)if(d->address_maps[i].dynamic)d->address_maps[i].enabled=0;
        Invalidate(s,d);
        for(i=0;i<d->address_map_count;++i)if(d->address_maps[i].dynamic)
        {memcpy(d->address_maps[i].advertised,ip,4);d->address_maps[i].enabled=1;}
        memcpy(s->current,ip,4);s->have_current=1;
        snprintf(message,sizeof(message),"Public address: mode=%s resolved=%u.%u.%u.%u; operator hairpin ranges only; previous mapped leases invalidated; reachability unverified",
            s->mode,ip[0],ip[1],ip[2],ip[3]);ML_Message(message);
    }
    s->good_until=now+(uint64_t)s->refresh_seconds*3000;
    return 1;
}

static void DNS(pa_job_t *j)
{
    struct addrinfo hints,*list=NULL,*p;int error;
    memset(&hints,0,sizeof(hints));hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
    error=getaddrinfo(j->host,NULL,&hints,&list);
    if(error){snprintf(j->reason,sizeof(j->reason),"DNS resolver error=%d",error);return;}
    for(p=list;p;p=p->ai_next)
    {
        unsigned char ip[4];memcpy(ip,&((struct sockaddr_in*)p->ai_addr)->sin_addr,4);
        if(DM_PublicIPv4(ip)){memcpy(j->result,ip,4);j->ok=1;break;}
    }
    freeaddrinfo(list);
    if(!j->ok)strcpy(j->reason,"DNS has no accepted public IPv4 (split-DNS/private answer?)");
}
#ifdef _WIN32
static void API(pa_job_t *j)
{
    wchar_t url[512]={0},host[254]={0},path[512]={0};URL_COMPONENTS parts;HINTERNET session=NULL,connection=NULL,request=NULL;
    char body[65];DWORD total=0,status=0,size=sizeof(status),got;int ok=0;DWORD error=0;
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,j->url,-1,url,512);
    memset(&parts,0,sizeof(parts));parts.dwStructSize=sizeof(parts);parts.lpszHostName=host;parts.dwHostNameLength=254;parts.lpszUrlPath=path;parts.dwUrlPathLength=512;
    if(!WinHttpCrackUrl(url,0,0,&parts))goto done;
    session=WinHttpOpen(L"DPT.DOOM master public-address/1",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session)goto done;
    WinHttpSetTimeouts(session,3000,3000,3000,3000);
    connection=WinHttpConnect(session,host,INTERNET_DEFAULT_HTTPS_PORT,0);if(!connection)goto done;
    request=WinHttpOpenRequest(connection,L"GET",path[0]?path:L"/",NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);if(!request)goto done;
    {DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;if(!WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy)))goto done;}
    if(!WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request,NULL)
        ||!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,NULL,&status,&size,NULL))goto done;
    if(status!=200){snprintf(j->reason,sizeof(j->reason),"HTTPS status=%lu (redirects disabled)",status);goto done;}
    while(total<sizeof(body)-1)
    {
        if(!WinHttpReadData(request,body+total,(DWORD)(sizeof(body)-1-total),&got))goto done;
        if(!got){body[total]=0;ok=total==strlen(body)&&PA_ParseIPv4(body,j->result);break;}
        total+=got;
    }
    if(ok)j->ok=1;else strcpy(j->reason,"HTTPS body is not a bounded public IPv4 plain-text value");
done:
    error=GetLastError();
    if(!j->ok&&!j->reason[0])snprintf(j->reason,sizeof(j->reason),"WinHTTP error=%lu",error);
    if(request)WinHttpCloseHandle(request);if(connection)WinHttpCloseHandle(connection);if(session)WinHttpCloseHandle(session);
}
#elif defined(DPT_MASTER_CURL)
typedef struct {char body[65];size_t size;} pa_body_t;
static size_t Body(char *data,size_t size,size_t count,void *context)
{
    pa_body_t *b=(pa_body_t*)context;size_t n=size*count;
    if(n>sizeof(b->body)-1-b->size)return 0;
    memcpy(b->body+b->size,data,n);b->size+=n;return n;
}
static void API(pa_job_t *j)
{
    CURL *curl;CURLcode result;long status=0;pa_body_t body={{0},0};
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK){strcpy(j->reason,"curl global init failed");return;}
    curl=curl_easy_init();if(!curl){strcpy(j->reason,"curl init failed");curl_global_cleanup();return;}
    curl_easy_setopt(curl,CURLOPT_URL,j->url);curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,3000L);curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,5000L);
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,Body);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&body);
    result=curl_easy_perform(curl);curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    body.body[body.size]=0;
    if(result==CURLE_OK&&status==200&&body.size==strlen(body.body)&&PA_ParseIPv4(body.body,j->result))j->ok=1;
    else snprintf(j->reason,sizeof(j->reason),"HTTPS curl_error=%d status=%ld or invalid bounded IPv4 body",(int)result,status);
    curl_easy_cleanup(curl);curl_global_cleanup();
}
#else
static void API(pa_job_t *j){strcpy(j->reason,"API mode unavailable without libcurl");}
#endif
#ifdef _WIN32
static DWORD WINAPI Worker(void *argument)
#else
static void *Worker(void *argument)
#endif
{
    pa_job_t *j=(pa_job_t*)argument;
    int abandoned;
#ifdef _WIN32
    WSADATA wsa;int startup=WSAStartup(MAKEWORD(2,2),&wsa);
    if(startup)snprintf(j->reason,sizeof(j->reason),"worker WSAStartup error=%d",startup);
    else
#endif
    if(!strcmp(j->mode,"dns"))DNS(j);else API(j);
#ifdef _WIN32
    if(!startup)WSACleanup();
#endif
    Lock(j);j->done=1;abandoned=j->abandoned;Unlock(j);
    if(abandoned){DestroyMutex(j);free(j);}
    return 0;
}
static int Ready(pa_job_t *j)
{
#ifdef _WIN32
    return WaitForSingleObject(j->thread,0)==WAIT_OBJECT_0;
#else
    int done;pthread_mutex_lock(&j->mutex);done=j->done;pthread_mutex_unlock(&j->mutex);return done;
#endif
}
void PA_Tick(pa_state_t *s,dm_directory_t *d,uint64_t now)
{
    pa_job_t *j=(pa_job_t*)s->job;unsigned int i;
    if(!strcmp(s->mode,"off")||!strcmp(s->mode,"manual"))return;
    if(s->have_current&&now>=s->good_until)
    {
        for(i=0;i<d->address_map_count;++i)if(d->address_maps[i].dynamic)d->address_maps[i].enabled=0;
        Invalidate(s,d);ML_Message("Public address: last-good value expired; mapped registrations disabled until discovery succeeds");
    }
    if(j)
    {
        if(Ready(j))
        {
#ifdef _WIN32
            CloseHandle(j->thread);
            DestroyMutex(j);
#else
            pthread_join(j->thread,NULL);pthread_mutex_destroy(&j->mutex);
#endif
            if(!s->timed_out&&j->ok)PA_Apply(s,d,j->result,now);
            else {char message[240];snprintf(message,sizeof(message),"Public address discovery failed: %s; last-good used only until bounded expiry",s->timed_out?"worker exceeded 5s deadline; late result discarded":j->reason);ML_Message(message);}
            free(j);s->job=NULL;s->next=now+(uint64_t)s->refresh_seconds*1000;
        }
        else if(!s->timed_out&&now>=s->next+5000){s->timed_out=1;ML_Message("Public address discovery deadline 5s; worker still pending, accept loop remains active");}
        return;
    }
    if(now<s->next)return;
    j=(pa_job_t*)calloc(1,sizeof(*j));if(!j){s->next=now+30000;return;}
    strcpy(j->mode,s->mode);strcpy(j->host,s->host);strcpy(j->url,s->api);s->timed_out=0;s->next=now;
#ifdef _WIN32
    InitializeCriticalSection(&j->mutex);
    j->thread=CreateThread(NULL,0,Worker,j,0,NULL);
    if(!j->thread){DestroyMutex(j);free(j);s->next=now+30000;return;}
#else
    if(pthread_mutex_init(&j->mutex,NULL)){free(j);s->next=now+30000;return;}
    if(pthread_create(&j->thread,NULL,Worker,j)){pthread_mutex_destroy(&j->mutex);free(j);s->next=now+30000;return;}
#endif
    s->job=j;
}
void PA_Close(pa_state_t *s)
{
    pa_job_t *j=(pa_job_t*)s->job;
    // Process shutdown only; never kill a resolver/TLS worker inside library code.
    // Worker has its own copies and no references to directory/log/config state.
    if(j)
    {
        int done;
#ifdef _WIN32
        HANDLE thread=j->thread;
#else
        pthread_t thread=j->thread;
#endif
        Lock(j);done=j->done;j->abandoned=!done;Unlock(j);
#ifdef _WIN32
        if(done)WaitForSingleObject(thread,INFINITE);
        CloseHandle(thread);
#else
        if(done)pthread_join(thread,NULL);else pthread_detach(thread);
#endif
        if(done){DestroyMutex(j);free(j);}
    }
    s->job=NULL;
}
