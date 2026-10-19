// SDL2 platform backend for DPT.DOOM (original DoomGeneric SDL port retained
// in git history). GPL version 2 or, at your option, any later version.

#define WIN32_LEAN_AND_MEAN
#include "doomkeys.h"
#include "doomgeneric.h"
#include "config.h"
#include "i_system.h"

#include <SDL.h>
#ifdef _WIN32
#include <SDL_syswm.h>
#endif

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;

// Consume SDL's event queue directly instead of copying to a tiny ring that
// silently overwrites key-up events. Remember the mapping at key-down time:
// layout changes and left/right modifiers must not leave Doom keys stuck.
static unsigned char held_keys[SDL_NUM_SCANCODES];
static unsigned int key_refs[256];
static int releasing_keys;

static unsigned char ConvertKey(SDL_Keycode key)
{
    switch (key)
    {
        case SDLK_RETURN: case SDLK_KP_ENTER: return KEY_ENTER;
        case SDLK_ESCAPE: return KEY_ESCAPE;
        case SDLK_LEFT: return KEY_LEFTARROW;
        case SDLK_RIGHT: return KEY_RIGHTARROW;
        case SDLK_UP: return KEY_UPARROW;
        case SDLK_DOWN: return KEY_DOWNARROW;
        case SDLK_LCTRL: case SDLK_RCTRL: return KEY_FIRE;
        case SDLK_SPACE: return KEY_USE;
        case SDLK_LSHIFT: case SDLK_RSHIFT: return KEY_RSHIFT;
        case SDLK_LALT: case SDLK_RALT: return KEY_RALT;
        case SDLK_BACKSPACE: return KEY_BACKSPACE;
        case SDLK_TAB: return KEY_TAB;
        case SDLK_PAUSE: return KEY_PAUSE;
        case SDLK_F1: return KEY_F1;
        case SDLK_F2: return KEY_F2;
        case SDLK_F3: return KEY_F3;
        case SDLK_F4: return KEY_F4;
        case SDLK_F5: return KEY_F5;
        case SDLK_F6: return KEY_F6;
        case SDLK_F7: return KEY_F7;
        case SDLK_F8: return KEY_F8;
        case SDLK_F9: return KEY_F9;
        case SDLK_F10: return KEY_F10;
        case SDLK_F11: return KEY_F11;
        case SDLK_F12: return KEY_F12;
        case SDLK_EQUALS: case SDLK_PLUS: return KEY_EQUALS;
        case SDLK_MINUS: return KEY_MINUS;
        default:
            // SDL special keycodes are not ASCII and cannot go to tolower().
            return key >= 32 && key < 127 ? (unsigned char)key : 0;
    }
}

static void ShutdownSDL(void)
{
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    texture = NULL;
    renderer = NULL;
    window = NULL;
    SDL_Quit();
}

void DG_Init(void)
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
        I_Error("SDL_Init: %s", SDL_GetError());
    // C atexit also covers partial startup failures and runs after the
    // engine's I_AtExit audio/config callbacks have finished.
    atexit(ShutdownSDL);
    window = SDL_CreateWindow(PACKAGE_NAME, SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, DOOMGENERIC_RESX, DOOMGENERIC_RESY,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (window == NULL)
        I_Error("SDL_CreateWindow: %s", SDL_GetError());

#ifdef _WIN32
    {
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(window, &info))
        {
            HICON icon = LoadIconA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(1));
            if (icon != NULL)
            {
                SendMessage(info.info.win.window, WM_SETICON, ICON_BIG, (LPARAM)icon);
                SendMessage(info.info.win.window, WM_SETICON, ICON_SMALL, (LPARAM)icon);
            }
        }
    }
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (renderer == NULL)
        I_Error("SDL_CreateRenderer: %s", SDL_GetError());
    if (SDL_RenderSetLogicalSize(renderer, DOOMGENERIC_RESX, DOOMGENERIC_RESY) < 0)
        I_Error("SDL_RenderSetLogicalSize: %s", SDL_GetError());
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888,
        SDL_TEXTUREACCESS_STREAMING, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
    if (texture == NULL)
        I_Error("SDL_CreateTexture: %s", SDL_GetError());
}

void DG_DrawFrame(void)
{
    if (SDL_UpdateTexture(texture, NULL, DG_ScreenBuffer,
            DOOMGENERIC_RESX * sizeof(*DG_ScreenBuffer)) < 0
        || SDL_RenderClear(renderer) < 0
        || SDL_RenderCopy(renderer, texture, NULL, NULL) < 0)
        I_Error("SDL render: %s", SDL_GetError());
    SDL_RenderPresent(renderer);
}

void DG_SleepMs(uint32_t ms) { SDL_Delay(ms); }
uint32_t DG_GetTicksMs(void) { return SDL_GetTicks(); }

int DG_GetKey(int *pressed, unsigned char *doom_key)
{
    SDL_Event event;
    for (;;)
    {
        if (releasing_keys)
        {
            int i;
            for (i = 0; i < SDL_NUM_SCANCODES; ++i)
            {
                unsigned char key = held_keys[i];
                if (key)
                {
                    held_keys[i] = 0;
                    if (--key_refs[key] == 0)
                    {
                        *pressed = 0;
                        *doom_key = key;
                        return 1;
                    }
                }
            }
            releasing_keys = 0;
        }
        if (!SDL_PollEvent(&event)) return 0;
        if (event.type == SDL_QUIT
            || (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE))
        {
            I_Quit();
            return 0;
        }
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            releasing_keys = 1;
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
        {
            SDL_Scancode scan = event.key.keysym.scancode;
            unsigned char key;
            if (scan <= SDL_SCANCODE_UNKNOWN || scan >= SDL_NUM_SCANCODES) continue;
            if (event.type == SDL_KEYDOWN)
            {
                if (event.key.repeat || held_keys[scan]) continue;
                key = ConvertKey(event.key.keysym.sym);
                if (!key) continue;
                held_keys[scan] = key;
                if (key_refs[key]++ != 0) continue;
                *pressed = 1;
            }
            else
            {
                key = held_keys[scan];
                if (!key) continue;
                held_keys[scan] = 0;
                if (--key_refs[key] != 0) continue;
                *pressed = 0;
            }
            *doom_key = key;
            return 1;
        }
    }
}

void DG_SetWindowTitle(const char *title)
{
    if (window) SDL_SetWindowTitle(window, title);
}

#ifndef DPTDOOM_BACKEND_TEST
int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);
    for (;;) doomgeneric_Tick();
}
#endif
