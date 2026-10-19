// DPT.DOOM native launcher model. GPL-2.0-or-later; see LICENSE.
#include "launch_model.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static BOOL Fail(wchar_t *error, size_t count, const wchar_t *message)
{
    if (count) { wcsncpy(error, message, count - 1); error[count - 1] = 0; }
    return FALSE;
}

void LP_Defaults(launch_profile_t *p)
{
    memset(p, 0, sizeof(*p));
    p->commercial = 1;
    p->map = p->episode = 1; p->skill = 3; p->port = 2342;
    wcscpy(p->address, L"127.0.0.1");
}

BOOL LP_Number(const wchar_t *text, unsigned int low, unsigned int high, unsigned int *result)
{
    unsigned int value = 0;
    if (!*text) return FALSE;
    while (*text)
    {
        if (*text < L'0' || *text > L'9' || value > high / 10) return FALSE;
        value = value * 10 + (unsigned int)(*text++ - L'0');
        if (value > high) return FALSE;
    }
    if (value < low) return FALSE;
    *result = value;
    return TRUE;
}

BOOL LP_IPv4(const wchar_t *text)
{
    int part;
    for (part = 0; part < 4; ++part)
    {
        unsigned int value = 0, digits = 0;
        while (*text >= L'0' && *text <= L'9')
        {
            value = value * 10 + (unsigned int)(*text++ - L'0');
            if (++digits > 3 || value > 255) return FALSE;
        }
        if (!digits) return FALSE;
        if (part < 3) { if (*text++ != L'.') return FALSE; }
    }
    return *text == 0;
}

static BOOL Put(wchar_t *buffer, size_t capacity, size_t *length, wchar_t ch)
{
    if (*length + 1 >= capacity) return FALSE;
    buffer[(*length)++] = ch; buffer[*length] = 0;
    return TRUE;
}

BOOL LP_QuoteAppend(wchar_t *buffer, size_t capacity, const wchar_t *argument)
{
    size_t length = wcslen(buffer);
    const wchar_t *p = argument;
    if (length && !Put(buffer, capacity, &length, L' ')) return FALSE;
    if (!Put(buffer, capacity, &length, L'"')) return FALSE;
    // CRT-compatible Windows argument quoting. Backslashes before a quote
    // or the closing delimiter must be doubled; shell syntax is never used.
    for (;;)
    {
        size_t slashes = 0, i;
        while (*p == L'\\') { ++slashes; ++p; }
        if (!*p || *p == L'"') slashes *= 2;
        for (i = 0; i < slashes; ++i) if (!Put(buffer, capacity, &length, L'\\')) return FALSE;
        if (!*p) break;
        if (*p == L'"' && !Put(buffer, capacity, &length, L'\\')) return FALSE;
        if (!Put(buffer, capacity, &length, *p++)) return FALSE;
    }
    return Put(buffer, capacity, &length, L'"');
}

BOOL LP_Build(const launch_profile_t *p, wchar_t *command, size_t capacity, wchar_t *error, size_t error_size)
{
    wchar_t value[96];
    unsigned int i;
    if (!capacity) return Fail(error, error_size, L"Command buffer is empty.");
    command[0] = 0;
    if (p->mode < 0 || p->mode > 2 || p->mod_count > LP_MODS || p->map < 1
        || p->map > (p->commercial ? 32u : 9u) || p->episode < 1 || p->episode > 4
        || p->skill < 1 || p->skill > 5 || p->port < 1 || p->port > 65535 || p->dmflags > 3)
        return Fail(error, error_size, L"Invalid mode/map/episode/skill/port/rules.");
    if (!p->executable[0] || !p->iwad[0]) return Fail(error, error_size, L"Select dptdoom.exe and a legal IWAD first.");
    if (p->mode == 2 && !LP_IPv4(p->address)) return Fail(error, error_size, L"Join address must be a numeric IPv4 address, without a port.");
#define ARG(text) do { if (!LP_QuoteAppend(command, capacity, (text))) return Fail(error, error_size, L"Command is too long."); } while (0)
#define NUM(n) do { swprintf(value, 96, L"%u", (unsigned int)(n)); ARG(value); } while (0)
    ARG(p->executable); ARG(L"-iwad"); ARG(p->iwad);
    if (p->mod_count)
    {
        ARG(L"-file");
        for (i = 0; i < p->mod_count; ++i) ARG(p->mods[i]);
    }
    ARG(L"-warp");
    if (!p->commercial) NUM(p->episode);
    NUM(p->map);
    ARG(L"-skill"); NUM(p->skill);
    if (p->nomonsters) ARG(L"-nomonsters");
    if (p->fast) ARG(L"-fast");
    if (p->respawn) ARG(L"-respawn");
    ARG(L"-nogui"); // errors go to the session log, not hidden dialogs
    if (p->mode)
    {
        ARG(L"-dmflags"); NUM(p->dmflags);
        ARG(L"-net-timeout"); ARG(L"30000");
        if (p->mode == 1)
        {
            ARG(L"-net-host"); NUM(p->port);
            if (p->lan) { ARG(L"-net-bind"); ARG(L"0.0.0.0"); }
        }
        else
        {
            ARG(L"-net-join"); swprintf(value, 96, L"%ls:%u", p->address, p->port); ARG(value);
        }
    }
#undef NUM
#undef ARG
    if (error_size) error[0] = 0;
    return TRUE;
}

static uint32_t LE32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static BOOL EnginePath(const wchar_t *path)
{
    BOOL substituted = FALSE;
    char ansi[LP_PATH * 4];
    DWORD attributes;
    if (!*path || wcslen(path) >= MAX_PATH) return FALSE;
    // The engine currently uses main(char**) / stdio fopen, not Unicode paths.
    if (GetACP() == CP_UTF8)
    {
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, ansi, sizeof(ansi), NULL, NULL)) return FALSE;
    }
    else if (!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, path, -1, ansi, sizeof(ansi), NULL, &substituted) || substituted)
        return FALSE;
    attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL CheckWad(const wchar_t *path, BOOL iwad, int *commercial, BOOL *map_found,
                     unsigned int episode, unsigned int map)
{
    HANDLE file;
    LARGE_INTEGER size;
    unsigned char header[12], entry[16];
    DWORD got;
    uint32_t count, offset, i;
    BOOL ok = FALSE;
    char target[9];
    *map_found = FALSE;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 12 || size.QuadPart > INT32_MAX
        || !ReadFile(file, header, 12, &got, NULL) || got != 12
        || memcmp(header, iwad ? "IWAD" : "PWAD", 4)) goto done;
    count = LE32(header + 4); offset = LE32(header + 8);
    if (!count || count > 1000000 || offset < 12 || (uint64_t)offset + (uint64_t)count * 16 > (uint64_t)size.QuadPart) goto done;
    { LARGE_INTEGER seek; seek.QuadPart = offset; if (!SetFilePointerEx(file, seek, NULL, FILE_BEGIN)) goto done; }
    *commercial = 0;
    // First scan identifies Doom's map naming, then the requested map.
    for (i = 0; i < count; ++i)
    {
        if (!ReadFile(file, entry, 16, &got, NULL) || got != 16
            || (uint64_t)LE32(entry) + LE32(entry + 4) > (uint64_t)size.QuadPart) goto done;
        if (!memcmp(entry + 8, "MAP01\0\0\0", 8)) *commercial = 1;
    }
    if (*commercial) snprintf(target, sizeof(target), "MAP%02u", map);
    else snprintf(target, sizeof(target), "E%uM%u", episode, map);
    { LARGE_INTEGER seek; seek.QuadPart = offset; if (!SetFilePointerEx(file, seek, NULL, FILE_BEGIN)) goto done; }
    for (i = 0; i < count; ++i)
    {
        char name[9];
        if (!ReadFile(file, entry, 16, &got, NULL) || got != 16) goto done;
        memcpy(name, entry + 8, 8); name[8] = 0;
        if (!strcmp(name, target)) *map_found = TRUE;
    }
    ok = TRUE;
done:
    CloseHandle(file);
    return ok;
}

BOOL LP_ValidateFiles(launch_profile_t *p, wchar_t *error, size_t error_size)
{
    unsigned int i;
    BOOL found;
    int naming;
    wchar_t full[LP_PATH];
    DWORD length;
    // Resolve relative UI entries before passing a different working directory
    // to CreateProcess. Paths with quotes are impossible Windows filenames.
#define FULLPATH(path) do { length = GetFullPathNameW(path, LP_PATH, full, NULL); if (!length || length >= LP_PATH || wcschr(full, L'"')) return Fail(error, error_size, L"Invalid or oversized resource path."); wcscpy(path, full); } while (0)
    if (!p->executable[0] || !p->iwad[0]) return Fail(error, error_size, L"Select EXE and IWAD first.");
    FULLPATH(p->executable); FULLPATH(p->iwad);
    { const wchar_t *extension = wcsrchr(p->executable, L'.'); if (!extension || _wcsicmp(extension, L".exe")) return Fail(error, error_size, L"Game executable must be an .exe file."); }
    { const wchar_t *extension = wcsrchr(p->iwad, L'.'); if (!extension || _wcsicmp(extension, L".wad")) return Fail(error, error_size, L"IWAD must use the .wad extension."); }
    if (!EnginePath(p->executable) || !EnginePath(p->iwad))
        return Fail(error, error_size, L"EXE/IWAD missing, too long, or not representable in the engine's ANSI code page. Use a shorter supported path.");
    if (!CheckWad(p->iwad, TRUE, &naming, &found, p->episode, p->map))
        return Fail(error, error_size, L"Invalid/unsupported IWAD header, size or directory.");
    p->commercial = naming;
    if (!found) return Fail(error, error_size, L"Requested map is not present in this IWAD. Choose a valid episode/map.");
    if (p->mod_count > LP_MODS) return Fail(error, error_size, L"Too many PWADs (maximum 16).");
    for (i = 0; i < p->mod_count; ++i)
    {
        unsigned int j;
        const wchar_t *extension;
        if (!p->mods[i][0]) return Fail(error, error_size, L"Empty PWAD entry.");
        FULLPATH(p->mods[i]);
        extension = wcsrchr(p->mods[i], L'.');
        if (!extension || _wcsicmp(extension, L".wad")) return Fail(error, error_size, L"Only .wad PWAD entries are supported.");
        if (!EnginePath(p->mods[i]) || !CheckWad(p->mods[i], FALSE, &naming, &found, 1, 1))
            return Fail(error, error_size, L"Invalid/missing PWAD or unsupported path. Only plain .wad resources are supported, not PK3/ZIP.");
        if (!_wcsicmp(p->mods[i], p->iwad)) return Fail(error, error_size, L"IWAD cannot also be a PWAD.");
        for (j = 0; j < i; ++j)
            if (!_wcsicmp(p->mods[i], p->mods[j])) return Fail(error, error_size, L"Duplicate PWAD in profile.");
    }
#undef FULLPATH
    return TRUE;
}

static void Section(int slot, wchar_t *text) { swprintf(text, 32, L"Profile%d", slot); }
static void LoadText(const wchar_t *ini, const wchar_t *section, const wchar_t *key, wchar_t *text, DWORD size)
{
    GetPrivateProfileStringW(section, key, L"", text, size, ini);
}

BOOL LP_Load(const wchar_t *ini, int slot, launch_profile_t *p)
{
    wchar_t section[32], key[32];
    unsigned int i;
    LP_Defaults(p);
    if (slot < 1 || slot > 3) return FALSE;
    Section(slot, section);
    LoadText(ini, section, L"Executable", p->executable, LP_PATH);
    LoadText(ini, section, L"IWAD", p->iwad, LP_PATH);
    LoadText(ini, section, L"Address", p->address, 64);
    if (!p->address[0]) wcscpy(p->address, L"127.0.0.1");
#define LOAD(field, keyname, fallback) p->field = GetPrivateProfileIntW(section, keyname, fallback, ini)
    LOAD(mode, L"Mode", 0); LOAD(map, L"Map", 1); LOAD(episode, L"Episode", 1);
    LOAD(skill, L"Skill", 3); LOAD(port, L"Port", 2342); LOAD(dmflags, L"DMFlags", 0);
    LOAD(lan, L"LAN", 0); LOAD(nomonsters, L"NoMonsters", 0); LOAD(fast, L"Fast", 0); LOAD(respawn, L"Respawn", 0);
    LOAD(mod_count, L"ModCount", 0);
#undef LOAD
    if (p->mode < 0 || p->mode > 2) p->mode = 0;
    if (p->mod_count > LP_MODS) p->mod_count = 0;
    for (i = 0; i < p->mod_count; ++i)
    {
        swprintf(key, 32, L"Mod%u", i);
        LoadText(ini, section, key, p->mods[i], LP_PATH);
    }
    return TRUE;
}

BOOL LP_Save(const wchar_t *ini, int slot, const launch_profile_t *p)
{
    wchar_t section[32], key[32], value[64];
    unsigned int i;
    if (slot < 1 || slot > 3 || p->mod_count > LP_MODS) return FALSE;
    // INI APIs otherwise create an ANSI file, losing Unicode-only resource
    // paths before validation. A BOM makes the existing Win32 API use UTF-16.
    {
        HANDLE file = CreateFileW(ini, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE)
        {
            const unsigned char bom[2] = {0xff, 0xfe}; DWORD written;
            BOOL ok = WriteFile(file, bom, 2, &written, NULL) && written == 2;
            CloseHandle(file); if (!ok) return FALSE;
        }
        else if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) return FALSE;
    }
    Section(slot, section);
#define WRITE_TEXT(keyname, text) do { if (!WritePrivateProfileStringW(section, keyname, text, ini)) return FALSE; } while (0)
#define SAVE(field, keyname) do { swprintf(value, 64, L"%u", (unsigned int)p->field); WRITE_TEXT(keyname, value); } while (0)
    WRITE_TEXT(L"Executable", p->executable); WRITE_TEXT(L"IWAD", p->iwad); WRITE_TEXT(L"Address", p->address);
    SAVE(mode, L"Mode"); SAVE(map, L"Map"); SAVE(episode, L"Episode"); SAVE(skill, L"Skill");
    SAVE(port, L"Port"); SAVE(dmflags, L"DMFlags"); SAVE(lan, L"LAN"); SAVE(nomonsters, L"NoMonsters");
    SAVE(fast, L"Fast"); SAVE(respawn, L"Respawn"); SAVE(mod_count, L"ModCount");
    for (i = 0; i < LP_MODS; ++i)
    {
        swprintf(key, 32, L"Mod%u", i);
        WRITE_TEXT(key, i < p->mod_count ? p->mods[i] : NULL);
    }
#undef SAVE
#undef WRITE_TEXT
    // This special cache-flush call returns zero even after successful writes.
    WritePrivateProfileStringW(NULL, NULL, NULL, ini);
    return TRUE;
}
