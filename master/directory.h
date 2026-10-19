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
} dm_entry_t;
#define DM_MAX_ADDRESS_MAPS 8
typedef struct
{
    unsigned char network[4], advertised[4];
    unsigned int prefix;
    int dynamic, enabled;
} dm_address_map_t;
typedef struct
{
    dm_entry_t entries[DM_MAX_SERVERS];
    unsigned int lease_seconds;
    dm_address_map_t address_maps[DM_MAX_ADDRESS_MAPS];
    unsigned int address_map_count;
} dm_directory_t;
// Explicit operator policy, never metadata supplied by a registering peer.
// Syntax: private-or-loopback-IPv4/canonical-prefix=public-IPv4.
int DM_AddAddressMap(dm_directory_t *directory, const char *text);
int DM_AddHairpinSubnet(dm_directory_t *directory, const char *cidr);
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
