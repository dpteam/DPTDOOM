// Boom generalized sector specials: bit decoding and damage rules.
// GPL-2.0-or-later; see LICENSE.
//
// Layout per BOOM's p_spec.h (DAMAGE_MASK / DAMAGE_SHIFT / SECRET_MASK /
// FRICTION_MASK / PUSH_MASK):
//
//   base      0x001f  (the vanilla light/door/secret type, 0..31)
//   damage    0x0060  (0=none, 1=5, 2=10, 3=20 per 32 tics)
//   secret    0x0080  (sector counts as a secret)
//   friction  0x0100  (sector has a friction thinker; linedef 223 sets it)
//   push      0x0200  (sector has a pusher thinker; linedef 224 sets it)
//
// IMPORTANT: this model applies to specials >= 32 only. Vanilla specials 1..31
// are handled by the engine's vanilla switch, which has its own damage table
// (for example special 5 is 10 damage even though its damage field is 0). The
// engine must not feed a vanilla value into this decoder and then act on the
// decoded damage; see DPT_SecSpecialIsGeneralized.
#ifndef DPT_SECSPECIAL_H
#define DPT_SECSPECIAL_H

#include "doomtype.h"

#define DPT_SEC_BASE_MASK     0x001f
#define DPT_SEC_DAMAGE_MASK   0x0060
#define DPT_SEC_DAMAGE_SHIFT  5
#define DPT_SEC_SECRET_MASK   0x0080
#define DPT_SEC_FRICTION_MASK 0x0100
#define DPT_SEC_PUSH_MASK     0x0200

// Everything BOOM defines for a sector special.
#define DPT_SEC_KNOWN_MASK    0x03ff

typedef struct
{
    int     base;       // 0..31, vanilla light/door/secret type
    int     damage;     // 0..3, see DPT_SecSpecialDamage
    boolean secret;     // 0x0080
    boolean friction;   // 0x0100
    boolean push;       // 0x0200
} dpt_secspecial_t;

// True for the generalized model (>= 32). Below that the engine keeps vanilla
// handling, including the vanilla damage table.
boolean DPT_SecSpecialIsGeneralized (int special);

// False only when bits outside DPT_SEC_KNOWN_MASK are set; BOOM leaves those
// undefined, so they are reported rather than guessed.
boolean DPT_SecSpecialDecode (int special, dpt_secspecial_t* out);

// True when every set bit is implemented. Push needs a pusher thinker plus a
// per-object touching-sector list and water-height tracking, which this engine
// does not have, so it is reported rather than silently ignored. Friction is
// implemented through linedef 223 and the movement code.
boolean DPT_SecSpecialSupported (int special);

// Damage per damage field value: 0, 5, 10, 20.
int DPT_SecSpecialDamage (int damage);

// Damage field 3 (20) still hurts through a radiation suit, with a 5/256
// chance per damage tick; fields 1 and 2 are fully blocked by the suit.
boolean DPT_SecSpecialSuitBlocks (int damage);

#endif
