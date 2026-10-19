// SDL event regression tests: real backend, no IWAD or visible window.
#include "doomgeneric.h"
#include "doomkeys.h"
#include "d_event.h"
#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static pixel_t pixels[DOOMGENERIC_RESX * DOOMGENERIC_RESY];
pixel_t *DG_ScreenBuffer = pixels;

void I_Error(char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    exit(1);
}
void I_Quit(void) { exit(0); }

#define CHECK(condition) do { if (!(condition)) I_Error("Line %d: %s\n", __LINE__, #condition); } while (0)

static void PushKey(Uint32 type, SDL_Scancode scan, SDL_Keycode code, int repeat)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = type;
    event.key.keysym.scancode = scan;
    event.key.keysym.sym = code;
    event.key.repeat = repeat;
    CHECK(SDL_PushEvent(&event) == 1);
}

static void Expect(int state, unsigned char key)
{
    int pressed;
    int got = 0, i;
    unsigned char actual;
    // SDL 2.0.22+ inserts a poll-cycle sentinel. An event injected after
    // that sentinel becomes visible on the next engine poll/tic.
    for (i = 0; i < 4 && !got; ++i) got = DG_GetKey(&pressed, &actual);
    CHECK(got);
    CHECK(pressed == state && actual == key);
}

static void Empty(void)
{
    int pressed;
    unsigned char key;
    CHECK(!DG_GetKey(&pressed, &key));
}

void I_GetEvent(void);

static void TestEngineQueue(void)
{
    int i, received = 0;
    for (i = 0; i < 200; ++i)
    {
        PushKey(SDL_KEYDOWN, SDL_SCANCODE_A, SDLK_a, 0);
        PushKey(SDL_KEYUP, SDL_SCANCODE_A, SDLK_a, 0);
    }
    for (i = 0; i < 100 && received < 400; ++i)
    {
        int per_tic = 0;
        event_t *event;
        I_GetEvent();
        while ((event = D_PopEvent()) != NULL)
        {
            CHECK(event->type == (received % 2 ? ev_keyup : ev_keydown));
            CHECK(event->data1 == 'a');
            ++received;
            ++per_tic;
        }
        CHECK(per_tic <= 32);
    }
    CHECK(received == 400);
    Empty();
}

int main(int argc, char **argv)
{
    SDL_Event event;
    int i;
    (void)argc; (void)argv;
    DG_Init();
    DG_DrawFrame();
    Empty();
    TestEngineQueue();
    PushKey(SDL_KEYDOWN, SDL_SCANCODE_F1, SDLK_F1, 0);
    PushKey(SDL_KEYUP, SDL_SCANCODE_F1, SDLK_F1, 0);
    PushKey(SDL_KEYDOWN, SDL_SCANCODE_F12, SDLK_F12, 0);
    PushKey(SDL_KEYUP, SDL_SCANCODE_F12, SDLK_F12, 0);
    Expect(1, KEY_F1); Expect(0, KEY_F1);
    Expect(1, KEY_F12); Expect(0, KEY_F12);
    // Unsupported SDL keycodes must not become random 8-bit Doom keys.
    PushKey(SDL_KEYDOWN, SDL_SCANCODE_AUDIOMUTE, SDLK_AUDIOMUTE, 0);
    PushKey(SDL_KEYUP, SDL_SCANCODE_AUDIOMUTE, SDLK_AUDIOMUTE, 0);
    Empty();

    // Larger than both the old backend queue (16) and engine queue (64).
    for (i = 0; i < 200; ++i)
    {
        PushKey(SDL_KEYDOWN, SDL_SCANCODE_A, SDLK_a, 0);
        PushKey(SDL_KEYUP, SDL_SCANCODE_A, SDLK_a, 0);
    }
    for (i = 0; i < 200; ++i) { Expect(1, 'a'); Expect(0, 'a'); }
    Empty();

    PushKey(SDL_KEYDOWN, SDL_SCANCODE_LSHIFT, SDLK_LSHIFT, 0);
    PushKey(SDL_KEYDOWN, SDL_SCANCODE_LSHIFT, SDLK_LSHIFT, 1);
    PushKey(SDL_KEYDOWN, SDL_SCANCODE_RSHIFT, SDLK_RSHIFT, 0);
    PushKey(SDL_KEYUP, SDL_SCANCODE_LSHIFT, SDLK_LSHIFT, 0);
    PushKey(SDL_KEYUP, SDL_SCANCODE_RSHIFT, SDLK_RSHIFT, 0);
    Expect(1, KEY_RSHIFT); Expect(0, KEY_RSHIFT); Empty();

    PushKey(SDL_KEYDOWN, SDL_SCANCODE_A, SDLK_a, 0);
    PushKey(SDL_KEYUP, SDL_SCANCODE_A, SDLK_q, 0); // layout changed
    Expect(1, 'a'); Expect(0, 'a'); Empty();

    PushKey(SDL_KEYDOWN, SDL_SCANCODE_UP, SDLK_UP, 0);
    Expect(1, KEY_UPARROW);
    SDL_zero(event);
    event.type = SDL_WINDOWEVENT;
    event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    CHECK(SDL_PushEvent(&event) == 1);
    Expect(0, KEY_UPARROW);
    PushKey(SDL_KEYUP, SDL_SCANCODE_UP, SDLK_UP, 0);
    Empty();
    puts("SDL input: mapping, engine queue burst, repeat, dual modifiers and focus loss OK");
    event.type = SDL_QUIT;
    CHECK(SDL_PushEvent(&event) == 1);
    for (i = 0; i < 4; ++i)
    {
        int pressed;
        unsigned char key;
        DG_GetKey(&pressed, &key);
    }
    return 1; // SDL_QUIT must use I_Quit (exit 0), not return here.
}
