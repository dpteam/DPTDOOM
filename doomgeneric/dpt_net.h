// DPT.DOOM direct two-player lockstep. GPL-2.0-or-later; see LICENSE.
#ifndef DPT_NET_H
#define DPT_NET_H
#include "net_defs.h"

#ifdef DPTDOOM_NETWORK
boolean DPT_NetRequested(void);
boolean DPT_NetActive(void);
void DPT_NetStart(net_gamesettings_t *settings, void (*pump)(void));
void DPT_NetExchange(ticcmd_t *commands, unsigned int tic);
void DPT_NetShutdown(void);
unsigned int DPT_NetStateChecksum(void);
#else
#define DPT_NetRequested() false
#define DPT_NetActive() false
#define DPT_NetShutdown() ((void)0)
#endif
#endif
