// Discovery policy tests without Internet calls. GPL-2.0-or-later.
#include "public_address.h"
#include <stdio.h>
#include <stdlib.h>
#define REQUIRE(x) do { if(!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static pa_state_t state;
static dm_directory_t directory;
int main(void)
{
    unsigned char ip[4],source[4]={192,168,50,2},outside[4]={198,51,100,20},out[4];
    unsigned char info[DM_INFO]={0},auth[DM_AUTH]={1},lease[DM_LEASE];
    PA_Init(&state);directory.lease_seconds=60;
    REQUIRE(PA_Prepare(&state,&directory));REQUIRE(!directory.address_map_count);
    REQUIRE(PA_ParseIPv4("203.0.113.10\n",ip));REQUIRE(!PA_ParseIPv4("203.0.113.10junk",ip));
    REQUIRE(!PA_ParseIPv4("10.1.2.3",ip));REQUIRE(!PA_ParseIPv4("100.64.1.1",ip));REQUIRE(!PA_ParseIPv4("999.1.1.1",ip));
    REQUIRE(PA_ValidHost("example.org"));REQUIRE(!PA_ValidHost("https://example.org"));REQUIRE(!PA_ValidHost("a..b"));
    REQUIRE(PA_ValidAPI("https://api.example.org/ip"));REQUIRE(!PA_ValidAPI("http://api.example.org"));
    REQUIRE(!PA_ValidAPI("https://user:pass@api.example.org/ip"));REQUIRE(!PA_ValidAPI("https://api.example.org:443/ip"));
    REQUIRE(!PA_ValidAPI("https://api.example.org/ip?secret=x"));
    REQUIRE(PA_Config(&state,"public-address-mode","dns"));REQUIRE(!PA_Config(&state,"public-address-mode","dns"));
    REQUIRE(PA_Config(&state,"public-host","example.org"));REQUIRE(PA_Config(&state,"hairpin-subnet","192.168.50.0/24"));
    REQUIRE(PA_Config(&state,"public-refresh-seconds","30"));REQUIRE(PA_Prepare(&state,&directory));
    REQUIRE(!DM_MappingReady(&directory,source));REQUIRE(DM_MappingReady(&directory,outside));
    memcpy(info,"Host",4);memcpy(info+116,"MAP01",5);DM_Put16(info+124,2342);info[126]=3;info[128]=2;
    REQUIRE(DM_Register(&directory,source,info,auth,1000,lease)==DM_CAPACITY);
    REQUIRE(PA_ParseIPv4("203.0.113.10",ip));REQUIRE(PA_Apply(&state,&directory,ip,1000));
    DM_AdvertisedAddress(&directory,source,out);REQUIRE(!memcmp(out,ip,4));
    REQUIRE(DM_Register(&directory,source,info,auth,1001,lease)==DM_OK);
    REQUIRE(PA_Apply(&state,&directory,ip,2000));REQUIRE(directory.entries[0].active);
    ip[3]=11;REQUIRE(PA_Apply(&state,&directory,ip,3000));REQUIRE(!directory.entries[0].active);
    REQUIRE(DM_Renew(&directory,source,auth,3001,0,lease)==DM_UNAUTHORIZED);
    REQUIRE(DM_Register(&directory,source,info,auth,3002,lease)==DM_OK);
    // Expiry must disable mapped entries but leave ordinary outside sources alone.
    auth[0]=2;REQUIRE(DM_Register(&directory,outside,info,auth,3003,lease)==DM_OK);
    state.next=UINT64_MAX;PA_Tick(&state,&directory,state.good_until);
    REQUIRE(!DM_MappingReady(&directory,source));REQUIRE(!directory.entries[0].active&&directory.entries[1].active);
    PA_Close(&state);
    memset(&directory,0,sizeof(directory));PA_Init(&state);
    REQUIRE(PA_Config(&state,"public-address-mode","manual"));REQUIRE(PA_Config(&state,"public-ip","203.0.113.12"));
    REQUIRE(PA_Config(&state,"hairpin-subnet","127.0.0.1/32"));REQUIRE(PA_Prepare(&state,&directory));
    REQUIRE(directory.address_maps[0].enabled);
    PA_Init(&state);REQUIRE(PA_Config(&state,"public-address-mode","dns"));REQUIRE(!PA_Prepare(&state,&directory));
    puts("Public address policy: modes/config/HTTPS grammar, IPv4 validation, last-good expiry, changed-IP lease invalidation OK");return 0;
}
