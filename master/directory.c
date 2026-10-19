// GPL-2.0-or-later; see LICENSE.
#include "directory.h"

static int Text(const unsigned char *p, unsigned int size)
{
    unsigned int i=0;
    // v1 is printable ASCII; no control sequences, URLs or implicit Unicode.
    while(i<size && p[i]) { if(p[i]<32 || p[i]>126)return 0; ++i; }
    if(!i || i==size || p[0]==' ' || p[i-1]==' ')return 0;
    while(i<size)if(p[i++])return 0;
    return 1;
}
int DM_ValidInfo(const unsigned char *p)
{
    unsigned int map,i;
    if(!Text(p,64) || !Text(p+116,8) || !DM_Get16(p+124) || p[126]<1 || p[126]>5
        || p[127]>31 || p[128]!=DM_GAME_PROTOCOL || p[129] || p[130] || p[131])return 0;
    for(i=104;i<116;++i)if(p[i])return 0;
    if(p[116]=='M' && p[117]=='A' && p[118]=='P' && p[119]>='0' && p[119]<='3'
        && p[120]>='0' && p[120]<='9' && !p[121])
    { map=(unsigned int)(p[119]-'0')*10+p[120]-'0'; return map>=1 && map<=32; }
    return p[116]=='E' && p[117]>='1' && p[117]<='4' && p[118]=='M' && p[119]>='1' && p[119]<='9' && !p[120];
}
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
    dm_entry_t *free_entry=NULL;
    if(!DM_ValidInfo(info))return DM_BAD_REQUEST;
    DM_Prune(d,now);
    for(i=0;i<DM_MAX_SERVERS;++i)
    {
        dm_entry_t *entry=&d->entries[i];
        if(!entry->active) { if(!free_entry)free_entry=entry; continue; }
        if(!memcmp(entry->id,entropy,16))return DM_CONFLICT;
        if(!memcmp(entry->host,host,4))
        {
            ++count;
            if(DM_Get16(entry->info+124)==DM_Get16(info+124))return DM_CONFLICT;
        }
    }
    if(!free_entry || count>=DM_MAX_IP_SERVERS)return DM_CAPACITY;
    free_entry->active=1; memcpy(free_entry->host,host,4); memcpy(free_entry->info,info,DM_INFO);
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
        memset(row,0,DM_ROW); memcpy(row,entry->id,16); memcpy(row+16,entry->host,4);
        DM_Put32(row+20,(uint32_t)((entry->expires-now)/1000)); memcpy(row+24,entry->info,DM_INFO);
        ++count;
    }
    DM_Put32(reply,count);
    return 4+count*DM_ROW;
}
