// Resolution-independent vertical aim, pure integer math. GPL-2.0-or-later.
// No engine state, no globals: everything here is a function of its arguments,
// so it can be tested without an IWAD or a running engine.
#include "dpt_pitch.h"

// 64-bit intermediates on purpose. slope can be +/-65536 (a full 45 degrees) and
// projection reaches ~4.2e7 at 640 pixels wide, whose product does not fit in a
// 32-bit int. Letting it wrap would make aim depend on the window size in the
// least debuggable way possible.
typedef long long dpt_i64;

int DPT_PitchClamp(int pitch, int limit)
{
    if (limit < 0)
        limit = 0;
    // A config file could ask for any limit at all; beyond the reference width
    // the shear exceeds the view and the horizon would just pin to the edge, so
    // cap it here rather than in each caller.
    if (limit > DPT_PITCH_REFERENCE_HALF_WIDTH)
        limit = DPT_PITCH_REFERENCE_HALF_WIDTH;
    if (pitch > limit)
        return limit;
    if (pitch < -limit)
        return -limit;
    return pitch;
}

fixed_t DPT_PitchSlope(int pitch)
{
    // slope = pitch / reference-half-width, in 16.16.
    return (fixed_t)(((dpt_i64)pitch << FRACBITS) / DPT_PITCH_REFERENCE_HALF_WIDTH);
}

int DPT_PitchShear(int pitch, fixed_t projection)
{
    // shear = pitch * projection / reference-width, in pixels.
    //
    // Computed from the pitch directly rather than from the rounded slope: going
    // through DPT_PitchSlope would truncate twice and the reference width would
    // stop being an exact identity (pitch 3 would render as 2 pixels). At
    // projection == 160<<16 this returns pitch unchanged, which is why the
    // default 320-wide view looks exactly as it did before this module existed.
    dpt_i64 denominator = (dpt_i64)DPT_PITCH_REFERENCE_HALF_WIDTH << FRACBITS;
    return (int)(((dpt_i64)pitch * projection) / denominator);
}

fixed_t DPT_PitchAimTop(fixed_t slope)
{
    return (fixed_t)(slope + (((dpt_i64)DPT_AIM_WINDOW_PIXELS << FRACBITS)
                              / DPT_PITCH_REFERENCE_HALF_WIDTH));
}

fixed_t DPT_PitchAimBottom(fixed_t slope)
{
    return (fixed_t)(slope - (((dpt_i64)DPT_AIM_WINDOW_PIXELS << FRACBITS)
                              / DPT_PITCH_REFERENCE_HALF_WIDTH));
}
