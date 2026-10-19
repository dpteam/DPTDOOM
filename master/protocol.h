// DPT.DOOM directory protocol v1; independent of game protocol v2.
// GPL-2.0-or-later; see LICENSE.
#ifndef DPT_MASTER_PROTOCOL_H
#define DPT_MASTER_PROTOCOL_H
#include <stdint.h>
#include <string.h>

#define DM_HEADER 16
#define DM_INFO 132
#define DM_ROW 160
#define DM_LEASE 48
#define DM_AUTH 40
#define DM_MAX_SERVERS 128
#define DM_MAX_IP_SERVERS 4
#define DM_MAX_BODY (4 + DM_ROW * DM_MAX_SERVERS)
#define DM_VERSION 1
#define DM_GAME_PROTOCOL 2
enum { DM_LIST=1, DM_REGISTER, DM_HEARTBEAT, DM_REMOVE };
enum { DM_OK=0, DM_BAD_REQUEST, DM_UNAUTHORIZED, DM_CONFLICT, DM_CAPACITY };

static inline uint16_t DM_Get16(const unsigned char *p)
{ return (uint16_t)((uint16_t)p[0] << 8 | p[1]); }
static inline uint32_t DM_Get32(const unsigned char *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static inline void DM_Put16(unsigned char *p, unsigned int n)
{ p[0]=(unsigned char)(n>>8); p[1]=(unsigned char)n; }
static inline void DM_Put32(unsigned char *p, uint32_t n)
{ p[0]=(unsigned char)(n>>24); p[1]=(unsigned char)(n>>16); p[2]=(unsigned char)(n>>8); p[3]=(unsigned char)n; }
static inline void DM_Header(unsigned char *p, unsigned int op, unsigned int length, unsigned int status)
{
    memset(p,0,DM_HEADER); memcpy(p,"DPTM",4); DM_Put16(p+4,DM_VERSION);
    DM_Put16(p+6,op); DM_Put32(p+8,length); DM_Put16(p+12,status);
}
static inline int DM_RequestLength(const unsigned char *p)
{
    unsigned int op=DM_Get16(p+6), length=DM_Get32(p+8);
    if(memcmp(p,"DPTM",4) || DM_Get16(p+4)!=DM_VERSION || DM_Get16(p+12) || DM_Get16(p+14))return -1;
    if(op==DM_LIST && !length)return 0;
    if(op==DM_REGISTER && length==DM_INFO)return DM_INFO;
    if((op==DM_HEARTBEAT || op==DM_REMOVE) && length==DM_AUTH)return DM_AUTH;
    return -1;
}
static inline const char *DM_HeaderError(const unsigned char *p)
{
    if(memcmp(p,"DPTM",4))return "magic-not-DPTM(wrong-service/proxy/probe)";
    if(DM_Get16(p+4)!=DM_VERSION)return "directory-version-mismatch";
    if(DM_Get16(p+12)||DM_Get16(p+14))return "nonzero-request-status/reserved";
    if(DM_Get16(p+6)<DM_LIST||DM_Get16(p+6)>DM_REMOVE)return "unknown-operation";
    if(DM_RequestLength(p)<0)return "invalid-body-length";
    return NULL;
}
#endif
