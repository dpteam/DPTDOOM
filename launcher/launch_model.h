// DPT.DOOM native launcher model. GPL-2.0-or-later; see LICENSE.
#ifndef DPT_LAUNCH_MODEL_H
#define DPT_LAUNCH_MODEL_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include <string.h>

#define LP_PATH 2048
#define LP_MODS 16
#define LP_COMMAND 32768
typedef struct
{
    wchar_t executable[LP_PATH], iwad[LP_PATH];
    wchar_t mods[LP_MODS][LP_PATH];
    unsigned int mod_count;
    int mode; // 0 SP, 1 listen host, 2 join
    int commercial; // derived from IWAD directory (MAP01 vs E1M1)
    unsigned int map, episode, skill, port, dmflags;
    int lan, nomonsters, fast, respawn;
    wchar_t address[64];
} launch_profile_t;

#ifdef __cplusplus
extern "C" {
#endif
void LP_Defaults(launch_profile_t *profile);
BOOL LP_Number(const wchar_t *text, unsigned int low, unsigned int high, unsigned int *result);
BOOL LP_IPv4(const wchar_t *text);
BOOL LP_QuoteAppend(wchar_t *command, size_t capacity, const wchar_t *argument);
BOOL LP_Build(const launch_profile_t *profile, wchar_t *command, size_t capacity, wchar_t *error, size_t error_size);
BOOL LP_ValidateFiles(launch_profile_t *profile, wchar_t *error, size_t error_size);
BOOL LP_Load(const wchar_t *ini, int slot, launch_profile_t *profile);
BOOL LP_Save(const wchar_t *ini, int slot, const launch_profile_t *profile);
#ifdef __cplusplus
}
#endif
#endif
