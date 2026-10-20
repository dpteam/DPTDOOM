// Data-free action table for the parser unit test.
// GPL-2.0-or-later; see LICENSE.
//
// The real table lives in doomgeneric/deh_actions.c and points at the engine's
// action functions. Linking those into a parser test would drag in the whole
// gameplay graph, so this stub expands the same DPT_ACTION_LIST into distinct
// local no-ops. Names, order and classes stay identical by construction; only
// the pointer identity differs, which is enough to test parsing, allowlisting
// and class rejection.
#include "deh_actions.h"

// Each stub records its own address into its own static, so the 74 bodies have
// distinct relocations. Empty bodies would be identical code and MSVC Release
// folds them with /OPT:ICF, collapsing every allowlist entry to one address and
// breaking name lookup by pointer.
#define DPT_ACTION_STUB(name,ps) \
    static const void *dpt_stub_self_##name; \
    static void name(void) { dpt_stub_self_##name = (const void *)&name; }
DPT_ACTION_LIST(DPT_ACTION_STUB)
#undef DPT_ACTION_STUB

#define DPT_ACTION_ENTRY(name,ps) {#name,{(actionf_v)name},(boolean)(ps)},
const dpt_action_entry_t dpt_action_entries[]={DPT_ACTION_LIST(DPT_ACTION_ENTRY)};
#undef DPT_ACTION_ENTRY

const unsigned int dpt_action_count=
    sizeof(dpt_action_entries)/sizeof(dpt_action_entries[0]);
