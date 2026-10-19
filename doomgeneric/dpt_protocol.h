// Explicit byte codec, never transmit a native C structure. GPL-2.0-or-later.
#ifndef DPT_PROTOCOL_H
#define DPT_PROTOCOL_H
#include "d_ticcmd.h"
#define DPT_NET_PROTOCOL 1
#define DPT_NET_HELLO_SIZE 96
#define DPT_NET_TIC_SIZE 24

static inline void DPT_Put32(byte *p, uint32_t value)
{
    p[0] = (byte)(value >> 24); p[1] = (byte)(value >> 16);
    p[2] = (byte)(value >> 8); p[3] = (byte)value;
}
static inline uint32_t DPT_Get32(const byte *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static inline void DPT_EncodeCommand(byte *p, const ticcmd_t *cmd)
{
    p[0] = (byte)cmd->forwardmove; p[1] = (byte)cmd->sidemove;
    p[2] = (byte)((uint16_t)cmd->angleturn >> 8); p[3] = (byte)cmd->angleturn;
    p[4] = cmd->buttons; p[5] = cmd->chatchar; p[6] = cmd->consistancy; p[7] = 0;
}
static inline boolean DPT_DecodeCommand(const byte *p, ticcmd_t *cmd)
{
    if (p[7] || (int8_t)p[0] < -50 || (int8_t)p[0] > 50
        || (int8_t)p[1] < -50 || (int8_t)p[1] > 50) return false;
    memset(cmd, 0, sizeof(*cmd));
    cmd->forwardmove = (signed char)(p[0] < 128 ? p[0] : (int)p[0] - 256);
    cmd->sidemove = (signed char)(p[1] < 128 ? p[1] : (int)p[1] - 256);
    {
        unsigned int turn = (unsigned int)p[2] << 8 | p[3];
        cmd->angleturn = (short)(turn < 32768 ? (int)turn : (int)turn - 65536);
    }
    cmd->buttons = p[4]; cmd->chatchar = p[5]; cmd->consistancy = p[6];
    return true;
}
#endif
