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
// Days since 1970-01-01 for a civil date. Straight from Howard Hinnant's
// algorithm: exact for every year we accept, no libc time zone or locale
// dependency, so the ban file means the same thing on every host.
static int64_t DaysFromCivil(int year,unsigned int month,unsigned int day)
{
    int64_t y=(int64_t)year;
    unsigned int m=month;
    y-=m<=2;
    {
        int64_t era=(y>=0?y:y-399)/400;
        unsigned int yoe=(unsigned int)(y-era*400);
        unsigned int doy=(153*(m+(m>2?-3:9))+2)/5+day-1;
        unsigned int doe=yoe*365+yoe/4-yoe/100+doy;
        return era*146097+(int64_t)doe-719468;
    }
}
static int Two(const char **p)
{
    if((*p)[0]<'0' || (*p)[0]>'9' || (*p)[1]<'0' || (*p)[1]>'9')return -1;
    { int v=((*p)[0]-'0')*10+((*p)[1]-'0'); *p+=2; return v; }
}
// Parse "YYYY-MM-DDTHH:MM:SSZ" (UTC only, trailing Z required) and advance past
// it. Rejects anything else rather than guessing a local time zone.
static int ParseUtc(const char **text,uint64_t *out)
{
    const char *p=*text;
    int year,month,day,hour,minute,second;
    int64_t days;

    // Four-digit year, then the two-digit fields.
    if(p[0]<'0'||p[0]>'9'||p[1]<'0'||p[1]>'9'||p[2]<'0'||p[2]>'9'||p[3]<'0'||p[3]>'9')return 0;
    year=(p[0]-'0')*1000+(p[1]-'0')*100+(p[2]-'0')*10+(p[3]-'0');
    p+=4;
    if(year<1970)return 0;   // before the epoch would overflow the sign
    if(*p++!='-')return 0;
    month=Two(&p); if(month<1||month>12)return 0; if(*p++!='-')return 0;
    day=Two(&p); if(day<1||day>31)return 0;
    if(*p!='T' && *p!=' ')return 0;
    ++p;
    hour=Two(&p); if(hour<0||hour>23)return 0; if(*p++!=':')return 0;
    minute=Two(&p); if(minute<0||minute>59)return 0; if(*p++!=':')return 0;
    second=Two(&p); if(second<0||second>60)return 0;
    if(*p!='Z')return 0;
    ++p;

    days=DaysFromCivil(year,(unsigned int)month,(unsigned int)day);
    *out=(uint64_t)(days*86400+(int64_t)hour*3600+(int64_t)minute*60+second);
    *text=p;
    return 1;
}

// Optional trailing "scope control|relay|both". The word must be the whole
// remainder of the line, so a typo cannot be read as a rule that silently
// applies everywhere. Absent scope keeps DM_SCOPE_BOTH.
static int ParseScope(const char *text,unsigned int *scope)
{
    if(!strcmp(text,"both"))*scope=DM_SCOPE_BOTH;
    else if(!strcmp(text,"control"))*scope=DM_SCOPE_CONTROL;
    else if(!strcmp(text,"relay"))*scope=DM_SCOPE_RELAY;
    else return 0;
    return 1;
}
// "scope <role>", advancing past it.
static int ParseScopeSuffix(const char **p,unsigned int *scope)
{
    if(strncmp(*p,"scope ",6))return 0;
    *p+=6;
    return ParseScope(*p,scope);
}

int DM_ParseRule(dm_ban_t *rules,unsigned int *count,const char *text)
{
    dm_ban_t rule;
    const char *p=text;
    uint32_t network,mask;
    unsigned int i,digits=0;
    memset(&rule,0,sizeof(rule));
    if(*count>=DM_MAX_BANS || !IPv4(&p,rule.network))return 0;
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
        // avoids the trap of a "single IP" rule silently covering a range.
        rule.prefix=32;
    }
    // Optional " until <ISO-8601 UTC>", then optional " scope control|relay|both".
    // Scope is also accepted on its own, because "ban this address, relay only"
    // is the common case and forcing a fake deadline to express it would be
    // absurd.
    if(*p)
    {
        if(*p++!=' ')return 0;
        if(!strncmp(p,"until ",6))
        {
            p+=6;
            if(!ParseUtc(&p,&rule.expires))return 0;
            if(*p)
            {
                if(*p++!=' ')return 0;
                if(!ParseScopeSuffix(&p,&rule.scope))return 0;
            }
        }
        else if(!ParseScopeSuffix(&p,&rule.scope))return 0;
    }
    mask=PrefixMask(rule.prefix);network=DM_Get32(rule.network);
    // Reject non-canonical CIDR rather than widening it: 10.0.0.5/24 would
    // otherwise quietly cover 10.0.0.0/24, which is a much larger blast radius
    // than the operator wrote.
    if((network&mask)!=network)return 0;
    for(i=0;i<*count;++i)
        if(rules[i].prefix==rule.prefix && !memcmp(rules[i].network,rule.network,4)
           && rules[i].expires==rule.expires && rules[i].scope==rule.scope)return 0;
    rules[(*count)++]=rule;return 1;
}
// A rule governs a port when it is unscoped or scoped to that port.
static int Applies(const dm_ban_t *rule,unsigned int scope)
{ return rule->scope==DM_SCOPE_BOTH || rule->scope==scope; }
int DM_AddBan(dm_directory_t *d,const char *text){return DM_ParseRule(d->bans,&d->ban_count,text);}
void DM_ClearBans(dm_directory_t *d){d->ban_count=0;}
int DM_IsBanned(const dm_directory_t *d,const unsigned char source[4],uint64_t now,unsigned int scope)
{
    unsigned int i;
    for(i=0;i<d->ban_count;++i)
    {
        const dm_ban_t *rule=&d->bans[i];
        // An expired temporary ban is ignored even before pruning runs, so a
        // stale rule can never keep blocking after its time.
        if(rule->expires && now>=rule->expires)continue;
        if(!Applies(rule,scope))continue;
        if((DM_Get32(source)&PrefixMask(rule->prefix))==DM_Get32(rule->network))return 1;
    }
    return 0;
}
static unsigned int PruneRules(dm_ban_t *rules,unsigned int *count,uint64_t now)
{
    unsigned int i,kept=0,removed=0;
    for(i=0;i<*count;++i)
    {
        if(rules[i].expires && now>=rules[i].expires) { ++removed; continue; }
        rules[kept++]=rules[i];
    }
    *count=kept;
    return removed;
}
unsigned int DM_PruneBans(dm_directory_t *d,uint64_t now)
{ return PruneRules(d->bans,&d->ban_count,now); }
// Allow rules share the grammar, so they can carry "until" too. Once an expired
// permit stops applying it is dead weight, and 64 dead lines would make the cap
// reject new rules, so they are pruned on the same schedule.
unsigned int DM_PruneAllowed(dm_directory_t *d,uint64_t now)
{ return PruneRules(d->allowed,&d->allowed_count,now); }
int DM_AddAllowed(dm_directory_t *d,const char *text){return DM_ParseRule(d->allowed,&d->allowed_count,text);}
void DM_ClearAllowed(dm_directory_t *d){d->allowed_count=0;}
int DM_IsAllowed(const dm_directory_t *d,const unsigned char source[4],uint64_t now,unsigned int scope)
{
    unsigned int i;
    for(i=0;i<d->allowed_count;++i)
    {
        const dm_ban_t *rule=&d->allowed[i];
        // Expiry is honoured here too: the grammar is shared with bans, so an
        // allow rule can carry "until", and silently ignoring it would mean a
        // temporary permit never ends. `now` is Unix seconds, 0 = no check.
        if(now && rule->expires && now>=rule->expires)continue;
        if(!Applies(rule,scope))continue;
        if((DM_Get32(source)&PrefixMask(rule->prefix))==DM_Get32(rule->network))return 1;
    }
    return 0;
}
unsigned int DM_AllowedRules(const dm_directory_t *d,unsigned int scope,uint64_t now)
{
    unsigned int i,count=0;
    for(i=0;i<d->allowed_count;++i)
    {
        // An expired permit is not a permit: counting it here would let the
        // startup check pass while the port is in fact locked.
        if(now && d->allowed[i].expires && now>=d->allowed[i].expires)continue;
        if(Applies(&d->allowed[i],scope))++count;
    }
    return count;
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
