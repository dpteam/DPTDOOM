// Joystick path regression tests: real SDL backend, virtual device, no hardware.
//
// This exists because the whole SDL joystick implementation used to be compiled
// out (#ifdef ORIGCODE, which config.h undefines) and I_UpdateJoystick was never
// called. Both bugs were invisible without a physical gamepad, so this drives a
// virtual one through the real code path.
#include "doomtype.h"
#include "d_event.h"
#include "i_joystick.h"
#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void I_Error(char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    exit(1);
}

static void StubAtExit(void (*func)(void), int on_error)
{
    (void)func;
    (void)on_error;
}

void I_AtExit(void (*func)(void), int on_error)
{
    StubAtExit(func, on_error);
}

// I_BindJoystickVariables binds the real variables by name. Capturing the
// pointers lets the test change axis inversion, which is otherwise file-static.
#define MAX_BINDINGS 64
static struct { const char *name; int *location; } bindings[MAX_BINDINGS];
static int binding_count;

void M_BindVariable(char *name, void *location)
{
    if (binding_count < MAX_BINDINGS)
    {
        bindings[binding_count].name = name;
        bindings[binding_count].location = (int *)location;
        binding_count++;
    }
}

int M_snprintf(char *buf, size_t buflen, const char *fmt, ...)
{
    va_list args;
    int result;
    va_start(args, fmt);
    result = vsnprintf(buf, buflen, fmt, args);
    va_end(args);
    return result;
}

static int *Binding(const char *name)
{
    int i;
    for (i = 0; i < binding_count; ++i)
        if (!strcmp(bindings[i].name, name))
            return bindings[i].location;
    I_Error("binding not found: %s\n", name);
    return NULL;
}

#define CHECK(condition) do { if (!(condition)) I_Error("Line %d: %s\n", __LINE__, #condition); } while (0)

// Virtual axis/button state is cached until SDL refreshes it; in the game this
// happens inside SDL_PollEvent. The test has to ask for it explicitly.
static void Refresh(void)
{
    SDL_JoystickUpdate();
    I_UpdateJoystick();
}

// Pop the most recent joystick event, ignoring anything else.
static boolean PopJoystick(event_t *out)
{
    event_t *ev;
    boolean found = false;
    while ((ev = D_PopEvent()) != NULL)
    {
        if (ev->type == ev_joystick)
        {
            *out = *ev;
            found = true;
        }
    }
    return found;
}

int main(int argc, char **argv)
{
    event_t ev;
    int device;
    (void)argc; (void)argv;

    I_BindJoystickVariables();
    CHECK(binding_count > 0);

    if (SDL_Init(SDL_INIT_JOYSTICK) < 0)
        I_Error("SDL_Init: %s\n", SDL_GetError());

    // 4 axes, 16 buttons, no hats: enough for the default x/y mapping.
    device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 4, 16, 0);
    if (device < 0)
        I_Error("SDL_JoystickAttachVirtual: %s\n", SDL_GetError());

    // Disabled by default: no device is opened and nothing is posted.
    CHECK(!*Binding("use_joystick"));
    I_InitJoystick();
    Refresh();
    CHECK(!PopJoystick(&ev));

    // Enabling opens the virtual device.
    I_SetJoystickEnabled(1);
    CHECK(*Binding("use_joystick"));
    CHECK(I_JoystickCount() >= 1);

    // A stale index from an older config must not disable the gamepad.
    *Binding("joystick_index") = 99;
    I_InitJoystick();

    // Virtual axis state belongs to the device and is cleared when the last
    // handle closes, so the test keeps its own handle open for the whole run.
    {
        SDL_Joystick *js = SDL_JoystickOpen(0);
        CHECK(js != NULL);

        // Axis through the real GetAxisState path.
        SDL_JoystickSetVirtualAxis(js, 0, 32767);
        SDL_JoystickSetVirtualAxis(js, 1, 0);
        Refresh();
        CHECK(PopJoystick(&ev));
        CHECK(ev.data2 == 32767);
        CHECK(ev.data3 == 0);

        // Dead zone: a small deflection reads as zero, not as drift.
        SDL_JoystickSetVirtualAxis(js, 0, 1000);
        Refresh();
        CHECK(PopJoystick(&ev));
        CHECK(ev.data2 == 0);

        // Inversion applies to the reported value.
        SDL_JoystickSetVirtualAxis(js, 0, -32767);
        *Binding("joystick_x_invert") = 1;
        Refresh();
        CHECK(PopJoystick(&ev));
        CHECK(ev.data2 == 32767);
        *Binding("joystick_x_invert") = 0;

        // Buttons reach the engine as a bitmask; the default mapping is 1:1.
        SDL_JoystickSetVirtualButton(js, 0, 1);
        SDL_JoystickSetVirtualButton(js, 2, 1);
        Refresh();
        CHECK(PopJoystick(&ev));
        CHECK((ev.data1 & 1) != 0);
        CHECK((ev.data1 & 4) != 0);
        CHECK((ev.data1 & 2) == 0);

        SDL_JoystickSetVirtualButton(js, 0, 0);
        SDL_JoystickSetVirtualButton(js, 2, 0);
        Refresh();
        CHECK(PopJoystick(&ev));
        CHECK(ev.data1 == 0);

        SDL_JoystickClose(js);
    }

    // Disabling closes the device and stops posting.
    I_SetJoystickEnabled(0);
    CHECK(!*Binding("use_joystick"));
    Refresh();
    CHECK(!PopJoystick(&ev));

    SDL_JoystickDetachVirtual(device);
    SDL_Quit();
    puts("SDL joystick: enable/disable, stale index fallback, axis, dead zone, "
         "inversion and buttons OK");
    return 0;
}
