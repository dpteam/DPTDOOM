// Opt-in, bounded session relay, never an arbitrary TCP proxy. GPL-2.0-or-later.
#ifndef DPT_MASTER_RELAY_H
#define DPT_MASTER_RELAY_H
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif
#include "directory.h"
#define MR_SESSIONS 8
void MR_Init(void);
// Success transfers ownership of the nonblocking socket. Reply is deferred
// until both peers are paired, then raw game bytes follow the DPTM reply.
unsigned int MR_Attach(dm_directory_t *directory,uintptr_t socket,unsigned int operation,
                       const unsigned char source[4],const unsigned char *body,uint64_t now);
void MR_Prepare(dm_directory_t *directory,fd_set *reads,fd_set *writes,uintptr_t *maximum,uint64_t now);
void MR_Poll(fd_set *reads,fd_set *writes,uint64_t now);
void MR_Close(void);
#endif
