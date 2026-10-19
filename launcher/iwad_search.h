// Bounded Windows IWAD discovery. GPL-2.0-or-later; see LICENSE.
#ifndef DPT_IWAD_SEARCH_H
#define DPT_IWAD_SEARCH_H
#include "launch_model.h"
#ifdef __cplusplus
extern "C" {
#endif
BOOL LI_Find(launch_profile_t *profile,const wchar_t *launcher_dir,BOOL system_paths);
BOOL LI_SearchSteam(launch_profile_t *profile,const wchar_t *steam_root);
#ifdef __cplusplus
}
#endif
#endif
