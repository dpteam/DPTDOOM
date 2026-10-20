// Boom generalized door bit layout, data-free.
// GPL-2.0-or-later; see LICENSE.
//
// Verifies every field combination against the values documented in BOOM's
// generalized linedef definitions (0x3c00..0x3fff). This is the risky part of
// the feature: a wrong mask or shift produces a door that opens at the wrong
// speed, waits the wrong time or is not triggered at all.
#include "dpt_gendoor.h"
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(x) do { if(!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while(0)

int main(void)
{
    static const int delays[4] = {1, 4, 9, 30};
    int trigger, speed, kind, delay, special;
    dpt_gendoor_t door;

    // The range boundaries are exclusive of the neighbouring Boom ranges.
    REQUIRE(!DPT_GenDoorIsDoor(0x3bff));   // locked doors
    REQUIRE(DPT_GenDoorIsDoor(0x3c00));
    REQUIRE(DPT_GenDoorIsDoor(0x3fff));
    REQUIRE(!DPT_GenDoorIsDoor(0x4000));   // generalized ceilings

    // Every trigger x speed x kind x delay combination decodes exactly.
    for (trigger = 0; trigger < 8; ++trigger)
    for (speed = 0; speed < 4; ++speed)
    for (kind = 0; kind < 4; ++kind)
    for (delay = 0; delay < 4; ++delay)
    {
        special = 0x3c00 | trigger | (speed << 3) | (kind << 5) | (delay << 8);
        REQUIRE(DPT_GenDoorDecode(special, &door));
        REQUIRE(door.trigger == trigger);
        REQUIRE(door.speed_shift == speed);
        REQUIRE(door.kind == kind);
        REQUIRE(door.delay_seconds == delays[delay]);
        REQUIRE(door.monsters == false);
    }

    // Bit 7 is the monster-activation flag and must not leak into other fields.
    REQUIRE(DPT_GenDoorDecode(0x3c00 | 0x0080, &door));
    REQUIRE(door.monsters == true);
    REQUIRE(door.trigger == 0 && door.speed_shift == 0);
    REQUIRE(door.kind == 0 && door.delay_seconds == 1);

    // Reserved high bits are not part of the door range; 0x4000+ is rejected.
    REQUIRE(!DPT_GenDoorDecode(0x4000, &door));
    REQUIRE(!DPT_GenDoorDecode(0x3bff, &door));
    REQUIRE(!DPT_GenDoorDecode(0x3c00, NULL));

    // Spot-check a few documented concrete values.
    REQUIRE(DPT_GenDoorDecode(0x3c00, &door));   // W1 slow open-wait-close 1s
    REQUIRE(door.trigger == 0 && door.kind == 0 && door.delay_seconds == 1);
    REQUIRE(DPT_GenDoorDecode(0x3c02, &door));   // S1
    REQUIRE(door.trigger == 2);
    REQUIRE(DPT_GenDoorDecode(0x3c06, &door));   // P1
    REQUIRE(door.trigger == 6);
    REQUIRE(DPT_GenDoorDecode(0x3c04, &door));   // G1
    REQUIRE(door.trigger == 4);
    REQUIRE(DPT_GenDoorDecode(0x3c18, &door));   // turbo, kind 0
    REQUIRE(door.speed_shift == 3 && door.kind == 0);
    REQUIRE(DPT_GenDoorDecode(0x3c60, &door));   // kind 3 close-and-stay
    REQUIRE(door.kind == 3);
    REQUIRE(DPT_GenDoorDecode(0x3f00, &door));   // 30 second delay
    REQUIRE(door.delay_seconds == 30 && door.trigger == 0);

    puts("Boom generalized doors: range bounds, 512 field combinations, "
         "monster bit and documented spot values OK");
    return 0;
}
