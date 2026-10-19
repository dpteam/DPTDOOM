// GPL-2.0-or-later; see LICENSE.
#ifndef DPT_LAUNCH_STORAGE_H
#define DPT_LAUNCH_STORAGE_H
#include "launch_model.h"
#ifdef __cplusplus
extern "C" {
#endif
BOOL LS_Select(const wchar_t *base, const wchar_t *appdata, const wchar_t *override_dir,
               wchar_t *directory, BOOL *portable);
BOOL LS_Writable(const wchar_t *directory);
BOOL LS_Import(const wchar_t *source, const wchar_t *destination);
#ifdef __cplusplus
}
#endif
#endif
