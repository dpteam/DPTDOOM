// Boom generalized locked door layout and key requirements, data-free.
// GPL-2.0-or-later; see LICENSE.
//
// Two things here are easy to get wrong and are both covered below:
//   1. the field layout (kind at 0x0020, lock at 0x01c0, sk=ck at 0x0200),
//   2. the key semantics for "any", a specific card/skull and "all keys",
//      including the sk=ck mode where skull and card are interchangeable.
#include "dpt_genlocked.h"
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(x) do { if(!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while(0)

int main(void)
{
    int trigger, speed, kind, lock, skull_card, special;
    dpt_genlocked_t locked;

    // Range bounds: locked doors 0x3800..0x3bff, generalized doors 0x3c00+.
    REQUIRE(!DPT_GenLockedIsLocked(0x37ff));   // lifts
    REQUIRE(DPT_GenLockedIsLocked(0x3800));
    REQUIRE(DPT_GenLockedIsLocked(0x3bff));
    REQUIRE(!DPT_GenLockedIsLocked(0x3c00));
    REQUIRE(!DPT_GenLockedDecode(0x3c00, &locked));
    REQUIRE(!DPT_GenLockedDecode(0x3800, NULL));

    // All 8 * 4 * 2 * 8 * 2 = 1024 field combinations decode exactly.
    for (trigger = 0; trigger < 8; ++trigger)
    for (speed = 0; speed < 4; ++speed)
    for (kind = 0; kind < 2; ++kind)
    for (lock = 0; lock < 8; ++lock)
    for (skull_card = 0; skull_card < 2; ++skull_card)
    {
        special = 0x3800 | trigger | (speed << 3) | (kind << 5)
                | (lock << 6) | (skull_card << 9);
        REQUIRE(DPT_GenLockedDecode(special, &locked));
        REQUIRE(locked.trigger == trigger);
        REQUIRE(locked.speed_shift == speed);
        REQUIRE(locked.kind == kind);
        REQUIRE(locked.lock == lock);
        REQUIRE(locked.skull_card == (skull_card != 0));
        REQUIRE(DPT_GenLockedSupported(special));
    }

    // "Any key" needs at least one of the six.
    REQUIRE(!DPT_GenLockedSatisfied(DPT_LOCK_ANY, false, 0));
    REQUIRE(DPT_GenLockedSatisfied(DPT_LOCK_ANY, false, DPT_KEY_REDSKULL));
    REQUIRE(DPT_GenLockedSatisfied(DPT_LOCK_ANY, false, DPT_KEY_BLUECARD));

    // A specific card does not accept the skull unless sk=ck is set.
    REQUIRE(DPT_GenLockedSatisfied(DPT_LOCK_REDCARD, false, DPT_KEY_REDCARD));
    REQUIRE(!DPT_GenLockedSatisfied(DPT_LOCK_REDCARD, false, DPT_KEY_REDSKULL));
    REQUIRE(DPT_GenLockedSatisfied(DPT_LOCK_REDCARD, true, DPT_KEY_REDSKULL));
    REQUIRE(!DPT_GenLockedSatisfied(DPT_LOCK_REDCARD, true, DPT_KEY_BLUESKULL));

    // A specific skull likewise accepts the card only in sk=ck mode.
    REQUIRE(DPT_GenLockedSatisfied(DPT_LOCK_YELLOWSKULL, false, DPT_KEY_YELLOWSKULL));
    REQUIRE(!DPT_GenLockedSatisfied(DPT_LOCK_YELLOWSKULL, false, DPT_KEY_YELLOWCARD));
    REQUIRE(DPT_GenLockedSatisfied(DPT_LOCK_YELLOWSKULL, true, DPT_KEY_YELLOWCARD));

    // Colours do not cross: a blue key never opens a red lock.
    REQUIRE(!DPT_GenLockedSatisfied(DPT_LOCK_REDCARD, true, DPT_KEY_BLUECARD));
    REQUIRE(!DPT_GenLockedSatisfied(DPT_LOCK_BLUESKULL, true, DPT_KEY_REDSKULL));

    // "All keys" without sk=ck needs all six.
    REQUIRE(!DPT_GenLockedSatisfied(DPT_LOCK_ALL, false,
        DPT_KEY_REDCARD | DPT_KEY_BLUECARD | DPT_KEY_YELLOWCARD));
    REQUIRE(DPT_GenLockedSatisfied(DPT_LOCK_ALL, false, DPT_KEY_ALL));

    // With sk=ck one key of each colour is enough, in any form.
    REQUIRE(DPT_GenLockedSatisfied(DPT_LOCK_ALL, true,
        DPT_KEY_REDCARD | DPT_KEY_BLUESKULL | DPT_KEY_YELLOWCARD));
    REQUIRE(!DPT_GenLockedSatisfied(DPT_LOCK_ALL, true,
        DPT_KEY_REDCARD | DPT_KEY_BLUECARD));   // yellow missing

    // Out-of-range lock values are rejected rather than guessed.
    REQUIRE(!DPT_GenLockedSatisfied(-1, false, DPT_KEY_ALL));
    REQUIRE(!DPT_GenLockedSatisfied(8, false, DPT_KEY_ALL));

    // Documented spot value: W1, slow, open-and-stay, blue card, sk=ck.
    REQUIRE(DPT_GenLockedDecode(0x3800 | (1 << 5) | (2 << 6) | (1 << 9),
                                &locked));
    REQUIRE(locked.trigger == 0 && locked.kind == 1);
    REQUIRE(locked.lock == DPT_LOCK_BLUECARD && locked.skull_card);

    puts("Boom generalized locked doors: range bounds, 1024 field combinations "
         "and key requirement rules OK");
    return 0;
}
