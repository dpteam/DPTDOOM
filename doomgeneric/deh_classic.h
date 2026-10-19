// Bounded Misc / Ammo / Weapon-ammo / Thing-numeric / Frame Duration subset.
// GPL-2.0-or-later; see LICENSE.
#ifndef DPT_DEH_CLASSIC_H
#define DPT_DEH_CLASSIC_H
#include "doomtype.h"
#include "sha1.h"
#define DPT_DEH_SUBSET "DPT-deh-duration-v4"
void DPT_DehReset(void);
boolean DPT_DehLoadLump(int lump);
boolean DPT_DehLoadFile(const char *path);
boolean DPT_DehLoadBytes(const unsigned char *data, size_t size);
void DPT_DehChecksum(sha1_digest_t digest);
unsigned int DPT_DehLoadedCount(void);
const char *DPT_DehLastError(void);
#endif
