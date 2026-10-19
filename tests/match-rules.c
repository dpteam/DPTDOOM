// Data-free rule policy tests; production physics integration tested separately.
// GPL-2.0-or-later; see LICENSE.
#include "dpt_rules.h"
#include <stdio.h>
#define CHECK(test) do { if (!(test)) { fprintf(stderr, "line %d: %s\n", __LINE__, #test); return 1; } } while (0)
int main(void)
{
    unsigned int flags = 99;
    mapthing_t thing = {0};
    mobj_t a = {0}, b = {0}, missile = {0}, monster = {0};
    player_t p = {0}, q = {0};
    int i;
    for (i = 0; i < 4; ++i)
    {
        char value[2] = {(char)('0' + i), 0};
        CHECK(DPT_ParseDMFlags(value, &flags) && flags == (unsigned int)i);
    }
    CHECK(!DPT_ParseDMFlags("", &flags)); CHECK(!DPT_ParseDMFlags("-1", &flags));
    CHECK(!DPT_ParseDMFlags("4", &flags)); CHECK(!DPT_ParseDMFlags("0x2", &flags));
    CHECK(!DPT_ParseDMFlags("9999999999999999999999", &flags));
    CHECK(!DPT_ParseDMFlags("2x", &flags));
    for (i = 2001; i <= 2006; ++i)
    {
        thing.type = i; thing.options = 16 | 7;
        CHECK(DPT_SkipCoopWeapon(1, true, 0, &thing));
        CHECK(!DPT_SkipCoopWeapon(0, true, 0, &thing));
        CHECK(!DPT_SkipCoopWeapon(1, false, 0, &thing));
        CHECK(!DPT_SkipCoopWeapon(1, true, 1, &thing));
        thing.options = 7; CHECK(!DPT_SkipCoopWeapon(1, true, 0, &thing));
    }
    thing.type = 82; thing.options = 23; CHECK(DPT_SkipCoopWeapon(1, true, 0, &thing));
    thing.type = 3004; CHECK(!DPT_SkipCoopWeapon(1, true, 0, &thing));
    thing.type = 2007; CHECK(!DPT_SkipCoopWeapon(1, true, 0, &thing));
    thing.type = 5; CHECK(!DPT_SkipCoopWeapon(1, true, 0, &thing));
    a.type = b.type = MT_PLAYER; a.player = &p; b.player = &q;
    a.health = b.health = 100; p.mo = &a; q.mo = &b;
    p.playerstate = q.playerstate = PST_LIVE;
    CHECK(DPT_PlayersPass(2, true, &a, &b)); CHECK(DPT_PlayersPass(2, true, &b, &a));
    CHECK(!DPT_PlayersPass(0, true, &a, &b)); CHECK(!DPT_PlayersPass(2, false, &a, &b));
    CHECK(!DPT_PlayersPass(2, true, &a, &a));
    missile.type = MT_ROCKET; monster.type = MT_TROOP;
    CHECK(!DPT_PlayersPass(2, true, &missile, &b)); CHECK(!DPT_PlayersPass(2, true, &a, &monster));
    b.health = 0; CHECK(!DPT_PlayersPass(2, true, &a, &b)); b.health = 100;
    q.playerstate = PST_DEAD; CHECK(!DPT_PlayersPass(2, true, &a, &b));
    q.playerstate = PST_LIVE; q.mo = &monster; CHECK(!DPT_PlayersPass(2, true, &a, &b));
    puts("Match rules: strict mask parsing, coop weapon filtering and player-only passing OK");
    return 0;
}
