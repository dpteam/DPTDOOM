// Boom generalized locked door decoding.
// GPL-2.0-or-later; see LICENSE.
#ifndef DPT_GENLOCKED_H
#define DPT_GENLOCKED_H

#include "doomtype.h"

// Generalized locked doors: 0x3800..0x3bff. Generalized doors start at 0x3c00.
#define DPT_GEN_LOCKED_BASE 0x3800
#define DPT_GEN_LOCKED_END  0x3bff

// Held-key bitmask, used by the data-free key check.
#define DPT_KEY_BLUECARD    (1u << 0)
#define DPT_KEY_YELLOWCARD  (1u << 1)
#define DPT_KEY_REDCARD     (1u << 2)
#define DPT_KEY_BLUESKULL   (1u << 3)
#define DPT_KEY_YELLOWSKULL (1u << 4)
#define DPT_KEY_REDSKULL    (1u << 5)
#define DPT_KEY_ALL         0x3fu

// Lock field values.
enum
{
    DPT_LOCK_ANY = 0,
    DPT_LOCK_REDCARD = 1,
    DPT_LOCK_BLUECARD = 2,
    DPT_LOCK_YELLOWCARD = 3,
    DPT_LOCK_REDSKULL = 4,
    DPT_LOCK_BLUESKULL = 5,
    DPT_LOCK_YELLOWSKULL = 6,
    DPT_LOCK_ALL = 7
};

typedef struct
{
    int     trigger;      // 0=W1 1=WR 2=S1 3=SR 4=G1 5=GR 6=P1 7=PR
    int     speed_shift;  // 0..3 -> 1x, 2x, 4x, 8x VDOORSPEED
    int     kind;         // 0=open/wait/close, 1=open and stay
    int     lock;         // DPT_LOCK_*
    boolean skull_card;   // skull and card interchangeable
} dpt_genlocked_t;

boolean DPT_GenLockedIsLocked(int special);
boolean DPT_GenLockedDecode(int special, dpt_genlocked_t *out);
boolean DPT_GenLockedSupported(int special);

// True when `held` (DPT_KEY_* bits) satisfies the lock.
boolean DPT_GenLockedSatisfied(int lock, boolean skull_card,
                              unsigned int held);

#endif
