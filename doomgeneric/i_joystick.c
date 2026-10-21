//
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//       SDL Joystick code.
//
// This file used to be wrapped in #ifdef ORIGCODE, and config.h explicitly
// undefines ORIGCODE, so every line of the SDL joystick implementation was
// compiled out. The result was a gamepad that silently did nothing even though
// the engine side (ev_joystick -> joybuttons/joyxmove) was fully wired.
//
// The guard is now DPTDOOM_SDL, which says what it means: the SDL backend has
// joystick support, the Win32 and headless backends do not (they get inert
// stubs so the engine still links and behaves predictably).
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "doomtype.h"
#include "d_event.h"
#include "i_joystick.h"
#include "i_system.h"

#include "m_config.h"
#include "m_misc.h"

#ifdef DPTDOOM_SDL
#include "SDL.h"
#include "SDL_joystick.h"
#endif

// When an axis is within the dead zone, it is set to zero.
// This is 5% of the full range:

#define DEAD_ZONE (32768 / 3)

// Configuration variables. These exist for every backend so the config file
// keeps the same keys everywhere; only the SDL backend acts on them.

// Standard default.cfg Joystick enable/disable. Default is off: silently
// grabbing an input device is worse than a visible, discoverable switch, so a
// gamepad is only used after the player enables it (menu or use_joystick 1).

int usejoystick = 0;

// Joystick to use, as an SDL joystick index. -1 means "pick the first one".

static int joystick_index = -1;

// Which joystick axis to use for horizontal movement, and whether to
// invert the direction:

static int joystick_x_axis = 0;
static int joystick_x_invert = 0;

// Which joystick axis to use for vertical movement, and whether to
// invert the direction:

static int joystick_y_axis = 1;
static int joystick_y_invert = 0;

// Which joystick axis to use for strafing?

static int joystick_strafe_axis = -1;
static int joystick_strafe_invert = 0;

// Virtual to physical button joystick button mapping. By default this
// is a straight mapping.
static int joystick_physical_buttons[NUM_VIRTUAL_BUTTONS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9
};

#ifdef DPTDOOM_SDL

static SDL_Joystick *joystick = NULL;

// I_AtExit must be registered exactly once, even though the menu can toggle the
// device open and closed repeatedly.
static boolean joystick_atexit_registered = false;

int I_JoystickCount(void)
{
    // The joystick subsystem is only brought up when the player enables the
    // device, so querying it has to initialise it first. That is cheap and does
    // not open a device.
    if (SDL_WasInit(SDL_INIT_JOYSTICK) == 0)
    {
        if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) < 0)
        {
            return 0;
        }
    }

    return SDL_NumJoysticks();
}

void I_ShutdownJoystick(void)
{
    if (joystick != NULL)
    {
        SDL_JoystickClose(joystick);
        joystick = NULL;
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    }
}

static boolean IsValidAxis(int axis)
{
    int num_axes;

    if (axis < 0)
    {
        return true;
    }

    if (IS_BUTTON_AXIS(axis))
    {
        return true;
    }

    if (IS_HAT_AXIS(axis))
    {
        return HAT_AXIS_HAT(axis) < SDL_JoystickNumHats(joystick);
    }

    num_axes = SDL_JoystickNumAxes(joystick);

    return axis < num_axes;
}

void I_InitJoystick(void)
{
    // Re-entrant: the menu toggle can call this while a device is already open.
    I_ShutdownJoystick();

    if (!usejoystick)
    {
        return;
    }

    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) < 0)
    {
        return;
    }

    // No joystick connected is a normal state, not an error: stay quiet instead
    // of printing a failure on every start for players without a gamepad.
    if (SDL_NumJoysticks() <= 0)
    {
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
        return;
    }

    // -1 means "first available". An explicit index that no longer exists falls
    // back to 0 rather than refusing to start: device order changes between
    // sessions, and a stale index in the config should not disable the gamepad.
    if (joystick_index < 0 || joystick_index >= SDL_NumJoysticks())
    {
        joystick_index = 0;
    }

    // Open the joystick

    joystick = SDL_JoystickOpen(joystick_index);

    if (joystick == NULL)
    {
        printf("I_InitJoystick: Failed to open joystick #%i\n",
               joystick_index);
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
        return;
    }

    if (!IsValidAxis(joystick_x_axis)
     || !IsValidAxis(joystick_y_axis)
     || !IsValidAxis(joystick_strafe_axis))
    {
        printf("I_InitJoystick: Invalid joystick axis for joystick #%i "
               "(run joystick setup again)\n",
               joystick_index);

        SDL_JoystickClose(joystick);
        joystick = NULL;
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
        return;
    }

    SDL_JoystickEventState(SDL_ENABLE);

    // Initialized okay!

    printf("I_InitJoystick: %s\n",
           SDL_JoystickName(joystick_index) != NULL
               ? SDL_JoystickName(joystick_index) : "unnamed device");

    if (!joystick_atexit_registered)
    {
        I_AtExit(I_ShutdownJoystick, true);
        joystick_atexit_registered = true;
    }
}

static boolean IsAxisButton(int physbutton)
{
    if (IS_BUTTON_AXIS(joystick_x_axis))
    {
        if (physbutton == BUTTON_AXIS_NEG(joystick_x_axis)
         || physbutton == BUTTON_AXIS_POS(joystick_x_axis))
        {
            return true;
        }
    }
    if (IS_BUTTON_AXIS(joystick_y_axis))
    {
        if (physbutton == BUTTON_AXIS_NEG(joystick_y_axis)
         || physbutton == BUTTON_AXIS_POS(joystick_y_axis))
        {
            return true;
        }
    }
    if (IS_BUTTON_AXIS(joystick_strafe_axis))
    {
        if (physbutton == BUTTON_AXIS_NEG(joystick_strafe_axis)
         || physbutton == BUTTON_AXIS_POS(joystick_strafe_axis))
        {
            return true;
        }
    }

    return false;
}

// Get the state of the given virtual button.

static int ReadButtonState(int vbutton)
{
    int physbutton;

    // Map from virtual button to physical (SDL) button.
    if (vbutton < NUM_VIRTUAL_BUTTONS)
    {
        physbutton = joystick_physical_buttons[vbutton];
    }
    else
    {
        physbutton = vbutton;
    }

    // Never read axis buttons as buttons.
    if (IsAxisButton(physbutton))
    {
        return 0;
    }

    return SDL_JoystickGetButton(joystick, physbutton);
}

// Get a bitmask of all currently-pressed buttons

static int GetButtonsState(void)
{
    int i;
    int result;

    result = 0;

    for (i = 0; i < 20; ++i)
    {
        if (ReadButtonState(i))
        {
            result |= 1 << i;
        }
    }

    return result;
}

// Read the state of an axis, inverting if necessary.

static int GetAxisState(int axis, int invert)
{
    int result;

    // Axis -1 means disabled.

    if (axis < 0)
    {
        return 0;
    }

    // Is this a button axis, or a hat axis?
    // If so, we need to handle it specially.

    result = 0;

    if (IS_BUTTON_AXIS(axis))
    {
        if (SDL_JoystickGetButton(joystick, BUTTON_AXIS_NEG(axis)))
        {
            result -= 32767;
        }
        if (SDL_JoystickGetButton(joystick, BUTTON_AXIS_POS(axis)))
        {
            result += 32767;
        }
    }
    else if (IS_HAT_AXIS(axis))
    {
        int direction = HAT_AXIS_DIRECTION(axis);
        int hatval = SDL_JoystickGetHat(joystick, HAT_AXIS_HAT(axis));

        if (direction == HAT_AXIS_HORIZONTAL)
        {
            if ((hatval & SDL_HAT_LEFT) != 0)
            {
                result -= 32767;
            }
            else if ((hatval & SDL_HAT_RIGHT) != 0)
            {
                result += 32767;
            }
        }
        else if (direction == HAT_AXIS_VERTICAL)
        {
            if ((hatval & SDL_HAT_UP) != 0)
            {
                result -= 32767;
            }
            else if ((hatval & SDL_HAT_DOWN) != 0)
            {
                result += 32767;
            }
        }
    }
    else
    {
        result = SDL_JoystickGetAxis(joystick, axis);

        if (result < DEAD_ZONE && result > -DEAD_ZONE)
        {
            result = 0;
        }
    }

    if (invert)
    {
        result = -result;
    }

    return result;
}

void I_UpdateJoystick(void)
{
    // Called once per tic. Posting unconditionally is intentional: the engine
    // stores the latest axis values and reads buttons for edge detection, so a
    // steady state still has to be reported. When no device is open this is a
    // single branch.
    if (joystick != NULL)
    {
        event_t ev;

        ev.type = ev_joystick;
        ev.data1 = GetButtonsState();
        ev.data2 = GetAxisState(joystick_x_axis, joystick_x_invert);
        ev.data3 = GetAxisState(joystick_y_axis, joystick_y_invert);
        ev.data4 = GetAxisState(joystick_strafe_axis, joystick_strafe_invert);

        D_PostEvent(&ev);
    }
}

#else // !DPTDOOM_SDL

// Backends without SDL have no joystick input. The stubs keep the engine's
// call sites meaningful instead of missing symbols, and report zero devices so
// the menu shows the feature as unavailable rather than broken.

int I_JoystickCount(void)
{
    return 0;
}

void I_ShutdownJoystick(void)
{
}

void I_InitJoystick(void)
{
}

void I_UpdateJoystick(void)
{
}

#endif // DPTDOOM_SDL

void I_SetJoystickEnabled(int enabled)
{
    usejoystick = enabled ? 1 : 0;
    I_InitJoystick();
}

void I_BindJoystickVariables(void)
{
    int i;

    M_BindVariable("use_joystick",          &usejoystick);
    M_BindVariable("joystick_index",        &joystick_index);
    M_BindVariable("joystick_x_axis",       &joystick_x_axis);
    M_BindVariable("joystick_y_axis",       &joystick_y_axis);
    M_BindVariable("joystick_strafe_axis",  &joystick_strafe_axis);
    M_BindVariable("joystick_x_invert",     &joystick_x_invert);
    M_BindVariable("joystick_y_invert",     &joystick_y_invert);
    M_BindVariable("joystick_strafe_invert",&joystick_strafe_invert);

    for (i = 0; i < NUM_VIRTUAL_BUTTONS; ++i)
    {
        char name[32];
        M_snprintf(name, sizeof(name), "joystick_physical_button%i", i);
        M_BindVariable(name, &joystick_physical_buttons[i]);
    }
}
