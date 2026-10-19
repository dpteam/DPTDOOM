// Experimental direct TCP lockstep, exactly two players, trusted LAN only.
// GPL version 2 or (at your option) any later version. See LICENSE.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#define net_close closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define net_close close
#endif
#include "dpt_net.h"
#include "dpt_protocol.h"
#include "dpt_rules.h"
#include "config.h"
#include "doomstat.h"
#include "d_event.h"
#include "doomgeneric.h"
#include "i_system.h"
#include "m_argv.h"
#include "p_saveg.h"
#include "w_wad.h"
#include "w_file.h"
#include <limits.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include "../master/client.h"
#include "dpt_connect_screen.h"

static dm_client_query_t master_query;
static boolean master_initialized, master_enabled, master_waiting, master_removing;
static char master_ip[64];
static unsigned int master_port;
static byte master_info[DM_INFO], master_auth[DM_AUTH];
static boolean master_has_lease;
static uint32_t master_next;
static unsigned int master_interval=20;
static unsigned int master_attempt;
static dm_client_query_t relay_query;
static boolean relay_initialized,relay_pending,relay_requested;
static uint32_t relay_retry;

static void RelayHostPoll(void)
{
    uint32_t now=DG_GetTicksMs();int result;
    if(!relay_requested||!master_has_lease)return;
    if(!relay_pending && (int32_t)(now-relay_retry)>=0)
    {
        MC_Begin(&relay_query,master_ip,master_port,DM_RELAY_HOST,master_auth,DM_AUTH,now);
        relay_query.deadline=UINT64_MAX;relay_pending=true;
        puts("Relay: attaching outgoing host channel to master (no game-port forwarding needed for relay)");fflush(stdout);
    }
    if(!relay_pending)return;
    result=MC_Poll(&relay_query,now);
    if(result<0|| (result>0&&relay_query.status))
    {
        char diagnostic[1024];MC_Diagnostic(&relay_query,diagnostic,sizeof(diagnostic));printf("Relay: host attach failed %s\n",diagnostic);fflush(stdout);
        MC_Close(&relay_query);relay_pending=false;relay_retry=now+10000;
    }
}

static void MasterDiagnostic(const char *event)
{
    char diagnostic[1024],stamp[32];time_t wall=time(NULL);struct tm *utc=gmtime(&wall);
    if(!utc||!strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%SZ",utc))strcpy(stamp,"unknown-time");
    MC_Diagnostic(&master_query,diagnostic,sizeof(diagnostic));
    printf("Master [%s]: %s attempt=%u %s\n",stamp,event,master_attempt,diagnostic);fflush(stdout);
}

static void MasterPoll(void)
{
    uint32_t now=DG_GetTicksMs();
    int result;
    if(!master_enabled)return;
    if(!master_waiting && (int32_t)(now-master_next)>=0)
    {
        MC_Begin(&master_query,master_ip,master_port,master_has_lease?DM_HEARTBEAT:DM_REGISTER,
            master_has_lease?master_auth:master_info,master_has_lease?DM_AUTH:DM_INFO,now);
        ++master_attempt;MasterDiagnostic("request-start");
        master_waiting=true;
    }
    if(!master_waiting)return;
    result=MC_Poll(&master_query,now);
    if(!result)return;
    master_waiting=false; master_next=now+10000;
    if(result<0 || master_query.status)
    {
        if(result>0 && master_query.status==DM_UNAUTHORIZED)master_has_lease=false;
        MasterDiagnostic("request-failed");
        puts("Master: direct host still waiting; retry_ms=10000. No automatic firewall/router changes."); fflush(stdout);
    }
    else
    {
        MasterDiagnostic("request-complete");
        if(!master_has_lease)
        {
            memcpy(master_auth,master_query.response+DM_HEADER,DM_AUTH); master_has_lease=true;
            master_interval=DM_Get32(master_query.response+DM_HEADER+44);
            puts("Master: waiting coop host published (game-port reachability unverified)"); fflush(stdout);
        }
        master_next=now+master_interval*1000;
    }
    MC_Close(&master_query);
}
static void MasterRemove(void)
{
    uint32_t start;
    if(!master_initialized || master_removing)return;
    master_removing=true; master_enabled=false;
    if(relay_initialized){MC_Close(&relay_query);relay_pending=false;}
    // A pending registration may have committed remotely before its response.
    // Give it a bounded chance to return the lease, then remove; otherwise TTL.
    start=DG_GetTicksMs();
    if(master_waiting && !master_has_lease)
    {
        int result=0;
        while(!result && (uint32_t)(DG_GetTicksMs()-start)<250)
        { result=MC_Poll(&master_query,DG_GetTicksMs()); if(!result)DG_SleepMs(1); }
        MasterDiagnostic(result?"pending-register-complete":"pending-register-cleanup-budget-exhausted");
        if(result>0 && !master_query.status)
        { memcpy(master_auth,master_query.response+DM_HEADER,DM_AUTH); master_has_lease=true; }
    }
    MC_Close(&master_query);
    if(master_has_lease)
    {
        int result=0;
        start=DG_GetTicksMs();
        MC_Begin(&master_query,master_ip,master_port,DM_REMOVE,master_auth,DM_AUTH,start);
        while(!result && (uint32_t)(DG_GetTicksMs()-start)<250)
        { result=MC_Poll(&master_query,DG_GetTicksMs()); if(!result)DG_SleepMs(1); }
        MasterDiagnostic(result?"remove-complete":"remove-cleanup-budget-exhausted");
        puts(result>0 && !master_query.status?"Master: advertisement removed":"Master: remove not confirmed; advertisement expires by TTL");
        fflush(stdout); MC_Close(&master_query);
    }
    memset(master_auth,0,sizeof(master_auth)); master_has_lease=false; master_waiting=false;
    master_removing=false;
}

static SOCKET connection = INVALID_SOCKET, listener = INVALID_SOCKET;
static boolean active, sockets_started;
static boolean alone;
static int local_player;
static uint32_t timeout_ms = 10000;
static boolean host_wait_forever;
static void (*pump_events)(void);
static uint32_t last_pump;
extern int prndindex;
static boolean disconnect_requested;
static char disconnect_reason[192];

static boolean SessionFailure(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(disconnect_reason, sizeof(disconnect_reason), format, args);
    va_end(args);
    if (!active || M_ParmExists("-net-fail-fast"))
        I_Error("%s", disconnect_reason);
    // Caller unwinds to TryRunTics before changing game/loop state.
    return false;
}

void DPT_NetRequestDisconnect(void) { if (active) disconnect_requested = true; }
const char *DPT_NetDisconnectReason(void) { return disconnect_reason; }

static boolean PeerLost(const char *reason)
{
    if (!active || M_ParmExists("-net-fail-fast"))
        return SessionFailure("%s", reason);
    // Retain session/rules/local slot. There is no server or second transport
    // to migrate: continue this local world, with no future remote commands.
    snprintf(disconnect_reason, sizeof(disconnect_reason), "%s", reason);
    if (connection != INVALID_SOCKET) { net_close(connection); connection = INVALID_SOCKET; }
    alone = true;
    return false; // unwind I/O; Exchange turns this into a missing-player tic
}

static boolean WouldBlock(void)
{
#ifdef _WIN32
    int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS || errno == EINTR;
#endif
}

static void Nonblocking(SOCKET sock)
{
#ifdef _WIN32
    u_long value = 1;
    if (ioctlsocket(sock, FIONBIO, &value)) I_Error("Network: cannot set nonblocking socket");
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0)
        I_Error("Network: cannot set nonblocking socket");
#endif
}

static boolean Wait(uint32_t start, const char *operation)
{
    uint32_t now = DG_GetTicksMs();
    if (!(host_wait_forever&&!active&&!strcmp(operation,"accept")) && (uint32_t)(now - start) >= timeout_ms)
    {
        if (active) return PeerLost("Network timeout: peer no longer responding");
        if(!strcmp(operation,"accept"))
        {
            printf("Direct coop diagnostic: accept wait expired after %u ms; no second player connected.\n",timeout_ms);
            puts("Master publication and game TCP port are separate. A master listing does not relay game traffic or prove game-port reachability.");
            puts("For a longer host wait use -net-timeout 120000; firewall/NAT/VPN cause cannot be inferred from accept timeout alone.");fflush(stdout);
        }
        return SessionFailure("Network timeout during %s", operation);
    }
    if ((uint32_t)(now - last_pump) >= 20)
    {
        last_pump = now;
        if (pump_events) pump_events();
        if(!active)DPT_ConnectScreen(!strcmp(operation,"accept")?"WAITING FOR PLAYER...":!strcmp(operation,"relay join")?"TRYING RELAY...":"CONNECTING...",
            !strcmp(operation,"relay join")?"RELAY VIA MASTER":"DIRECT + OPTIONAL RELAY");
        else DG_DrawFrame();
    }
    DG_SleepMs(1);
    return true;
}

static boolean Transfer(byte *buffer, int size, boolean sending)
{
    int done = 0;
    uint32_t start = DG_GetTicksMs();
    while (done < size)
    {
        int result;
        if (disconnect_requested) return SessionFailure("Disconnected by local player");
        if (sending)
        {
#ifdef MSG_NOSIGNAL
            result = (int)send(connection, (const char *)buffer + done, size - done, MSG_NOSIGNAL);
#else
            result = (int)send(connection, (const char *)buffer + done, size - done, 0);
#endif
        }
        else result = (int)recv(connection, (char *)buffer + done, size - done, 0);
        if (result > 0) done += result;
        else if (!result)
        {
            if (!sending && done) return SessionFailure("Network: truncated tic packet before disconnect");
            return PeerLost("Network: peer disconnected");
        }
        else if (WouldBlock())
        {
            // A partially delivered command is not a clean departure. Never
            // silently accept it as a valid command, including on timeout.
            if (active && !sending && done && (uint32_t)(DG_GetTicksMs() - start) >= timeout_ms)
                return SessionFailure("Network: truncated tic packet timeout");
            if (!Wait(start, sending ? "send" : "receive")) return false;
        }
        else
        {
            if (!sending && done) return SessionFailure("Network: truncated tic packet connection failure");
            return PeerLost("Network: peer connection lost");
        }
    }
    return true;
}

static unsigned int Number(const char *text, unsigned int minimum, unsigned int maximum)
{
    unsigned long value = 0;
    const char *p;
    if (!text[0]) I_Error("Network: expected numeric port/timeout");
    for (p = text; *p; ++p)
    {
        if (*p < '0' || *p > '9' || value > maximum / 10)
            I_Error("Network: invalid numeric port/timeout");
        value = value * 10 + (*p - '0');
        if (value > maximum) I_Error("Network: port/timeout out of range");
    }
    if (value < minimum) I_Error("Network: port/timeout out of range");
    return (unsigned int)value;
}

static char *Argument(char *name)
{
    int p = M_CheckParm(name);
    if (!p) return NULL;
    if (p + 1 >= myargc || myargv[p + 1][0] == '-') I_Error("%s requires an argument", name);
    return myargv[p + 1];
}

boolean DPT_NetRequested(void)
{
    if((M_ParmExists("-net-relay")&&!M_ParmExists("-net-host"))
        ||((M_ParmExists("-relay-master")||M_ParmExists("-relay-session"))&&!M_ParmExists("-net-join")))
        I_Error("Relay requires a published host or an explicit join session");
    if((M_ParmExists("-master")||M_ParmExists("-session-name")) && !M_ParmExists("-net-host"))
        I_Error("Master publication requires -net-host");
    return M_ParmExists("-net-host") || M_ParmExists("-net-join");
}
boolean DPT_NetActive(void) { return active; }
boolean DPT_NetPeerPresent(void) { return active && !alone; }

void DPT_NetShutdown(void)
{
    MasterRemove();
    if(relay_initialized)MC_Close(&relay_query);
    active = false;
    alone = false;
    disconnect_requested = false;
    if (connection != INVALID_SOCKET) { net_close(connection); connection = INVALID_SOCKET; }
    if (listener != INVALID_SOCKET) { net_close(listener); listener = INVALID_SOCKET; }
#ifdef _WIN32
    if (sockets_started) WSACleanup();
#endif
    sockets_started = false;
}

static void ResourceDigest(byte *digest)
{
    sha1_context_t context;
    byte buffer[8192];
    unsigned int i;
    SHA1_Init(&context);
    // Hash effective content/order, not just lump names and sizes. Cache and
    // filenames/absolute paths are intentionally irrelevant across machines.
    for (i = 0; i < numlumps; ++i)
    {
        unsigned int offset = 0;
        lumpinfo_t *lump = &lumpinfo[i];
        if (lump->size < 0 || lump->position < 0) I_Error("Network: invalid resource directory");
        SHA1_Update(&context, (byte *)lump->name, 8);
        SHA1_UpdateInt32(&context, (unsigned int)lump->size);
        while (offset < (unsigned int)lump->size)
        {
            unsigned int length = (unsigned int)lump->size - offset;
            if (length > sizeof(buffer)) length = sizeof(buffer);
            if (W_Read(lump->wad_file, (unsigned int)lump->position + offset, buffer, length) != length)
                I_Error("Network: cannot read resource contents");
            SHA1_Update(&context, buffer, length);
            offset += length;
        }
    }
    SHA1_Final(digest, &context);
}

void DPT_NetStart(net_gamesettings_t *settings, void (*pump)(void))
{
    char *host = Argument("-net-host"), *join = Argument("-net-join");
    char *bind_ip = Argument("-net-bind"), *timeout = Argument("-net-timeout");
    char *master=Argument("-master"), *session_name=Argument("-session-name");
    char *relay_master=Argument("-relay-master"),*relay_session=Argument("-relay-session");
    byte relay_id[16];char relay_ip[64];unsigned int relay_port=0;
    struct sockaddr_in address;
    byte hello[DPT_NET_HELLO_SIZE] = {0}, remote[DPT_NET_HELLO_SIZE];
    sha1_context_t context;
    int values[11], i;
    uint32_t start;
    disconnect_reason[0] = 0;
    disconnect_requested = false;
    alone = false;
    if ((host != NULL) == (join != NULL)) I_Error("Use exactly one of -net-host or -net-join");
    if((master || session_name) && !host)I_Error("Master publication requires -net-host");
    if(session_name && !master)I_Error("-session-name requires -master");
    relay_requested=M_ParmExists("-net-relay");
    if(relay_requested&&(!host||!master))I_Error("-net-relay requires published host (-net-host and -master)");
    if((relay_master!=NULL)!=(relay_session!=NULL)|| (relay_master&&!join))I_Error("Relay fallback requires join plus -relay-master and -relay-session");
    if(relay_master)
    {
        char parsed[254];struct in_addr numeric;
        if(!MC_Endpoint(relay_master,parsed,&relay_port)||strlen(parsed)>=sizeof(relay_ip)||inet_pton(AF_INET,parsed,&numeric)!=1
            ||strlen(relay_session)!=32)I_Error("Invalid relay endpoint/session id");
        strcpy(relay_ip,parsed);
        for(i=0;i<16;++i)
        {
            char pair[3]={relay_session[i*2],relay_session[i*2+1],0};char *end;unsigned long value;
            if(!((pair[0]>='0'&&pair[0]<='9')||(pair[0]>='a'&&pair[0]<='f'))
                ||!((pair[1]>='0'&&pair[1]<='9')||(pair[1]>='a'&&pair[1]<='f')))I_Error("Invalid relay session id");
            value=strtoul(pair,&end,16);relay_id[i]=(byte)value;
        }
    }
    if (M_ParmExists("-playdemo") || M_ParmExists("-timedemo") || M_ParmExists("-record")
        || M_ParmExists("-loadgame") || M_ParmExists("-turbo") || M_ParmExists("-solo-net")
        || M_ParmExists("-left") || M_ParmExists("-right") || M_ParmExists("-drone")
        || M_ParmExists("-dup") || M_ParmExists("-headless-save-tic") || settings->deathmatch || settings->timelimit
        || (join && bind_ip))
        I_Error("Direct coop: demos/loadgame/turbo/deathmatch/timelimit/drones/ticdup are unsupported");
    if (settings->skill < sk_baby || settings->skill > sk_nightmare
        || settings->episode < 1 || settings->episode > (gamemode == shareware ? 1 : 4)
        || settings->map < 1 || settings->map > (gamemode == commercial ? 32 : 9))
        I_Error("Network: invalid skill/episode/map");
    if (timeout) timeout_ms = Number(timeout, 100, 120000);
    host_wait_forever=M_ParmExists("-net-wait-forever");
    if(host_wait_forever&&!host)I_Error("-net-wait-forever requires -net-host");
    pump_events = pump;
    I_AtExit(DPT_NetShutdown, true);
#ifdef _WIN32
    { WSADATA data; if (WSAStartup(MAKEWORD(2, 2), &data)) I_Error("WSAStartup failed"); }
#endif
    sockets_started = true;
    MC_Init(&relay_query);relay_initialized=true;relay_pending=false;relay_retry=DG_GetTicksMs();
    if(master)
    {
        char parsed[254]; struct in_addr numeric;
        const char *name=session_name?session_name:"DPT.DOOM COOP";
        size_t length=strlen(name);
        MC_Init(&master_query); master_initialized=true;
        if(!MC_Endpoint(master,parsed,&master_port) || strlen(parsed)>=sizeof(master_ip)
            || inet_pton(AF_INET,parsed,&numeric)!=1)I_Error("-master requires numeric IPv4:port (launcher resolves master domains)");
        strcpy(master_ip,parsed);
        memset(master_info,0,sizeof(master_info));
        if(!length || length>63)I_Error("Session name must be 1..63 printable ASCII characters");
        memcpy(master_info,name,length);
        SHA1_Init(&context); SHA1_UpdateString(&context,PACKAGE_STRING " direct-coop-v2 " DPT_NET_BUILD_ID);
        SHA1_Final(master_info+64,&context); ResourceDigest(master_info+84);
        if(gamemode==commercial)snprintf((char*)master_info+116,8,"MAP%02d",settings->map);
        else snprintf((char*)master_info+116,8,"E%dM%d",settings->episode,settings->map);
        DM_Put16(master_info+124,Number(host,1,65535)); master_info[126]=(byte)(settings->skill+1);
        master_info[127]=(byte)(dpt_dmflags | (settings->nomonsters?4:0) | (settings->fast_monsters?8:0) | (settings->respawn_monsters?16:0));
        master_info[128]=2;
        if(!DM_ValidInfo(master_info))I_Error("Invalid master session metadata/name");
        master_enabled=true; master_waiting=false; master_has_lease=false; master_next=DG_GetTicksMs();
        master_attempt=0;
        printf("Master diagnostic v2: endpoint=%s:%u directory_protocol=%u game_port=%s request_deadline_ms=2500 retry_ms=10000 accept_timeout_ms=%u\n",
            master_ip,master_port,DM_VERSION,host,timeout_ms);
        printf("Master metadata: map=%s skill=%u flags=%u game_protocol=%u (name/auth omitted)\n",master_info+116,master_info[126],master_info[127],master_info[128]);
        fflush(stdout);
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    if (host)
    {
        address.sin_port = htons((unsigned short)Number(host, 1, 65535));
        if (inet_pton(AF_INET, bind_ip ? bind_ip : "127.0.0.1", &address.sin_addr) != 1)
            I_Error("Network: -net-bind expects a numeric IPv4 address");
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET) I_Error("Network: cannot create listen socket");
        Nonblocking(listener);
        if (bind(listener, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR
            || listen(listener, 1) == SOCKET_ERROR) I_Error("Network: cannot bind/listen on requested address/port");
        printf("Direct coop: listening on %s:%s, waiting for one player\n", bind_ip ? bind_ip : "127.0.0.1", host);
        if(host_wait_forever)puts("Host wait: unlimited until player joins or game window is closed; connection/game timeouts remain bounded");
        printf("Direct coop diagnostic: accept_timeout_ms=%u master=%s build=%s\n",timeout_ms,master?master:"disabled",DPT_NET_BUILD_ID);
        fflush(stdout);
        start = DG_GetTicksMs();
        for(;;)
        {
            connection=accept(listener,NULL,NULL);
            if(connection!=INVALID_SOCKET)break;
            if (!WouldBlock()) I_Error("Network: accept failed");
            RelayHostPoll();
            if(relay_pending&&relay_query.result==1&&!relay_query.status)
            {
                connection=(SOCKET)MC_TakeSocket(&relay_query);relay_pending=false;
                master_has_lease=false; // pairing atomically consumed listing
                puts("Relay: host paired through master; using relayed game transport");fflush(stdout);break;
            }
            MasterPoll();
            Wait(start, "accept");
        }
        net_close(listener); listener = INVALID_SOCKET;
        MasterRemove();
        local_player = 0;
    }
    else
    {
        char ip[64];
        const char *colon = strrchr(join, ':');
        size_t length;
        if (!colon || !(length = (size_t)(colon - join)) || length >= sizeof(ip))
            I_Error("Network: -net-join expects numeric IPv4:port");
        memcpy(ip, join, length); ip[length] = 0;
        address.sin_port = htons((unsigned short)Number(colon + 1, 1, 65535));
        if (inet_pton(AF_INET, ip, &address.sin_addr) != 1)
            I_Error("Network: -net-join expects a numeric IPv4 address");
        connection = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (connection == INVALID_SOCKET) I_Error("Network: cannot create client socket");
        Nonblocking(connection);
        printf("Direct coop connect: target=%s timeout_ms=%u fallback=%s\n",join,timeout_ms,relay_master?"relay-after-2000ms":"disabled");fflush(stdout);
        if (connect(connection, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR)
        {
            if (!WouldBlock()) {if(relay_master)goto relay_fallback;I_Error("Network: connect failed");}
            start = DG_GetTicksMs();
            for (;;)
            {
                fd_set writable,errors;
                struct timeval wait_time = {0, 0};
                FD_ZERO(&writable); FD_SET(connection, &writable);FD_ZERO(&errors);FD_SET(connection,&errors);
#ifdef _WIN32
                if (select(0, NULL, &writable, &errors, &wait_time) > 0)
#else
                if (select(connection + 1, NULL, &writable, &errors, &wait_time) > 0)
#endif
                {
                    int error = 0;
#ifdef _WIN32
                    int size = sizeof(error);
#else
                    socklen_t size = sizeof(error);
#endif
                    if (getsockopt(connection, SOL_SOCKET, SO_ERROR, (char *)&error, &size) || error)
                    {printf("Direct coop connect failed: socket_error=%d\n",error);fflush(stdout);if(relay_master)goto relay_fallback;I_Error("Network: connect refused/failed");}
                    break;
                }
                if(relay_master&&(uint32_t)(DG_GetTicksMs()-start)>=2000)goto relay_fallback;
                Wait(start, "connect");
            }
        }
        goto join_ready;
relay_fallback:
        net_close(connection);connection=INVALID_SOCKET;
        printf("Relay: direct unavailable; connecting outgoing guest channel to %s (session id omitted)\n",relay_master);fflush(stdout);
        MC_Begin(&relay_query,relay_ip,relay_port,DM_RELAY_JOIN,relay_id,16,DG_GetTicksMs());
        start=DG_GetTicksMs();relay_query.deadline=(uint64_t)start+timeout_ms;
        for(;;)
        {
            int result=MC_Poll(&relay_query,DG_GetTicksMs());
            if(result)
            {
                if(result<0||relay_query.status){char diagnostic[1024];MC_Diagnostic(&relay_query,diagnostic,sizeof(diagnostic));I_Error("Relay join failed: %s",diagnostic);}
                connection=(SOCKET)MC_TakeSocket(&relay_query);break;
            }
            Wait(start,"relay join");
        }
        puts("Relay: guest paired through master; using relayed game transport");fflush(stdout);
join_ready:
        local_player = 1;
    }
    Nonblocking(connection);
#ifdef SO_NOSIGPIPE
    { int flag = 1; setsockopt(connection, SOL_SOCKET, SO_NOSIGPIPE, &flag, sizeof(flag)); }
#endif
    { int flag = 1; setsockopt(connection, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag)); }
    memcpy(hello, "DPTNET02", 8);
    DPT_Put32(hello + 8, DPT_NET_PROTOCOL);
    SHA1_Init(&context);
    SHA1_UpdateString(&context, PACKAGE_STRING " direct-coop-v2 " DPT_NET_BUILD_ID);
    SHA1_Final(hello + 12, &context);
    ResourceDigest(hello + 32);
    values[0] = gamemode; values[1] = gamemission; values[2] = settings->gameversion;
    values[3] = settings->skill; values[4] = settings->episode; values[5] = settings->map;
    values[6] = settings->nomonsters; values[7] = settings->fast_monsters;
    values[8] = settings->respawn_monsters; values[9] = local_player;
    values[10] = 2;
    for (i = 0; i < 11; ++i) DPT_Put32(hello + 52 + i * 4, (uint32_t)values[i]);
    DPT_Put32(hello + 96, dpt_dmflags);
    Transfer(hello, sizeof(hello), true); Transfer(remote, sizeof(remote), false);
    if (memcmp(remote, hello, 12)) I_Error("Network protocol mismatch");
    if (memcmp(remote + 12, hello + 12, 20)) I_Error("Network engine version mismatch");
    if (memcmp(remote + 32, hello + 32, 20)) I_Error("Network resource content/order mismatch");
    if (memcmp(remote + 52, hello + 52, 36)) I_Error("Network game settings mismatch (skill/map/flags/version)");
    if (DPT_Get32(remote + 96) != dpt_dmflags)
        I_Error("Network dmflags mismatch (both players must use the same rules)");
    if (DPT_Get32(remote + 88) != (unsigned int)(1 - local_player) || DPT_Get32(remote + 92) != 2)
        I_Error("Network: invalid player assignment");
    active = true;
    settings->consoleplayer = local_player; settings->num_players = 2;
    settings->ticdup = 1; settings->new_sync = 0; settings->lowres_turn = false;
    settings->loadgame = -1;
    printf("Direct coop: connected as player %d of 2 (protocol %d)\n", local_player + 1, DPT_NET_PROTOCOL);
    printf("Match rules: dmflags=%u coop-no-extra-weapons=%s players-pass-through=%s\n",
        dpt_dmflags, dpt_dmflags & DPT_DMF_COOP_NO_EXTRA_WEAPONS ? "on" : "off",
        dpt_dmflags & DPT_DMF_PLAYERS_PASS_THROUGH ? "on" : "off");
    fflush(stdout);
}

unsigned int DPT_NetStateChecksum(void)
{
    // Projection deliberately masks renderer-discovered automap flags.
    return P_NetArchiveChecksum() ^ ((unsigned int)prndindex * 0x9e3779b9u)
        ^ (dpt_dmflags * 0x85ebca6bu);
}

boolean DPT_NetExchange(ticcmd_t *commands, unsigned int tic)
{
    byte packet[DPT_NET_TIC_SIZE] = {0}, remote[DPT_NET_TIC_SIZE];
    unsigned int checksum = tic ? DPT_NetStateChecksum() : 0;
    if (disconnect_requested) return SessionFailure("Disconnected by local player");
    if (alone)
    {
        memset(&commands[1 - local_player], 0, sizeof(*commands));
        return true;
    }
    memcpy(packet, "TIC1", 4); DPT_Put32(packet + 4, tic); DPT_Put32(packet + 8, checksum);
    DPT_EncodeCommand(packet + 12, &commands[local_player]);
    if (!Transfer(packet, sizeof(packet), true) || !Transfer(remote, sizeof(remote), false))
    {
        if (!alone) return false;
        memset(&commands[1 - local_player], 0, sizeof(*commands));
        return true;
    }
    if (memcmp(remote, "TIC1", 4) || DPT_Get32(remote + 4) != tic || DPT_Get32(remote + 20))
        return SessionFailure("Network: malformed/out-of-order tic packet");
    if (DPT_Get32(remote + 8) != checksum)
        return SessionFailure("Network desync before tic %u (%08x vs %08x)", tic, checksum, DPT_Get32(remote + 8));
    if (!DPT_DecodeCommand(remote + 12, &commands[1 - local_player]))
        return SessionFailure("Network: invalid player command");
    if ((commands[1 - local_player].buttons & BT_SPECIAL)
        && commands[1 - local_player].buttons != (BT_SPECIAL | BTS_PAUSE))
        return SessionFailure("Network: unsupported special command");
    return true;
}
