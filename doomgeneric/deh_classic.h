// Bounded Misc / Ammo / Text / Weapon / Thing / Frame graph+visual+psprite subset.
// GPL-2.0-or-later; see LICENSE.
#ifndef DPT_DEH_CLASSIC_H
#define DPT_DEH_CLASSIC_H
#include "doomtype.h"
#include "sha1.h"
#include "d_think.h"
#define DPT_DEH_SUBSET "DPT-deh-levelnames-v19"
void DPT_DehReset(void);
boolean DPT_DehLoadLump(int lump);
boolean DPT_DehLoadFile(const char *path);
boolean DPT_DehLoadBytes(const unsigned char *data, size_t size);
// Final loaded-resource validation after R_InitSpriteDefs, before simulation.
boolean DPT_DehValidateSpriteFrames(const int *frame_counts, size_t count);
// Allowlist name of a state action, for diagnostics and tests.
const char *DPT_DehActionName(const actionf_t *action);
void DPT_DehChecksum(sha1_digest_t digest);
unsigned int DPT_DehLoadedCount(void);
const char *DPT_DehLastError(void);
#endif
