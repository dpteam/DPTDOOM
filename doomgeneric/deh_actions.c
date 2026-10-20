// DPT.DOOM engine-side action allowlist table for BEX [CODEPTR].
// GPL-2.0-or-later; see LICENSE.
//
// This unit exists so the bounded parser does not depend on the gameplay
// translation units. The data-free parser test uses tests/deh_actions_stub.c,
// which expands the same DPT_ACTION_LIST with local stubs.
#include "deh_actions.h"

// The real definitions live in info.c/p_enemy.c/p_pspr.c and are declared
// without prototypes there, so keep the same unprototyped form.
#define DPT_ACTION_DECLARE(name,ps) void name();
DPT_ACTION_LIST(DPT_ACTION_DECLARE)
#undef DPT_ACTION_DECLARE

#define DPT_ACTION_ENTRY(name,ps) {#name,{name},(boolean)(ps)},
const dpt_action_entry_t dpt_action_entries[]={DPT_ACTION_LIST(DPT_ACTION_ENTRY)};
#undef DPT_ACTION_ENTRY

const unsigned int dpt_action_count=
    sizeof(dpt_action_entries)/sizeof(dpt_action_entries[0]);
