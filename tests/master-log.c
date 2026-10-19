// Exercise rotation and suppression with fake monotonic time, no network.
// GPL-2.0-or-later; see LICENSE.
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define MKDIR(path) mkdir(path,0700)
#endif
#define REQUIRE(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static long Size(const char *path)
{
    FILE *f=fopen(path,"rb"); long size;
    REQUIRE(f); REQUIRE(!fseek(f,0,SEEK_END)); size=ftell(f); fclose(f); return size;
}
int main(void)
{
    unsigned int i;
    unsigned char host[4]={127,0,0,1};
    char text[4097]; FILE *f; size_t n;
    REQUIRE(!ML_Open("missing/log.txt",4096));
    REQUIRE(ML_Open("events.log",4096));
    for(i=0;i<100;++i)ML_Event(ML_HEARTBEAT,host,2342,1000);
    ML_Flush(6000,0); ML_Close();
    f=fopen("events.log","rb"); REQUIRE(f); n=fread(text,1,sizeof(text)-1,f); text[n]=0; fclose(f);
    REQUIRE(strstr(text,"suppressed=96")); REQUIRE(n<1024);
    REQUIRE(ML_Open("events.log",4096));
    for(i=0;i<250;++i)ML_Event(ML_REGISTER,host,2342,(uint64_t)i*5000+10000);
    ML_Close();
    REQUIRE(Size("events.log")<=4096); REQUIRE(Size("events.log.1")<=4096);
    // A backup path that's a directory prevents rotation. Report once and keep
    // console output rather than deleting the directory or growing the file.
    REQUIRE(!MKDIR("blocked.log.1"));
    REQUIRE(ML_Open("blocked.log",4096));
    for(i=0;i<100;++i)ML_Message("rotation-test: a bounded fixed event message, no user-controlled data");
    ML_Close(); REQUIRE(Size("blocked.log")<=4096);
    puts("Master log: bounded rotation and repeated-event suppression OK");
    return 0;
}
