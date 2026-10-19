// Shared client input/output validation. GPL-2.0-or-later; see LICENSE.
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#define REQUIRE(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static uint64_t Now(void)
{
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return (uint64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;
#endif
}
int main(int argc,char **argv)
{
    char host[254]; unsigned port,i;
    unsigned char body[4+DM_ROW]={0};
    const char *bad[]={"", "https://example.com:25135", "example.com", "x:0", "x:65536", "x:-1", "-x:5", "x-:5", "a..b:5", "x:5/path", "x:5\n", "[::1]:5", "a b:5"};
    dm_client_query_t query;
    if(argc==3)
    {
        uint64_t now=Now();int result=0;
        MC_Init(&query);MC_Begin(&query,"127.0.0.1",(unsigned int)atoi(argv[1]),DM_LIST,NULL,0,now);
        while(!result)
        {
            result=MC_Poll(&query,Now());
#ifdef _WIN32
            Sleep(1);
#else
            {struct timespec delay={0,1000000};nanosleep(&delay,NULL);}
#endif
        }
        // Always print transport diagnostics before checking expectations, so
        // CI logs identify the failed native stage even on a success-case fail.
        {char diagnostic[1024];MC_Diagnostic(&query,diagnostic,sizeof(diagnostic));puts(diagnostic);fflush(stdout);}
        if(!strcmp(argv[2],"ok"))REQUIRE(result==1&&!query.status);
        else if(!strcmp(argv[2],"reject"))REQUIRE(result==1&&query.status);
        else REQUIRE(result<0);
        MC_Close(&query);puts("Master client wire probe OK");return 0;
    }
    REQUIRE(MC_Endpoint("play.dartpower.xyz:25135",host,&port)&&port==25135);
    REQUIRE(!strcmp(host,"play.dartpower.xyz"));
    REQUIRE(MC_Endpoint("127.0.0.1:1",host,&port)&&port==1);
    for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i)REQUIRE(!MC_Endpoint(bad[i],host,&port));
    REQUIRE(MC_ValidList(body,4)); DM_Put32(body,1); REQUIRE(!MC_ValidList(body,4));
    memcpy(body+28,"Session",7);memcpy(body+28+116,"MAP01",5);DM_Put16(body+28+124,2342);body[28+126]=3;body[28+128]=2;
    body[20]=127;body[23]=1;DM_Put32(body+24,60);
    REQUIRE(MC_ValidList(body,sizeof(body)));
    body[sizeof(body)-1]=1;REQUIRE(MC_ValidList(body,sizeof(body)));
    body[sizeof(body)-1]=2;REQUIRE(!MC_ValidList(body,sizeof(body)));body[sizeof(body)-1]=0;
    body[20]=224;REQUIRE(!MC_ValidList(body,sizeof(body)));
    MC_Init(&query); REQUIRE(MC_Begin(&query,"domain.invalid",25135,DM_LIST,NULL,0,1)<0);
    REQUIRE(MC_Begin(&query,"127.0.0.1",0,DM_LIST,NULL,0,1)<0);
    MC_Close(&query);
    puts("Master client: endpoint grammar, bounded list metadata, bad Begin inputs OK");return 0;
}
