// Operator-only public address discovery. GPL-2.0-or-later; see LICENSE.
#ifndef DPT_PUBLIC_ADDRESS_H
#define DPT_PUBLIC_ADDRESS_H
#include "directory.h"
typedef struct
{
    char mode[16], host[254], ip[64], api[512];
    char subnets[DM_MAX_ADDRESS_MAPS][32];
    unsigned int subnet_count, refresh_seconds, seen;
    uint64_t next, good_until;
    unsigned char current[4];
    int have_current, timed_out;
    void *job;
} pa_state_t;
void PA_Init(pa_state_t *state);
int PA_Config(pa_state_t *state,const char *key,const char *value);
int PA_Prepare(pa_state_t *state,dm_directory_t *directory);
void PA_Tick(pa_state_t *state,dm_directory_t *directory,uint64_t now);
void PA_Close(pa_state_t *state);
int PA_ParseIPv4(const char *text,unsigned char ip[4]);
int PA_ValidHost(const char *text);
int PA_ValidAPI(const char *url);
// Apply validated provider result, independently testable without network.
int PA_Apply(pa_state_t *state,dm_directory_t *directory,const unsigned char ip[4],uint64_t now);
#endif
