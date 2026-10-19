// Launcher storage: no silent fallback; imports never replace existing files.
// GPL-2.0-or-later; see LICENSE.
#include "storage.h"
#include <stdio.h>

BOOL LS_Select(const wchar_t *base, const wchar_t *appdata, const wchar_t *override_dir,
               wchar_t *directory, BOOL *portable)
{
    wchar_t marker[LP_PATH];
    DWORD attr;
    *portable = FALSE;
    if (wcslen(base) + 20 >= LP_PATH) return FALSE;
    swprintf(marker, LP_PATH, L"%ls\\portable.txt", base);
    attr = GetFileAttributesW(marker);
    if (attr == INVALID_FILE_ATTRIBUTES && GetLastError() != ERROR_FILE_NOT_FOUND
        && GetLastError() != ERROR_PATH_NOT_FOUND) return FALSE;
    *portable = attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    if (override_dir && *override_dir)
    {
        DWORD n = GetFullPathNameW(override_dir, LP_PATH, directory, NULL);
        return n && n < LP_PATH;
    }
    if (!*portable && (!appdata || !*appdata)) return FALSE;
    if (wcslen(*portable ? base : appdata) + 20 >= LP_PATH) return FALSE;
    swprintf(directory, LP_PATH, L"%ls\\%ls", *portable ? base : appdata, *portable ? L"user" : L"DPTDOOM");
    return TRUE;
}

BOOL LS_Writable(const wchar_t *directory)
{
    wchar_t probe[LP_PATH];
    HANDLE file;
    if (wcslen(directory) + 64 >= LP_PATH) return FALSE;
    if (!CreateDirectoryW(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return FALSE;
    swprintf(probe, LP_PATH, L"%ls\\.write-test-%lu-%llu", directory, GetCurrentProcessId(), GetTickCount64());
    file = CreateFileW(probe, GENERIC_WRITE, 0, NULL, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    CloseHandle(file);
    return TRUE;
}

static BOOL CopyOne(const wchar_t *source, const wchar_t *destination)
{
    if (CopyFileW(source, destination, TRUE)) return TRUE;
    return GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS;
}

static BOOL CopyTree(const wchar_t *source, const wchar_t *destination, unsigned int depth)
{
    WIN32_FIND_DATAW entry;
    wchar_t pattern[LP_PATH], from[LP_PATH], to[LP_PATH];
    HANDLE search;
    BOOL ok = TRUE;
    if (depth > 4 || wcslen(source) + MAX_PATH >= LP_PATH || wcslen(destination) + MAX_PATH >= LP_PATH) return FALSE;
    if (!CreateDirectoryW(destination, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return FALSE;
    swprintf(pattern, LP_PATH, L"%ls\\*", source);
    search = FindFirstFileW(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    do
    {
        if (!wcscmp(entry.cFileName, L".") || !wcscmp(entry.cFileName, L"..")) continue;
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { ok = FALSE; break; }
        swprintf(from, LP_PATH, L"%ls\\%ls", source, entry.cFileName);
        swprintf(to, LP_PATH, L"%ls\\%ls", destination, entry.cFileName);
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok = CopyTree(from, to, depth+1);
        else
        {
            const wchar_t *ext = wcsrchr(entry.cFileName, L'.');
            // Import only settings and saves, never resources, executables or cache.
            if (ext && (!_wcsicmp(ext, L".cfg") || !_wcsicmp(ext, L".dsg"))) ok = CopyOne(from, to);
        }
        if (!ok) break;
    } while (FindNextFileW(search, &entry));
    if (ok && GetLastError() != ERROR_NO_MORE_FILES) ok = FALSE;
    FindClose(search);
    return ok;
}

BOOL LS_Import(const wchar_t *source, const wchar_t *destination)
{
    wchar_t from[LP_PATH], to[LP_PATH];
    DWORD attributes;
    if (wcslen(source) + 32 >= LP_PATH || wcslen(destination) + 32 >= LP_PATH) return FALSE;
    swprintf(from, LP_PATH, L"%ls\\launcher.ini", source);
    swprintf(to, LP_PATH, L"%ls\\launcher.ini", destination);
    attributes = GetFileAttributesW(from);
    if (attributes != INVALID_FILE_ATTRIBUTES && !CopyOne(from, to)) return FALSE;
    swprintf(from, LP_PATH, L"%ls\\game", source);
    swprintf(to, LP_PATH, L"%ls\\game", destination);
    attributes = GetFileAttributesW(from);
    if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return FALSE;
    return CopyTree(from, to, 0);
}
