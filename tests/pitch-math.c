// Data-free checks for the vertical aim math. No IWAD, no engine, no sockets.
// GPL version 2 or, at your option, any later version. See LICENSE.
#include "dpt_pitch.h"
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

// The projection the renderer uses at a given view width: centerxfrac.
static fixed_t Projection(int viewwidth)
{
    return (viewwidth / 2) << FRACBITS;
}

int main(void)
{
    int pitch;

    // Zero pitch is level, and a half-width pitch is exactly 45 degrees. These
    // are the anchors the rest of the file is measured against.
    REQUIRE(DPT_PitchSlope(0) == 0);
    REQUIRE(DPT_PitchSlope(DPT_PITCH_REFERENCE_HALF_WIDTH) == FRACUNIT);
    REQUIRE(DPT_PitchSlope(-DPT_PITCH_REFERENCE_HALF_WIDTH) == -FRACUNIT);
    REQUIRE(DPT_PitchSlope(80) == FRACUNIT / 2);

    // At the reference width the shear is the pitch in pixels, unchanged. This
    // is the guarantee that the default 320x200 view looks exactly as it did
    // before the slope conversion existed: no silent visual change.
    for (pitch = -160; pitch <= 160; ++pitch)
        REQUIRE(DPT_PitchShear(pitch, Projection(320)) == pitch);

    // Twice the width, same angle: the shear doubles in pixels because each
    // pixel is now half the angle. Without this, a wide window would silently
    // reduce how far the player looks and, worse, stop matching the aim.
    for (pitch = -80; pitch <= 80; ++pitch)
    {
        REQUIRE(DPT_PitchShear(pitch, Projection(640)) == pitch * 2);
        REQUIRE(DPT_PitchShear(pitch, Projection(160)) == pitch / 2);
    }

    // Slope is monotone in pitch: no wrap-around or sign flip anywhere in the
    // allowed range, which a 32-bit intermediate would have introduced.
    for (pitch = -160; pitch < 160; ++pitch)
        REQUIRE(DPT_PitchSlope(pitch) <= DPT_PitchSlope(pitch + 1));

    // The aim window at zero pitch must be exactly vanilla's +/-100/160, or
    // turning the feature on would change autoaim for a level view.
    REQUIRE(DPT_PitchAimTop(0) == 100 * FRACUNIT / 160);
    REQUIRE(DPT_PitchAimBottom(0) == -(100 * FRACUNIT / 160));
    // And it must follow the view rather than stay level.
    REQUIRE(DPT_PitchAimTop(DPT_PitchSlope(80)) > DPT_PitchAimTop(0));
    REQUIRE(DPT_PitchAimBottom(DPT_PitchSlope(80)) > DPT_PitchAimBottom(0));
    // Symmetry: looking down mirrors looking up.
    for (pitch = 0; pitch <= 160; ++pitch)
    {
        fixed_t up = DPT_PitchSlope(pitch), down = DPT_PitchSlope(-pitch);
        REQUIRE(DPT_PitchAimTop(up) == -DPT_PitchAimBottom(down));
    }
    // The window keeps its width wherever the view points: a 0.625-slope band.
    for (pitch = -160; pitch <= 160; pitch += 16)
    {
        fixed_t slope = DPT_PitchSlope(pitch);
        REQUIRE(DPT_PitchAimTop(slope) - DPT_PitchAimBottom(slope)
                == 2 * (100 * FRACUNIT / 160));
    }

    // Clamping: both directions, and a limit beyond the reference width is
    // capped rather than producing a shear larger than the view itself.
    REQUIRE(DPT_PitchClamp(50, 80) == 50);
    REQUIRE(DPT_PitchClamp(-50, 80) == -50);
    REQUIRE(DPT_PitchClamp(120, 80) == 80);
    REQUIRE(DPT_PitchClamp(-120, 80) == -80);
    REQUIRE(DPT_PitchClamp(200, 400) == 160);
    REQUIRE(DPT_PitchClamp(-200, 400) == -160);
    REQUIRE(DPT_PitchClamp(120, 400) == 120);
    REQUIRE(DPT_PitchClamp(120, 0) == 0);
    REQUIRE(DPT_PitchClamp(-120, 0) == 0);
    // A negative limit is nonsense from a hand-edited config; treat it as zero
    // instead of inverting the comparison and letting everything through.
    REQUIRE(DPT_PitchClamp(120, -80) == 0);

    puts("Pitch math: slope, resolution-independent shear, aim window, clamp OK");
    return 0;
}
