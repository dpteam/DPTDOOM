// Boom generalized lift (platform) decoding.
// GPL-2.0-or-later; see LICENSE.
#ifndef DPT_GENLIFT_H
#define DPT_GENLIFT_H

#include "doomtype.h"
#include "m_fixed.h"

// Generalized lifts: 0x3400..0x37ff. Locked doors start at 0x3800.
#define DPT_GEN_LIFT_BASE 0x3400
#define DPT_GEN_LIFT_END  0x37ff

// BOOM platform targets, 1-based.
enum
{
    DPT_LIFT_TARGET_LNF = 1,      // lowest neighbour floor
    DPT_LIFT_TARGET_NNLF = 2,     // next lowest neighbour floor
    DPT_LIFT_TARGET_LNC = 3,      // lowest neighbour ceiling
    DPT_LIFT_TARGET_PERPETUAL = 4 // lowest <-> highest neighbour floor
};

typedef struct
{
    int     trigger;        // 0=W1 1=WR 2=S1 3=SR 4=G1 5=GR 6=P1 7=PR
    int     speed_eighths;  // 8/16/32/64 eighths of a unit per tic
    boolean monsters;
    int     delay_tics;     // 35, 105, 175 or 350 (BOOM: 1/3/5/10 seconds)
    int     target;         // DPT_LIFT_TARGET_*
} dpt_genlift_t;

boolean DPT_GenLiftIsLift(int special);
boolean DPT_GenLiftDecode(int special, dpt_genlift_t *out);
boolean DPT_GenLiftSupported(int special);

typedef struct
{
    fixed_t current;           // this sector's floor height
    fixed_t lowest_floor;      // lowest surrounding floor
    fixed_t next_lowest_floor; // highest surrounding floor below current
    fixed_t lowest_ceiling;    // lowest surrounding ceiling
} dpt_lift_env_t;

// Destination (the low point of the travel) for a supported generalized lift.
// Mirrors BOOM: the next-lowest target is deliberately unclamped, so a sector
// with nothing lower simply does not move, and the ceiling target is clamped to
// the current floor so the platform cannot start above itself.
fixed_t DPT_GenLiftDest (const dpt_genlift_t* gen, const dpt_lift_env_t* env);

#endif
