// GPL-2.0-or-later; see LICENSE.
#include "directory.h"

static int IPv4(const char **text, unsigned char result[4])
{
    unsigned int i;
    const char *p=*text;
    for(i=0;i<4;++i)
    {
        unsigned int value=0,digits=0;
        while(*p>='0' && *p<='9')
        {value=value*10+(unsigned int)(*p++-'0');if(++digits>3 || value>255)return 0;}
        if(!digits)return 0;
        result[i]=(unsigned char)value;
        if(i<3 && *p++!='.')return 0;
    }
    *text=p;return 1;
}
static uint32_t PrefixMask(unsigned int prefix)
{ return prefix?UINT32_MAX<<(32-prefix):0; }
int DM_PublicIPv4(const unsigned char ip[4])
{
    return ip[0] && ip[0]<224 && ip[0]!=127 && ip[0]!=10
        && !(ip[0]==172&&ip[1]>=16&&ip[1]<=31) && !(ip[0]==192&&ip[1]==168)
        && !(ip[0]==169&&ip[1]==254) && !(ip[0]==100&&ip[1]>=64&&ip[1]<=127);
}
static int AddMap(dm_directory_t *d,const char *text,int dynamic)
{
    dm_address_map_t rule;
    const char *p=text;
    uint32_t network,mask;
    unsigned int i,digits=0,min_prefix;
    memset(&rule,0,sizeof(rule));
    if(d->address_map_count>=DM_MAX_ADDRESS_MAPS || !IPv4(&p,rule.network) || *p++!='/')return 0;
    while(*p>='0' && *p<='9')
    {rule.prefix=rule.prefix*10+(unsigned int)(*p++-'0');if(++digits>2 || rule.prefix>32)return 0;}
    if(!digits)return 0;
    if(dynamic){if(*p)return 0;}
    else if(*p++!='=' || !IPv4(&p,rule.advertised) || *p)return 0;
    // Only a bounded private/loopback source range may be overridden. An
    // arbitrary Internet client must never nominate a different listing IP.
    if(rule.network[0]==10 || rule.network[0]==127)min_prefix=8;
    else if(rule.network[0]==172 && rule.network[1]>=16 && rule.network[1]<=31)min_prefix=12;
    else if(rule.network[0]==192 && rule.network[1]==168)min_prefix=16;
    else return 0;
    if(rule.prefix<min_prefix)return 0;
    if(!dynamic&&!DM_PublicIPv4(rule.advertised))return 0;
    mask=PrefixMask(rule.prefix);network=DM_Get32(rule.network);
    if((network&mask)!=network)return 0; // reject noncanonical CIDR, no silent widening
    for(i=0;i<d->address_map_count;++i)
    {
        const dm_address_map_t *old=&d->address_maps[i];
        uint32_t common=PrefixMask(old->prefix<rule.prefix?old->prefix:rule.prefix);
        if((network&common)==(DM_Get32(old->network)&common))return 0;
    }
    rule.dynamic=dynamic;rule.enabled=!dynamic;d->address_maps[d->address_map_count++]=rule;return 1;
}
int DM_AddAddressMap(dm_directory_t *d,const char *text){return AddMap(d,text,0);}
int DM_AddHairpinSubnet(dm_directory_t *d,const char *text){return AddMap(d,text,1);}
int DM_AddBan(dm_directory_t *d,const char *text)
{
    dm_ban_t rule;
    const char *p=text;
    uint32_t network,mask;
    unsigned int i,digits=0;
    memset(&rule,0,sizeof(rule));
    if(d->ban_count>=DM_MAX_BANS || !IPv4(&p,rule.network))return 0;
    if(*p=='/')
    {
        ++p;
        while(*p>='0' && *p<='9')
        {rule.prefix=rule.prefix*10+(unsigned int)(*p++-'0');if(++digits>2 || rule.prefix>32)return 0;}
        if(!digits)return 0;
    }
    else
    {
        // A bare address means exactly that host. Being explicit about /32
        // avoids the trap of a "single IP" ban silently covering a range.
        rule.prefix=32;
    }
    if(*p)return 0;
    mask=PrefixMask(rule.prefix);network=DM_Get32(rule.network);
    // Reject non-canonical CIDR rather than widening it: 10.0.0.5/24 would
    // otherwise quietly ban 10.0.0.0/24, which is a much larger blast radius
    // than the operator wrote.
    if((network&mask)!=network)return 0;
    for(i=0;i<d->ban_count;++i)
        if(d->bans[i].prefix==rule.prefix && !memcmp(d->bans[i].network,rule.network,4))return 0;
    d->bans[d->ban_count++]=rule;return 1;
}
void DM_ClearBans(dm_directory_t *d){d->ban_count=0;}
int DM_IsBanned(const dm_directory_t *d,const unsigned char source[4])
{
    unsigned int i;
    for(i=0;i<d->ban_count;++i)
        if((DM_Get32(source)&PrefixMask(d->bans[i].prefix))==DM_Get32(d->bans[i].network))return 1;
    return 0;
}
int DM_MappingReady(const dm_directory_t *d,const unsigned char source[4])
{
    unsigned int i;
    for(i=0;i<d->address_map_count;++i)
    {
        const dm_address_map_t *rule=&d->address_maps[i];
        if((DM_Get32(source)&PrefixMask(rule->prefix))==DM_Get32(rule->network))return rule->enabled;
    }
    return 1;
}
void DM_AdvertisedAddress(const dm_directory_t *d,const unsigned char source[4],unsigned char output[4])
{
    unsigned int i;
    memcpy(output,source,4);
    for(i=0;i<d->address_map_count;++i)
    {
        const dm_address_map_t *rule=&d->address_maps[i];
        if(rule->enabled && (DM_Get32(source)&PrefixMask(rule->prefix))==DM_Get32(rule->network))
        {memcpy(output,rule->advertised,4);return;}
    }
}

static int Text(const unsigned char *p, unsigned int size)
{
    unsigned int i=0;
    // v1 is printable ASCII; no control sequences, URLs or implicit Unicode.
    while(i<size && p[i]) { if(p[i]<32 || p[i]>126)return 0; ++i; }
    if(!i || i==size || p[0]==' ' || p[i-1]==' ')return 0;
    while(i<size)if(p[i++])return 0;
    return 1;
}
const char *DM_InfoError(const unsigned char *p)
{
    unsigned int map,i;
    if(!Text(p,64))return "invalid-name-ASCII/padding";
    if(!Text(p+116,8))return "invalid-map-text/padding";
    if(!DM_Get16(p+124))return "invalid-game-port";
    if(p[126]<1||p[126]>5)return "invalid-skill";
    if(p[127]>31)return "unknown-rule-bits";
    if(p[128]!=DM_GAME_PROTOCOL)return "game-protocol-mismatch";
    if(p[129]||p[130]||p[131])return "nonzero-reserved";
    for(i=104;i<116;++i)if(p[i])return "nonzero-reserved";
    if(p[116]=='M' && p[117]=='A' && p[118]=='P' && p[119]>='0' && p[119]<='3'
        && p[120]>='0' && p[120]<='9' && !p[121])
    { map=(unsigned int)(p[119]-'0')*10+p[120]-'0'; return map>=1 && map<=32?NULL:"map-out-of-range"; }
    return p[116]=='E' && p[117]>='1' && p[117]<='4' && p[118]=='M' && p[119]>='1' && p[119]<='9' && !p[120]?NULL:"invalid-map-name/range";
}
int DM_ValidInfo(const unsigned char *p) { return DM_InfoError(p)==NULL; }
void DM_Prune(dm_directory_t *d, uint64_t now)
{
    unsigned int i;
    for(i=0;i<DM_MAX_SERVERS;++i)if(d->entries[i].active && d->entries[i].expires<=now)
        memset(&d->entries[i],0,sizeof(d->entries[i]));
}
unsigned int DM_Register(dm_directory_t *d, const unsigned char host[4], const unsigned char *info,
                         const unsigned char entropy[DM_AUTH], uint64_t now, unsigned char reply[DM_LEASE])
{
    unsigned int i, count=0;
    unsigned char advertised[4];
    dm_entry_t *free_entry=NULL;
    if(!DM_ValidInfo(info))return DM_BAD_REQUEST;
    if(!DM_MappingReady(d,host))return DM_CAPACITY;
    DM_Prune(d,now);
    DM_AdvertisedAddress(d,host,advertised);
    for(i=0;i<DM_MAX_SERVERS;++i)
    {
        dm_entry_t *entry=&d->entries[i];
        if(!entry->active) { if(!free_entry)free_entry=entry; continue; }
        if(!memcmp(entry->id,entropy,16))return DM_CONFLICT;
        // Do not allow a mapped LAN registration to shadow a different public
        // source (or vice versa) at the same advertised game endpoint.
        if(!memcmp(entry->advertised,advertised,4) && DM_Get16(entry->info+124)==DM_Get16(info+124))return DM_CONFLICT;
        if(!memcmp(entry->host,host,4))
        {
            ++count;
            if(DM_Get16(entry->info+124)==DM_Get16(info+124))return DM_CONFLICT;
        }
    }
    if(!free_entry || count>=DM_MAX_IP_SERVERS)return DM_CAPACITY;
    free_entry->active=1; memcpy(free_entry->host,host,4); memcpy(free_entry->info,info,DM_INFO);
    memcpy(free_entry->advertised,advertised,4);
    memcpy(free_entry->id,entropy,16); memcpy(free_entry->token,entropy+16,24);
    free_entry->expires=now+(uint64_t)d->lease_seconds*1000;
    memcpy(reply,entropy,DM_AUTH); DM_Put32(reply+40,d->lease_seconds); DM_Put32(reply+44,d->lease_seconds/3);
    return DM_OK;
}
unsigned int DM_Renew(dm_directory_t *d, const unsigned char host[4], const unsigned char auth[DM_AUTH],
                      uint64_t now, int remove, unsigned char reply[4])
{
    unsigned int i;
    DM_Prune(d,now);
    for(i=0;i<DM_MAX_SERVERS;++i)
    {
        dm_entry_t *entry=&d->entries[i];
        if(entry->active && !memcmp(entry->id,auth,16))
        {
            unsigned int mismatch=0,j;
            for(j=0;j<4;++j)mismatch|=entry->host[j]^host[j];
            for(j=0;j<24;++j)mismatch|=entry->token[j]^auth[16+j];
            if(mismatch)return DM_UNAUTHORIZED;
            if(remove)memset(entry,0,sizeof(*entry));
            else entry->expires=now+(uint64_t)d->lease_seconds*1000;
            DM_Put32(reply,remove?0:d->lease_seconds);
            return DM_OK;
        }
    }
    return DM_UNAUTHORIZED;
}
unsigned int DM_List(dm_directory_t *d, uint64_t now, unsigned char *reply)
{
    unsigned int i,count=0;
    DM_Prune(d,now);
    for(i=0;i<DM_MAX_SERVERS;++i)
    {
        const dm_entry_t *entry=&d->entries[i];
        unsigned char *row=reply+4+count*DM_ROW;
        if(!entry->active)continue;
        memset(row,0,DM_ROW); memcpy(row,entry->id,16); memcpy(row+16,entry->advertised,4);
        DM_Put32(row+20,(uint32_t)((entry->expires-now)/1000)); memcpy(row+24,entry->info,DM_INFO);
        DM_Put32(row+156,entry->relay_available?DM_RELAY_AVAILABLE:0);
        ++count;
    }
    DM_Put32(reply,count);
    return 4+count*DM_ROW;
}
