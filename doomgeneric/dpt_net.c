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

static SOCKET connection = INVALID_SOCKET, listener = INVALID_SOCKET;
static boolean active, sockets_started;
static int local_player;
static uint32_t timeout_ms = 10000;
static void (*pump_events)(void);
static uint32_t last_pump;
extern int prndindex;

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

static void Wait(uint32_t start, const char *operation)
{
    uint32_t now = DG_GetTicksMs();
    if ((uint32_t)(now - start) >= timeout_ms)
        I_Error("Network timeout during %s", operation);
    if ((uint32_t)(now - last_pump) >= 20)
    {
        last_pump = now;
        if (pump_events) pump_events();
        DG_DrawFrame();
    }
    DG_SleepMs(1);
}

static void Transfer(byte *buffer, int size, boolean sending)
{
    int done = 0;
    uint32_t start = DG_GetTicksMs();
    while (done < size)
    {
        int result;
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
        else if (!result) I_Error("Network: peer disconnected");
        else if (WouldBlock()) Wait(start, sending ? "send" : "receive");
        else I_Error("Network: connection failed during %s", sending ? "send" : "receive");
    }
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
    return M_ParmExists("-net-host") || M_ParmExists("-net-join");
}
boolean DPT_NetActive(void) { return active; }

void DPT_NetShutdown(void)
{
    active = false;
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
    struct sockaddr_in address;
    byte hello[DPT_NET_HELLO_SIZE] = {0}, remote[DPT_NET_HELLO_SIZE];
    sha1_context_t context;
    int values[11], i;
    uint32_t start;
    if ((host != NULL) == (join != NULL)) I_Error("Use exactly one of -net-host or -net-join");
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
    pump_events = pump;
    I_AtExit(DPT_NetShutdown, true);
#ifdef _WIN32
    { WSADATA data; if (WSAStartup(MAKEWORD(2, 2), &data)) I_Error("WSAStartup failed"); }
#endif
    sockets_started = true;
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
        fflush(stdout);
        start = DG_GetTicksMs();
        while ((connection = accept(listener, NULL, NULL)) == INVALID_SOCKET)
        {
            if (!WouldBlock()) I_Error("Network: accept failed");
            Wait(start, "accept");
        }
        net_close(listener); listener = INVALID_SOCKET;
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
        if (connect(connection, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR)
        {
            if (!WouldBlock()) I_Error("Network: connect failed");
            start = DG_GetTicksMs();
            for (;;)
            {
                fd_set writable;
                struct timeval wait_time = {0, 0};
                FD_ZERO(&writable); FD_SET(connection, &writable);
#ifdef _WIN32
                if (select(0, NULL, &writable, NULL, &wait_time) > 0)
#else
                if (select(connection + 1, NULL, &writable, NULL, &wait_time) > 0)
#endif
                {
                    int error = 0;
#ifdef _WIN32
                    int size = sizeof(error);
#else
                    socklen_t size = sizeof(error);
#endif
                    if (getsockopt(connection, SOL_SOCKET, SO_ERROR, (char *)&error, &size) || error)
                        I_Error("Network: connect refused/failed");
                    break;
                }
                Wait(start, "connect");
            }
        }
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

void DPT_NetExchange(ticcmd_t *commands, unsigned int tic)
{
    byte packet[DPT_NET_TIC_SIZE] = {0}, remote[DPT_NET_TIC_SIZE];
    unsigned int checksum = tic ? DPT_NetStateChecksum() : 0;
    memcpy(packet, "TIC1", 4); DPT_Put32(packet + 4, tic); DPT_Put32(packet + 8, checksum);
    DPT_EncodeCommand(packet + 12, &commands[local_player]);
    Transfer(packet, sizeof(packet), true); Transfer(remote, sizeof(remote), false);
    if (memcmp(remote, "TIC1", 4) || DPT_Get32(remote + 4) != tic || DPT_Get32(remote + 20))
        I_Error("Network: malformed/out-of-order tic packet");
    if (DPT_Get32(remote + 8) != checksum)
        I_Error("Network desync before tic %u (%08x vs %08x)", tic, checksum, DPT_Get32(remote + 8));
    if (!DPT_DecodeCommand(remote + 12, &commands[1 - local_player]))
        I_Error("Network: invalid player command");
    if ((commands[1 - local_player].buttons & BT_SPECIAL)
        && commands[1 - local_player].buttons != (BT_SPECIAL | BTS_PAUSE))
        I_Error("Network: unsupported special command");
}
