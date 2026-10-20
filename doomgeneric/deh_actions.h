// DPT.DOOM action allowlist for BEX [CODEPTR].
// GPL-2.0-or-later; see LICENSE.
//
// Only existing DPT action functions are addressable, never an arbitrary
// offset or a caller-supplied pointer. The second field is the calling
// convention: 1 = psprite (player_t*, pspdef_t*), 0 = mobj (mobj_t*).
// P_SetMobjState calls action.acp1(mobj) and P_SetPsprite calls
// action.acp2(player, psp), so mixing the classes would reinterpret a
// pspdef_t as a mobj_t. The parser refuses such an assignment.
//
// The same X-macro drives the engine table, the declarations and the
// data-free unit-test stub, so names and classes cannot drift apart.
#ifndef DPT_DEH_ACTIONS_H
#define DPT_DEH_ACTIONS_H

#include "doomtype.h"
#include "d_think.h"

#define DPT_ACTION_LIST(X) \
    X(A_Light0,1) X(A_WeaponReady,1) X(A_Lower,1) X(A_Raise,1) \
    X(A_Punch,1) X(A_ReFire,1) X(A_FirePistol,1) X(A_Light1,1) \
    X(A_FireShotgun,1) X(A_Light2,1) X(A_FireShotgun2,1) \
    X(A_CheckReload,1) X(A_OpenShotgun2,1) X(A_LoadShotgun2,1) \
    X(A_CloseShotgun2,1) X(A_FireCGun,1) X(A_GunFlash,1) \
    X(A_FireMissile,1) X(A_Saw,1) X(A_FirePlasma,1) X(A_BFGsound,1) \
    X(A_FireBFG,1) \
    X(A_BFGSpray,0) X(A_Explode,0) X(A_Pain,0) X(A_PlayerScream,0) \
    X(A_Fall,0) X(A_XScream,0) X(A_Look,0) X(A_Chase,0) \
    X(A_FaceTarget,0) X(A_PosAttack,0) X(A_Scream,0) X(A_SPosAttack,0) \
    X(A_VileChase,0) X(A_VileStart,0) X(A_VileTarget,0) X(A_VileAttack,0) \
    X(A_StartFire,0) X(A_Fire,0) X(A_FireCrackle,0) X(A_Tracer,0) \
    X(A_SkelWhoosh,0) X(A_SkelFist,0) X(A_SkelMissile,0) X(A_FatRaise,0) \
    X(A_FatAttack1,0) X(A_FatAttack2,0) X(A_FatAttack3,0) \
    X(A_BossDeath,0) X(A_CPosAttack,0) X(A_CPosRefire,0) \
    X(A_TroopAttack,0) X(A_SargAttack,0) X(A_HeadAttack,0) \
    X(A_BruisAttack,0) X(A_SkullAttack,0) X(A_Metal,0) X(A_SpidRefire,0) \
    X(A_BabyMetal,0) X(A_BspiAttack,0) X(A_Hoof,0) X(A_CyberAttack,0) \
    X(A_PainAttack,0) X(A_PainDie,0) X(A_KeenDie,0) X(A_BrainPain,0) \
    X(A_BrainScream,0) X(A_BrainDie,0) X(A_BrainAwake,0) X(A_BrainSpit,0) \
    X(A_SpawnSound,0) X(A_SpawnFly,0) X(A_BrainExplode,0)

typedef struct
{
    const char *name;
    actionf_t action;
    boolean psprite;
} dpt_action_entry_t;

extern const dpt_action_entry_t dpt_action_entries[];
extern const unsigned int dpt_action_count;

#endif
