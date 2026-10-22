// Opt-in, bounded session relay, never an arbitrary TCP proxy. GPL-2.0-or-later.
#ifndef DPT_MASTER_RELAY_H
#define DPT_MASTER_RELAY_H
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif
#include "directory.h"
// Hard ceilings. These are array sizes, so they cannot be raised by config: the
// buffer is embedded in every peer slot, and a session table that grows at
// runtime would need allocation the relay deliberately does not do.
#define MR_SESSIONS 8
#define MR_BUFFER 4096

// Operator-tunable limits. Every field has a hard range enforced by MR_SetLimits
// so a config file cannot disable the bounds the relay exists to guarantee.
//
// buffer can only shrink. It is the size of a fixed array in each peer slot, so
// raising it is a compile-time change; shrinking it is useful on a tiny host and
// harmless, because backpressure already works from one buffer per direction.
typedef struct
{
    unsigned int sessions;        // pairs, 1..MR_SESSIONS
    unsigned int per_source;      // waiting hosts per source IP, 1..4
    unsigned int rate;            // bytes/s per side, 4096..1048576
    unsigned int buffer;          // bytes per direction, 512..MR_BUFFER
    unsigned int idle_seconds;    // paired session idle limit, 10..3600
    unsigned int lifetime_seconds;// paired session hard limit, 60..86400
} mr_limits_t;

void MR_Init(void);
// Applies validated limits. Returns 0 if any value is out of range, leaving the
// previous limits untouched, so a bad config cannot half-apply.
int MR_SetLimits(const mr_limits_t *limits);
// The active limits, for startup logging.
const mr_limits_t *MR_Limits(void);
// Success transfers ownership of the nonblocking socket. Reply is deferred
// until both peers are paired, then raw game bytes follow the DPTM reply.
unsigned int MR_Attach(dm_directory_t *directory,uintptr_t socket,unsigned int operation,
                       const unsigned char source[4],const unsigned char *body,uint64_t now);
void MR_Prepare(dm_directory_t *directory,fd_set *reads,fd_set *writes,uintptr_t *maximum,uint64_t now);
void MR_Poll(fd_set *reads,fd_set *writes,uint64_t now);
void MR_Close(void);
#endif
