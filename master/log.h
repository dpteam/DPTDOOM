// Bounded native master event log. GPL-2.0-or-later; see LICENSE.
#ifndef DPT_MASTER_LOG_H
#define DPT_MASTER_LOG_H
#include <stdint.h>
enum { ML_CONNECT, ML_LIST, ML_REGISTER, ML_HEARTBEAT, ML_REMOVE, ML_EXPIRE,
       ML_INVALID, ML_AUTH, ML_LIMIT, ML_TIMEOUT, ML_IO, ML_EVENTS };
int ML_Open(const char *path, unsigned int max_bytes);
void ML_Message(const char *message);
void ML_Event(unsigned int event, const unsigned char *host, unsigned int port, uint64_t now);
void ML_Flush(uint64_t now, int force);
void ML_Close(void);
#endif
