// Resolution-independent vertical aim, pure integer math. GPL-2.0-or-later.
//
// The view pitch is stored in "nominal pixels": how far the horizon moves in a
// 320x200 view. That keeps the existing option, the menu and mouse sensitivity
// meaningful, while the angle itself stays the same at any render size.
//
// Why this matters: a y-shear of N pixels is a different angle at a different
// view width. Aiming with the shear would therefore make shooting depend on the
// window size - a player could change accuracy by resizing, and two machines at
// different resolutions would disagree. Converting through a slope fixes both,
// because the slope is the angle and the shear is derived from it.
#ifndef DPT_PITCH_H
#define DPT_PITCH_H

#include "m_fixed.h"

// Half the width of the reference view. Nominal pixels are defined against this,
// which is what makes "80 pixels of pitch" a stable angle rather than a stable
// pixel count.
#define DPT_PITCH_REFERENCE_HALF_WIDTH 160

// Vanilla's aim window: +/-100 pixels at the same reference, i.e. +/-100/160.
// Keeping the window centred on the view is what lets autoaim still help, while
// the shot itself follows the crosshair.
#define DPT_AIM_WINDOW_PIXELS 100

// Clamps a pitch to +/-limit. Used by both the renderer and the aim path so the
// two can never disagree about how far the player is allowed to look.
int DPT_PitchClamp(int pitch, int limit);

// Pitch in nominal pixels to a slope (rise over run, 16.16 fixed point).
fixed_t DPT_PitchSlope(int pitch);

// Pitch in nominal pixels to a pixel shear for a given projection (centerxfrac).
// At the reference width this is the identity, so nothing about the default view
// changes; at other widths the angle is preserved instead of the pixel count.
int DPT_PitchShear(int pitch, fixed_t projection);

// Autoaim window edges around a pitched view direction.
fixed_t DPT_PitchAimTop(fixed_t slope);
fixed_t DPT_PitchAimBottom(fixed_t slope);

#endif
