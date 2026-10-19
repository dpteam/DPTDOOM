// Nonblocking directory client, numeric IPv4 transport. GPL-2.0-or-later.
#ifndef DPT_MASTER_CLIENT_H
#define DPT_MASTER_CLIENT_H
#include "protocol.h"
#include "directory.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct
{
    uintptr_t socket;
    unsigned int stage, sent, received, wanted, request_size, op, status;
    uint64_t deadline;
    int started, result; // 0 pending, 1 success, -1 transport/protocol error
    unsigned char request[DM_HEADER+DM_INFO], response[DM_HEADER+DM_MAX_BODY];
} dm_client_query_t;
// Call once before use. Begin closes a previous query. No DNS or outgoing I/O
// occurs until an explicit Begin. Caller owns monotonic time/poll scheduling.
void MC_Init(dm_client_query_t *query);
void MC_Close(dm_client_query_t *query);
int MC_Endpoint(const char *text, char host[254], unsigned int *port);
int MC_Begin(dm_client_query_t *query, const char *ipv4, unsigned int port,
             unsigned int op, const unsigned char *body, unsigned int size, uint64_t now);
int MC_Poll(dm_client_query_t *query, uint64_t now);
int MC_ValidList(const unsigned char *body, unsigned int size);
#ifdef __cplusplus
}
#endif
#endif
