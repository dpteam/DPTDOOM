// Explicit hairpin address policy. No real public addresses. GPL-2.0-or-later.
#include "directory.h"
#include <stdio.h>
#include <stdlib.h>
#define REQUIRE(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static dm_directory_t d;
int main(void)
{
    unsigned char source[4]={192,168,1,1},external[4]={203,0,113,9},other[4]={192,168,2,1},out[4];
    unsigned char info[DM_INFO]={0},auth[DM_AUTH]={1},lease[DM_LEASE],list[DM_MAX_BODY];unsigned int i;
    const char *bad[]={"", "192.168.1.1/24=203.0.113.9", "192.168.1.0/0=203.0.113.9",
        "192.168.1.0/33=203.0.113.9", "0.0.0.0/0=203.0.113.9", "8.8.8.0/24=203.0.113.9",
        "192.168.1.0/24=192.168.1.11", "192.168.1.0/24=127.0.0.1", "192.168.1.0/24=224.0.0.1",
        "192.168.1.0/24=example.invalid", "192.168.1.0/24=203.0.113.9:2342", "192.168.1.0/24=203.0.113.999"};
    d.lease_seconds=60;
    DM_AdvertisedAddress(&d,source,out);REQUIRE(!memcmp(out,source,4));
    for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i)REQUIRE(!DM_AddAddressMap(&d,bad[i]));
    REQUIRE(!d.address_map_count);
    REQUIRE(DM_AddAddressMap(&d,"192.168.1.0/24=203.0.113.9"));
    REQUIRE(!DM_AddAddressMap(&d,"192.168.1.128/25=203.0.113.10"));
    REQUIRE(!DM_AddAddressMap(&d,"192.168.0.0/16=203.0.113.10"));
    DM_AdvertisedAddress(&d,source,out);REQUIRE(!memcmp(out,external,4));
    DM_AdvertisedAddress(&d,other,out);REQUIRE(!memcmp(out,other,4));
    memcpy(info,"Mapped host",11);memcpy(info+116,"MAP01",5);DM_Put16(info+124,2342);info[126]=3;info[128]=2;
    REQUIRE(DM_Register(&d,source,info,auth,1000,lease)==DM_OK);
    REQUIRE(DM_List(&d,1000,list)==4+DM_ROW);REQUIRE(!memcmp(list+20,external,4));
    REQUIRE(DM_Get16(list+28+124)==2342);
    REQUIRE(DM_Renew(&d,external,auth,2000,0,lease)==DM_UNAUTHORIZED);
    REQUIRE(DM_Renew(&d,source,auth,2000,0,lease)==DM_OK);
    {unsigned char auth2[DM_AUTH]={2};REQUIRE(DM_Register(&d,external,info,auth2,2000,lease)==DM_CONFLICT);}
    DM_Put16(info+124,2343);auth[0]=3;REQUIRE(DM_Register(&d,source,info,auth,2000,lease)==DM_OK);
    REQUIRE(DM_Renew(&d,source,auth,3000,1,lease)==DM_OK);
    DM_Prune(&d,100000);REQUIRE(DM_List(&d,100000,list)==4);
    puts("Address mapping: opt-in CIDR, independent source auth, endpoint collision, unchanged port and expiry OK");return 0;
}
