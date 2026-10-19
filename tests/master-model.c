// Data-free directory model and bounds. GPL-2.0-or-later; see LICENSE.
#include "directory.h"
#include <stdio.h>
#include <stdlib.h>
#define REQUIRE(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static dm_directory_t directory;
static unsigned char list[DM_MAX_BODY];
static void Info(unsigned char *p, unsigned int port)
{
    memset(p,0,DM_INFO); memcpy(p,"DPT test",8); memset(p+64,0xaa,20); memset(p+84,0xbb,20);
    memcpy(p+116,"MAP01",5); DM_Put16(p+124,port); p[126]=3; p[128]=2;
}
int main(void)
{
    unsigned char info[DM_INFO],bad[DM_INFO],auth[DM_AUTH],reply[DM_LEASE],host[4]={127,0,0,1},other[4]={127,0,0,2},header[DM_HEADER];
    unsigned int i,j;
    directory.lease_seconds=60; memset(auth,0x55,sizeof(auth)); Info(info,2342);
    REQUIRE(DM_ValidInfo(info));
    memcpy(bad,info,sizeof(bad)); memset(bad,0xff,64); REQUIRE(!DM_ValidInfo(bad));
    for(i=0;i<DM_INFO;++i)
    {
        memcpy(bad,info,sizeof(bad));
        if(i==126 || i==127 || i>=128) { bad[i]=255; REQUIRE(!DM_ValidInfo(bad)); }
    }
    memcpy(bad,info,sizeof(bad)); memcpy(bad+116,"MAP33",5); REQUIRE(!DM_ValidInfo(bad));
    memcpy(bad+116,"MAP00",5); REQUIRE(!DM_ValidInfo(bad));
    memset(bad+116,0,8); memcpy(bad+116,"E4M9",4); REQUIRE(DM_ValidInfo(bad));
    bad[117]='5'; REQUIRE(!DM_ValidInfo(bad));
    REQUIRE(DM_Register(&directory,host,info,auth,1000,reply)==DM_OK);
    REQUIRE(!memcmp(reply,auth,DM_AUTH) && DM_Get32(reply+40)==60 && DM_Get32(reply+44)==20);
    REQUIRE(DM_Register(&directory,host,info,auth,1001,reply)==DM_CONFLICT);
    REQUIRE(DM_List(&directory,2000,list)==4+DM_ROW && DM_Get32(list)==1);
    REQUIRE(!memcmp(list+4,auth,16) && !memcmp(list+20,host,4) && DM_Get32(list+24)==59);
    REQUIRE(!memcmp(list+28,info,DM_INFO));
    REQUIRE(DM_Renew(&directory,other,auth,2000,0,reply)==DM_UNAUTHORIZED);
    auth[20]^=1; REQUIRE(DM_Renew(&directory,host,auth,2000,1,reply)==DM_UNAUTHORIZED); auth[20]^=1;
    REQUIRE(DM_Renew(&directory,host,auth,40000,0,reply)==DM_OK);
    REQUIRE(DM_List(&directory,61000,list)==4+DM_ROW);
    REQUIRE(DM_List(&directory,100000,list)==4 && DM_Get32(list)==0);
    REQUIRE(DM_Renew(&directory,host,auth,100000,0,reply)==DM_UNAUTHORIZED);
    for(i=0;i<DM_MAX_IP_SERVERS;++i)
    { auth[0]=(unsigned char)i; Info(info,2342+i); REQUIRE(DM_Register(&directory,host,info,auth,100001,reply)==DM_OK); }
    auth[0]=100; Info(info,9000); REQUIRE(DM_Register(&directory,host,info,auth,100001,reply)==DM_CAPACITY);
    memcpy(auth,directory.entries[0].id,16); memcpy(auth+16,directory.entries[0].token,24);
    REQUIRE(DM_Renew(&directory,host,auth,100002,0,reply)==DM_OK);
    for(i=DM_MAX_IP_SERVERS;i<DM_MAX_SERVERS;++i)
    { other[3]=(unsigned char)i; auth[0]=(unsigned char)i; REQUIRE(DM_Register(&directory,other,info,auth,100001,reply)==DM_OK); }
    other[2]=1; auth[0]=255; REQUIRE(DM_Register(&directory,other,info,auth,100001,reply)==DM_CAPACITY);
    REQUIRE(DM_List(&directory,100002,list)==DM_MAX_BODY && DM_Get32(list)==DM_MAX_SERVERS);
    DM_Prune(&directory,200000); REQUIRE(DM_List(&directory,200000,list)==4);
    DM_Header(header,DM_LIST,0,0); REQUIRE(DM_RequestLength(header)==0);
    DM_Header(header,DM_REGISTER,DM_INFO,0); REQUIRE(DM_RequestLength(header)==DM_INFO);
    DM_Header(header,DM_HEARTBEAT,DM_AUTH,0); REQUIRE(DM_RequestLength(header)==DM_AUTH);
    for(i=0;i<DM_HEADER;++i)
    {
        unsigned char copy[DM_HEADER]; memcpy(copy,header,sizeof(copy)); copy[i]^=0x80;
        REQUIRE(DM_RequestLength(copy)<0);
    }
    for(j=0;j<65536;++j) { DM_Header(header,j,0xffffffffu,0); REQUIRE(DM_RequestLength(header)<0); }
    puts("Master model: schema, bounds, lease/expiry, source/token auth, capacity and header validation OK");
    return 0;
}
