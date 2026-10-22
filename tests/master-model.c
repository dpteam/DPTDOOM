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
    // The flags word at row offset 156 exists only for bit0 (relay available).
    // Nothing else may be packed in there: clients from before the relay exist
    // reject any other bit, so the row has to stay byte-identical.
    REQUIRE(DM_Get32(list+4+156)==0);
    directory.entries[0].relay_available=1;
    REQUIRE(DM_List(&directory,2000,list)==4+DM_ROW);
    REQUIRE(DM_Get32(list+4+156)==DM_RELAY_AVAILABLE);
    directory.entries[0].relay_available=0;
    REQUIRE(DM_List(&directory,2000,list)==4+DM_ROW && DM_Get32(list+4+156)==0);
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

    // Ban rules. Matching is by TCP source only; a peer-supplied advertised
    // address must never be able to lift a ban.
    {
        dm_directory_t bans;
        unsigned char blocked[4]={203,0,113,7};
        unsigned char allowed[4]={198,51,100,7};
        unsigned char subnet[4]={192,168,10,42};
        unsigned char subnet_other[4]={192,168,11,42};

        memset(&bans,0,sizeof(bans));
        REQUIRE(!DM_IsBanned(&bans,blocked,0,DM_SCOPE_BOTH));           // empty list bans nothing

        REQUIRE(DM_AddBan(&bans,"203.0.113.7"));        // bare address is /32
        REQUIRE(DM_IsBanned(&bans,blocked,0,DM_SCOPE_BOTH));
        REQUIRE(!DM_IsBanned(&bans,allowed,0,DM_SCOPE_BOTH));
        REQUIRE(!DM_IsBanned(&bans,subnet,0,DM_SCOPE_BOTH));            // no accidental widening

        REQUIRE(DM_AddBan(&bans,"192.168.10.0/24"));
        REQUIRE(DM_IsBanned(&bans,subnet,0,DM_SCOPE_BOTH));
        REQUIRE(!DM_IsBanned(&bans,subnet_other,0,DM_SCOPE_BOTH));

        // Rejections must be errors, not silent acceptance.
        REQUIRE(!DM_AddBan(&bans,"203.0.113.7"));       // exact duplicate
        REQUIRE(!DM_AddBan(&bans,"192.168.10.5/24"));   // non-canonical CIDR
        REQUIRE(!DM_AddBan(&bans,"203.0.113.0/33"));    // prefix out of range
        REQUIRE(!DM_AddBan(&bans,"203.0.113"));         // malformed
        REQUIRE(!DM_AddBan(&bans,"203.0.113.0/"));      // missing prefix
        REQUIRE(!DM_AddBan(&bans,""));                  // empty
        REQUIRE(!DM_AddBan(&bans,"::1"));               // not IPv4
        REQUIRE(!DM_AddBan(&bans,"203.0.113.7 extra")); // trailing junk
        REQUIRE(bans.ban_count==2);

        // A full table is a hard limit, not an unbounded allocation.
        {
            dm_directory_t full;
            char rule[32];
            unsigned int k;
            memset(&full,0,sizeof(full));
            for(k=0;k<DM_MAX_BANS;++k)
            {
                snprintf(rule,sizeof(rule),"10.0.%u.0/24",k);
                REQUIRE(DM_AddBan(&full,rule));
            }
            snprintf(rule,sizeof(rule),"10.1.0.0/24");
            REQUIRE(!DM_AddBan(&full,rule));
        }

        // Clearing is how a reload removes a rule that was deleted from the file.
        DM_ClearBans(&bans);
        REQUIRE(bans.ban_count==0);
        REQUIRE(!DM_IsBanned(&bans,blocked,0,DM_SCOPE_BOTH));

        // Allow list: same grammar and matching, but a separate set, and it is
        // only consulted by the caller when allowlist_only is set.
        memset(&bans,0,sizeof(bans));
        REQUIRE(!DM_IsAllowed(&bans,blocked,0,DM_SCOPE_BOTH));      // empty allow list allows nothing
        REQUIRE(DM_AddAllowed(&bans,"203.0.113.0/24"));
        REQUIRE(DM_IsAllowed(&bans,blocked,0,DM_SCOPE_BOTH));
        REQUIRE(!DM_IsAllowed(&bans,allowed,0,DM_SCOPE_BOTH));
        REQUIRE(!DM_AddAllowed(&bans,"203.0.113.0/24"));  // duplicate
        REQUIRE(!DM_AddAllowed(&bans,"192.168.10.5/24")); // non-canonical
        REQUIRE(bans.allowed_count==1);
        // The two lists are independent: allowing does not ban, banning does not allow.
        REQUIRE(!DM_IsBanned(&bans,blocked,0,DM_SCOPE_BOTH));
        REQUIRE(DM_AddBan(&bans,"203.0.113.7"));
        REQUIRE(DM_IsBanned(&bans,blocked,0,DM_SCOPE_BOTH) && DM_IsAllowed(&bans,blocked,0,DM_SCOPE_BOTH));
        DM_ClearAllowed(&bans);
        REQUIRE(!DM_IsAllowed(&bans,blocked,0,DM_SCOPE_BOTH));
        REQUIRE(DM_IsBanned(&bans,blocked,0,DM_SCOPE_BOTH));        // clearing one list leaves the other
    }

    // Temporary bans: "until <ISO-8601 UTC>" stops applying at that instant
    // without needing the file to be touched again.
    {
        dm_directory_t temp;
        unsigned char blocked[4]={203,0,113,7};
        uint64_t deadline=0;

        memset(&temp,0,sizeof(temp));
        REQUIRE(DM_AddBan(&temp,"203.0.113.7 until 2030-01-01T00:00:00Z"));
        REQUIRE(temp.bans[0].expires!=0);
        // Before the deadline the ban applies.
        REQUIRE(DM_IsBanned(&temp,blocked,temp.bans[0].expires-1,DM_SCOPE_BOTH));
        // At and after the deadline it does not, even before pruning runs.
        REQUIRE(!DM_IsBanned(&temp,blocked,temp.bans[0].expires,DM_SCOPE_BOTH));
        REQUIRE(!DM_IsBanned(&temp,blocked,temp.bans[0].expires+1,DM_SCOPE_BOTH));

        // Pruning removes it, so the table cannot fill up with dead rules.
        REQUIRE(DM_PruneBans(&temp,temp.bans[0].expires-1)==0);
        REQUIRE(temp.ban_count==1);
        REQUIRE(DM_PruneBans(&temp,temp.bans[0].expires)==1);
        REQUIRE(temp.ban_count==0);

        // A permanent rule has no expiry and is never pruned.
        memset(&temp,0,sizeof(temp));
        REQUIRE(DM_AddBan(&temp,"203.0.113.7"));
        REQUIRE(temp.bans[0].expires==0);
        REQUIRE(DM_IsBanned(&temp,blocked,0xffffffffu,DM_SCOPE_BOTH));
        REQUIRE(DM_PruneBans(&temp,0xffffffffu)==0);

        // Malformed expiry must be rejected, not silently treated as permanent.
        REQUIRE(!DM_AddBan(&temp,"203.0.113.8 until 2030-01-01"));
        REQUIRE(!DM_AddBan(&temp,"203.0.113.8 until 2030-01-01T00:00:00"));
        REQUIRE(!DM_AddBan(&temp,"203.0.113.8 until 2030-13-01T00:00:00Z"));
        REQUIRE(!DM_AddBan(&temp,"203.0.113.8 until 2030-01-32T00:00:00Z"));
        REQUIRE(!DM_AddBan(&temp,"203.0.113.8 until 2030-01-01T25:00:00Z"));
        REQUIRE(!DM_AddBan(&temp,"203.0.113.8 until 2030-01-01T00:00:00+02:00"));
        REQUIRE(!DM_AddBan(&temp,"203.0.113.8 until"));
        REQUIRE(!DM_AddBan(&temp,"203.0.113.8 untily 2030-01-01T00:00:00Z"));
        REQUIRE(!DM_AddBan(&temp,"203.0.113.8 2030-01-01T00:00:00Z"));
        // The same address with a different deadline is a different rule.
        REQUIRE(DM_AddBan(&temp,"203.0.113.8 until 2030-01-01T00:00:00Z"));
        REQUIRE(DM_AddBan(&temp,"203.0.113.8 until 2031-01-01T00:00:00Z"));
        REQUIRE(temp.ban_count==3);
        (void)deadline;
    }

    // Rule scope: a relay-only ban must not silence a peer's registration, and
    // a control-only ban must not eject it from a relay. Unscoped rules keep
    // applying everywhere, so an existing ban file changes meaning not at all.
    {
        dm_directory_t scoped;
        unsigned char peer[4]={203,0,113,7};
        unsigned char other[4]={198,51,100,9};

        memset(&scoped,0,sizeof(scoped));
        REQUIRE(DM_AddBan(&scoped,"203.0.113.7 scope relay"));
        REQUIRE(scoped.bans[0].scope==DM_SCOPE_RELAY);
        REQUIRE(scoped.bans[0].expires==0);
        REQUIRE(DM_IsBanned(&scoped,peer,0,DM_SCOPE_RELAY));
        REQUIRE(!DM_IsBanned(&scoped,peer,0,DM_SCOPE_CONTROL));
        REQUIRE(!DM_IsBanned(&scoped,other,0,DM_SCOPE_RELAY));

        // Scope without a deadline and with one, in either the natural order.
        REQUIRE(DM_AddBan(&scoped,"198.51.100.9 scope control"));
        REQUIRE(scoped.bans[1].scope==DM_SCOPE_CONTROL);
        REQUIRE(DM_IsBanned(&scoped,other,0,DM_SCOPE_CONTROL));
        REQUIRE(!DM_IsBanned(&scoped,other,0,DM_SCOPE_RELAY));

        REQUIRE(DM_AddBan(&scoped,"192.0.2.5 until 2030-01-01T00:00:00Z scope relay"));
        REQUIRE(scoped.bans[2].scope==DM_SCOPE_RELAY);
        REQUIRE(scoped.bans[2].expires!=0);
        {
            unsigned char timed[4]={192,0,2,5};
            REQUIRE(DM_IsBanned(&scoped,timed,scoped.bans[2].expires-1,DM_SCOPE_RELAY));
            REQUIRE(!DM_IsBanned(&scoped,timed,scoped.bans[2].expires,DM_SCOPE_RELAY));
            REQUIRE(!DM_IsBanned(&scoped,timed,scoped.bans[2].expires-1,DM_SCOPE_CONTROL));
        }

        // Explicit "both" is the default, not a third kind of rule.
        REQUIRE(DM_AddBan(&scoped,"192.0.2.77 scope both"));
        REQUIRE(scoped.bans[3].scope==DM_SCOPE_BOTH);
        {
            unsigned char everywhere[4]={192,0,2,77};
            REQUIRE(DM_IsBanned(&scoped,everywhere,0,DM_SCOPE_CONTROL));
            REQUIRE(DM_IsBanned(&scoped,everywhere,0,DM_SCOPE_RELAY));
        }

        // Same address with different scopes are distinct rules, so an operator
        // can ban relay-only first and extend it later without a conflict.
        REQUIRE(DM_AddBan(&scoped,"203.0.113.7 scope control"));
        REQUIRE(scoped.ban_count==5);
        REQUIRE(DM_IsBanned(&scoped,peer,0,DM_SCOPE_CONTROL));
        REQUIRE(DM_IsBanned(&scoped,peer,0,DM_SCOPE_RELAY));
        // ...but an exact repeat still is not.
        REQUIRE(!DM_AddBan(&scoped,"203.0.113.7 scope control"));

        // A malformed or unknown scope is an error, never a silent widening to
        // "both": a typo must not start blocking the port the operator spared.
        REQUIRE(!DM_AddBan(&scoped,"198.18.0.1 scope"));
        REQUIRE(!DM_AddBan(&scoped,"198.18.0.2 scope Control"));
        REQUIRE(!DM_AddBan(&scoped,"198.18.0.3 scope gameport"));
        REQUIRE(!DM_AddBan(&scoped,"198.18.0.4 scope relay extra"));
        REQUIRE(!DM_AddBan(&scoped,"198.18.0.5 scope relay until 2030-01-01T00:00:00Z"));
        REQUIRE(!DM_AddBan(&scoped,"198.18.0.6 until 2030-01-01T00:00:00Z scope"));
        REQUIRE(!DM_AddBan(&scoped,"198.18.0.7 until 2030-01-01T00:00:00Zscope relay"));

        // The allow list shares the grammar, and the per-port count is what the
        // startup check uses to avoid locking a served port out.
        memset(&scoped,0,sizeof(scoped));
        REQUIRE(DM_AddAllowed(&scoped,"203.0.113.7 scope relay"));
        REQUIRE(DM_AddAllowed(&scoped,"198.51.100.9"));
        REQUIRE(DM_AllowedRules(&scoped,DM_SCOPE_RELAY,0)==2);   // relay rule + unscoped
        REQUIRE(DM_AllowedRules(&scoped,DM_SCOPE_CONTROL,0)==1); // only the unscoped one
        REQUIRE(DM_IsAllowed(&scoped,peer,0,DM_SCOPE_RELAY));
        REQUIRE(!DM_IsAllowed(&scoped,peer,0,DM_SCOPE_CONTROL));
        REQUIRE(DM_IsAllowed(&scoped,other,0,DM_SCOPE_CONTROL));
        REQUIRE(DM_IsAllowed(&scoped,other,0,DM_SCOPE_RELAY));
        // A temporary allow must stop permitting; ignoring the deadline would
        // leave a permit that never ends.
        REQUIRE(DM_AddAllowed(&scoped,"192.0.2.5 until 2030-01-01T00:00:00Z"));
        {
            unsigned char timed[4]={192,0,2,5};
            REQUIRE(DM_IsAllowed(&scoped,timed,scoped.allowed[2].expires-1,DM_SCOPE_CONTROL));
            REQUIRE(!DM_IsAllowed(&scoped,timed,scoped.allowed[2].expires,DM_SCOPE_CONTROL));
        }
        // Expired permits are pruned like expired bans, or 64 dead lines would
        // make the cap reject new rules.
        REQUIRE(DM_AllowedRules(&scoped,DM_SCOPE_CONTROL,scoped.allowed[2].expires)==1);
        REQUIRE(DM_PruneAllowed(&scoped,scoped.allowed[2].expires-1)==0);
        REQUIRE(DM_PruneAllowed(&scoped,scoped.allowed[2].expires)==1);
        REQUIRE(scoped.allowed_count==2);
    }

    puts("Master model: schema, bounds, lease/expiry, source/token auth, capacity, header validation, ban rules and rule scope OK");
    return 0;
}
