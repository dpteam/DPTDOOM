// DPT.DOOM non-interactive backend. No display server or audio libraries.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. Distributed WITHOUT ANY WARRANTY. See LICENSE.

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "doomgeneric.h"
#include "i_system.h"
#include "m_argv.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

static unsigned int frames;
static unsigned int frame_limit;
static const char *dump_path;

static void FinishHeadless(void)
{
    FILE *file;
    size_t i;
    uint32_t hash = 2166136261u;

    // Report an RGB-only FNV-1a checksum, independent of host byte order.
    for (i = 0; i < DOOMGENERIC_RESX * DOOMGENERIC_RESY; ++i)
    {
        uint32_t pixel = DG_ScreenBuffer[i];
        hash = (hash ^ ((pixel >> 16) & 255)) * 16777619u;
        hash = (hash ^ ((pixel >> 8) & 255)) * 16777619u;
        hash = (hash ^ (pixel & 255)) * 16777619u;
    }
    printf("Headless: frames=%u rgb-fnv1a=%08x\n", frames, (unsigned int)hash);

    if (dump_path == NULL || frames == 0)
    {
        return;
    }

    file = fopen(dump_path, "wb");
    if (file == NULL)
    {
        fprintf(stderr, "Headless: cannot open frame dump: %s\n", dump_path);
        exit(EXIT_FAILURE);
    }
    fprintf(file, "P6\n%d %d\n255\n", DOOMGENERIC_RESX, DOOMGENERIC_RESY);
    for (i = 0; i < DOOMGENERIC_RESX * DOOMGENERIC_RESY; ++i)
    {
        uint32_t pixel = DG_ScreenBuffer[i];
        unsigned char rgb[3] = { (pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255 };
        if (fwrite(rgb, sizeof(rgb), 1, file) != 1)
        {
            fclose(file);
            fprintf(stderr, "Headless: failed writing frame dump\n");
            exit(EXIT_FAILURE);
        }
    }
    if (fclose(file) != 0)
    {
        fprintf(stderr, "Headless: failed closing frame dump\n");
        exit(EXIT_FAILURE);
    }
}

void DG_Init(void)
{
    int p;
    char *end;
    unsigned long value;

    // Also keep the initial framebuffer defined for startup-error reports.
    memset(DG_ScreenBuffer, 0, DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(*DG_ScreenBuffer));
    p = M_CheckParm("-headless-frames");
    if (p)
    {
        if (p + 1 >= myargc)
        {
            I_Error("-headless-frames requires a positive integer");
        }
        errno = 0;
        value = strtoul(myargv[p + 1], &end, 10);
        if (errno || *end || end == myargv[p + 1] || myargv[p + 1][0] == '-'
            || value == 0 || value > UINT_MAX)
        {
            I_Error("-headless-frames requires a positive integer");
        }
        frame_limit = (unsigned int)value;
    }
    p = M_CheckParm("-headless-dump");
    if (p)
    {
        if (p + 1 >= myargc || myargv[p + 1][0] == '-')
        {
            I_Error("-headless-dump requires a PPM output path");
        }
        dump_path = myargv[p + 1];
    }
    I_AtExit(FinishHeadless, true);
    puts("Headless: no window, keyboard or audio output");
}

void DG_DrawFrame(void)
{
    ++frames;
    if (frame_limit && frames >= frame_limit)
    {
        I_Quit();
    }
}

void DG_SleepMs(uint32_t ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec delay = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {}
#endif
}

uint32_t DG_GetTicksMs(void)
{
#ifdef _WIN32
    return (uint32_t)GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        I_Error("Headless: clock_gettime failed");
    }
    return (uint32_t)((uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);
#endif
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    (void)pressed;
    (void)key;
    return 0;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

int main(int argc, char **argv)
{
    // Suppress GUI error popups even if the caller forgets -nogui.
    int i;
    char **args = malloc((argc + 3) * sizeof(*args));
    if (args == NULL)
    {
        return EXIT_FAILURE;
    }
    // Keep the two-argument version probe working.
    if (argc == 2 && (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-version")))
    {
        doomgeneric_Create(argc, argv);
    }
    for (i = 0; i < argc; ++i)
    {
        args[i] = argv[i];
    }
    args[argc] = "-nogui";
    args[argc + 1] = "-nosound";
    args[argc + 2] = NULL;
    doomgeneric_Create(argc + 2, args);
    for (;;)
    {
        doomgeneric_Tick();
    }
}
