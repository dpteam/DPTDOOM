// Bounded directory model, no sockets or game engine dependencies.
// GPL-2.0-or-later; see LICENSE.
#ifndef DPT_MASTER_DIRECTORY_H
#define DPT_MASTER_DIRECTORY_H
#include "protocol.h"
typedef struct
{
    int active;
    // Authentication source never changes when a listing address is rewritten.
    unsigned char host[4], advertised[4], info[DM_INFO], id[16], token[24];
    uint64_t expires;
    int relay_available;
} dm_entry_t;
#define DM_MAX_ADDRESS_MAPS 8
typedef struct
{
    unsigned char network[4], advertised[4];
    unsigned int prefix;
    int dynamic, enabled;
} dm_address_map_t;

// Ban rules, matched against the TCP source address only.
//
// Deliberately NOT matched against the advertised/listing address: a peer
// supplies that, so banning by it would let anyone escape a ban by advertising
// a different IP, and would also punish an innocent third party whose address
// was advertised. Source is what the kernel tells us and what auth/limits use.
#define DM_MAX_BANS 64
typedef struct
{
    unsigned char network[4];
    unsigned int prefix;
    // Expiry as Unix seconds; 0 means permanent. Checked at match time and
    // pruned on reload/periodically, so an expired rule stops applying even if
    // the file is never touched again.
    uint64_t expires;
} dm_ban_t;
typedef struct
{
    dm_entry_t entries[DM_MAX_SERVERS];
    unsigned int lease_seconds;
    dm_address_map_t address_maps[DM_MAX_ADDRESS_MAPS];
    unsigned int address_map_count;
    dm_ban_t bans[DM_MAX_BANS];
    unsigned int ban_count;
    // Allow list, same rule grammar, matched the same way (TCP source only).
    // Only consulted when allowlist_only is set: an allow list that is present
    // but not enforced is reported at startup rather than silently ignored.
    dm_ban_t allowed[DM_MAX_BANS];
    unsigned int allowed_count;
    int allowlist_only;
} dm_directory_t;
// Explicit operator policy, never metadata supplied by a registering peer.
// Syntax: private-or-loopback-IPv4/canonical-prefix=public-IPv4.
int DM_AddAddressMap(dm_directory_t *directory, const char *text);
int DM_AddHairpinSubnet(dm_directory_t *directory, const char *cidr);
// Ban rule syntax: IPv4 or canonical IPv4/prefix. A host address means /32.
// Returns 0 on malformed input, overflow or an exact duplicate; callers treat
// that as a config error rather than silently ignoring half a ban list.
// Shared rule parser, exposed so both lists use identical grammar and the
// reload path can validate into a scratch array before committing.
int DM_ParseRule(dm_ban_t *rules, unsigned int *count, const char *text);
int DM_AddBan(dm_directory_t *directory, const char *text);
// Clears every rule, used when reloading the file so a removed line really
// removes the ban.
void DM_ClearBans(dm_directory_t *directory);
// True when the TCP source address matches a ban rule that has not expired.
// `now` is Unix seconds; 0 means "no expiry has passed yet", which keeps the
// permanent-only callers and tests unchanged.
int DM_IsBanned(const dm_directory_t *directory, const unsigned char source[4], uint64_t now);
// Drops expired ban rules. Returns how many were removed.
unsigned int DM_PruneBans(dm_directory_t *directory, uint64_t now);

// Allow list: same grammar and the same TCP-source-only matching. When
// allowlist_only is set, a source that is not listed is refused; bans still win
// over the allow list, so denying always beats permitting.
int DM_AddAllowed(dm_directory_t *directory, const char *text);
void DM_ClearAllowed(dm_directory_t *directory);
int DM_IsAllowed(const dm_directory_t *directory, const unsigned char source[4]);
void DM_AdvertisedAddress(const dm_directory_t *directory, const unsigned char source[4], unsigned char output[4]);
int DM_PublicIPv4(const unsigned char ip[4]);
int DM_MappingReady(const dm_directory_t *directory, const unsigned char source[4]);
void DM_Prune(dm_directory_t *directory, uint64_t now);
int DM_ValidInfo(const unsigned char *info);
const char *DM_InfoError(const unsigned char *info);
// Entropy is supplied by the OS-backed transport, not rand()/timestamps.
unsigned int DM_Register(dm_directory_t *directory, const unsigned char host[4], const unsigned char *info,
                         const unsigned char entropy[DM_AUTH], uint64_t now, unsigned char reply[DM_LEASE]);
unsigned int DM_Renew(dm_directory_t *directory, const unsigned char host[4], const unsigned char auth[DM_AUTH],
                      uint64_t now, int remove, unsigned char reply[4]);
unsigned int DM_List(dm_directory_t *directory, uint64_t now, unsigned char *reply);
#endif
