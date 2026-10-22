// Boom generalized sector specials: bit layout, damage tiers and support rules.
// GPL-2.0-or-later; see LICENSE.
//
// The trap this test is built around: the damage field is not the damage. A
// vanilla sector special 5 is 10 damage but its damage field is 0, so the engine
// must never feed a vanilla value through the generalized damage table.
#include "dpt_secspecial.h"
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(x) do { if(!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while(0)

int main(void)
{
    int base, damage, secret, friction, push, special;
    dpt_secspecial_t sec;

    // The vanilla/generalized split is exactly 32.
    REQUIRE(!DPT_SecSpecialIsGeneralized(0));
    REQUIRE(!DPT_SecSpecialIsGeneralized(9));
    REQUIRE(!DPT_SecSpecialIsGeneralized(31));
    REQUIRE(DPT_SecSpecialIsGeneralized(32));
    REQUIRE(DPT_SecSpecialIsGeneralized(0x200));

    // All 32 * 4 * 2 * 2 * 2 = 1024 defined combinations decode exactly.
    for (base = 0; base < 32; ++base)
    for (damage = 0; damage < 4; ++damage)
    for (secret = 0; secret < 2; ++secret)
    for (friction = 0; friction < 2; ++friction)
    for (push = 0; push < 2; ++push)
    {
        special = base | (damage << 5) | (secret << 7)
                | (friction << 8) | (push << 9);
        REQUIRE(DPT_SecSpecialDecode(special, &sec));
        REQUIRE(sec.base == base);
        REQUIRE(sec.damage == damage);
        REQUIRE(sec.secret == (secret != 0));
        REQUIRE(sec.friction == (friction != 0));
        REQUIRE(sec.push == (push != 0));
    }

    // Bits above 0x03ff are not defined by BOOM and must be rejected, not
    // silently dropped.
    REQUIRE(!DPT_SecSpecialDecode(0x0400, &sec));
    REQUIRE(!DPT_SecSpecialDecode(0x8000, &sec));
    REQUIRE(!DPT_SecSpecialDecode(0x0400 | 0x61, &sec));
    REQUIRE(!DPT_SecSpecialDecode(0x61, NULL));
    REQUIRE(DPT_SecSpecialDecode(0x03ff, &sec));

    // Damage tiers: none, 5, 10, 20.
    REQUIRE(DPT_SecSpecialDamage(0) == 0);
    REQUIRE(DPT_SecSpecialDamage(1) == 5);
    REQUIRE(DPT_SecSpecialDamage(2) == 10);
    REQUIRE(DPT_SecSpecialDamage(3) == 20);

    // Only the heaviest tier leaks through a radiation suit.
    REQUIRE(DPT_SecSpecialSuitBlocks(1));
    REQUIRE(DPT_SecSpecialSuitBlocks(2));
    REQUIRE(!DPT_SecSpecialSuitBlocks(3));

    // Supported subset: base type, damage, secret and friction. Push (bit 9)
    // still needs a pusher thinker plus per-object touching-sector and
    // water-height tracking, which this engine does not have.
    REQUIRE(DPT_SecSpecialSupported(0));           // no special at all
    REQUIRE(DPT_SecSpecialSupported(0x61));        // base 1, damage 3
    REQUIRE(DPT_SecSpecialSupported(0x80 | 5));    // secret, base 5
    REQUIRE(DPT_SecSpecialSupported(0xE1));        // base 1, damage 3, secret
    REQUIRE(DPT_SecSpecialSupported(0x100));       // friction
    REQUIRE(!DPT_SecSpecialSupported(0x200));      // push
    REQUIRE(!DPT_SecSpecialSupported(0x300));      // friction + push
    REQUIRE(!DPT_SecSpecialSupported(0x400));      // undefined bit

    // Vanilla specials stay in the vanilla path, so their damage field is not
    // the damage. This is the exact mistake the split guards against.
    REQUIRE(!DPT_SecSpecialIsGeneralized(5));
    REQUIRE(DPT_SecSpecialDecode(5, &sec));
    REQUIRE(sec.damage == 0);                      // yet vanilla deals 10
    REQUIRE(!DPT_SecSpecialIsGeneralized(7));
    REQUIRE(DPT_SecSpecialDecode(7, &sec));
    REQUIRE(sec.damage == 0);                      // yet vanilla deals 5

    // Spot value: base 9 (secret type) with damage 2 is not a vanilla secret;
    // only the SECRET_MASK bit makes a generalized sector secret.
    REQUIRE(DPT_SecSpecialDecode(9 | (2 << 5), &sec));
    REQUIRE(sec.base == 9 && sec.damage == 2 && !sec.secret);

    puts("Boom generalized sector specials: 1024 field combinations, damage "
         "tiers and support rules OK");
    return 0;
}
