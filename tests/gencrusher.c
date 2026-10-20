// Boom generalized crusher bit layout, data-free.
// GPL-2.0-or-later; see LICENSE.
//
// Crushers are the simplest generalized family: no target and no crush bit,
// because every crusher crushes. Only trigger, speed, monster and silent exist,
// so a wrong mask here would silently change crusher speed or sound.
#include "dpt_gencrusher.h"
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(x) do { if(!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while(0)

int main(void)
{
    int trigger, speed, monster, silent, special;
    dpt_gencrusher_t crusher;

    // Range bounds: crushers 0x2f80..0x2fff, stairs start at 0x3000.
    REQUIRE(!DPT_GenCrusherIsCrusher(0x2f7f));
    REQUIRE(DPT_GenCrusherIsCrusher(0x2f80));
    REQUIRE(DPT_GenCrusherIsCrusher(0x2fff));
    REQUIRE(!DPT_GenCrusherIsCrusher(0x3000));
    REQUIRE(!DPT_GenCrusherDecode(0x3000, &crusher));
    REQUIRE(!DPT_GenCrusherDecode(0x2f80, NULL));

    // All 8 * 4 * 2 * 2 = 128 field combinations decode exactly.
    for (trigger = 0; trigger < 8; ++trigger)
    for (speed = 0; speed < 4; ++speed)
    for (monster = 0; monster < 2; ++monster)
    for (silent = 0; silent < 2; ++silent)
    {
        special = 0x2f80 | trigger | (speed << 3)
                | (monster << 5) | (silent << 6);
        REQUIRE(DPT_GenCrusherDecode(special, &crusher));
        REQUIRE(crusher.trigger == trigger);
        REQUIRE(crusher.speed_shift == speed);
        REQUIRE(crusher.monsters == (monster != 0));
        REQUIRE(crusher.silent == (silent != 0));
    }

    // Bits 7..15 are not part of the crusher encoding: a value inside the range
    // with them set is still a crusher, but nothing must leak into the fields.
    REQUIRE(DPT_GenCrusherDecode(0x2f80 | 0x0f80, &crusher));
    REQUIRE(crusher.trigger == 0 && crusher.speed_shift == 0);
    REQUIRE(!crusher.monsters && !crusher.silent);

    // Documented spot values.
    REQUIRE(DPT_GenCrusherDecode(0x2f80, &crusher));       // W1 slow audible
    REQUIRE(crusher.trigger == 0 && crusher.speed_shift == 0);
    REQUIRE(!crusher.silent && !crusher.monsters);
    REQUIRE(DPT_GenCrusherDecode(0x2f83, &crusher));       // SR
    REQUIRE(crusher.trigger == 3);
    REQUIRE(DPT_GenCrusherDecode(0x2fd8, &crusher));       // turbo + silent
    REQUIRE(crusher.speed_shift == 3 && crusher.silent);

    // Every trigger class except push is supported.
    REQUIRE(DPT_GenCrusherSupported(0x2f80));       // W1
    REQUIRE(DPT_GenCrusherSupported(0x2f81));       // WR
    REQUIRE(DPT_GenCrusherSupported(0x2f82));       // S1
    REQUIRE(DPT_GenCrusherSupported(0x2f83));       // SR
    REQUIRE(DPT_GenCrusherSupported(0x2f84));       // G1
    REQUIRE(DPT_GenCrusherSupported(0x2f85));       // GR
    REQUIRE(!DPT_GenCrusherSupported(0x2f86));      // P1: no back-sector path
    REQUIRE(!DPT_GenCrusherSupported(0x2f87));      // PR
    REQUIRE(!DPT_GenCrusherSupported(0x3000));      // not a crusher

    puts("Boom generalized crushers: range bounds, 128 field combinations and "
         "trigger support rules OK");
    return 0;
}
