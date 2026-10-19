// Production zone allocator tests. GPL-2.0-or-later; see LICENSE.
#include "doomtype.h"
#include "z_zone.h"
#include "i_system.h"
#include <stdlib.h>
#include <stdarg.h>

static byte *zone;
byte *I_ZoneBase(int *size)
{
    *size = 1024 * 1024;
    zone = malloc(*size);
    return zone;
}
void I_Error(char *format, ...)
{
    va_list args;
    va_start(args, format); vfprintf(stderr, format, args); va_end(args);
    exit(2);
}

int main(int argc, char **argv)
{
    volatile byte *a;
    byte *b, *c, *owner = NULL;
    Z_Init();
    a = Z_Malloc(17, PU_STATIC, NULL);
    if (argc > 1 && !strcmp(argv[1], "uaf"))
    {
        Z_Free((void *)a);
        return a[0]; // sanitizer probe: must fail
    }
    if (argc > 1 && !strcmp(argv[1], "overflow"))
    {
        a[17] = 1; // allocator alignment slack is poisoned
        return 0;
    }
    b = Z_Malloc(100, PU_CACHE, &owner);
    c = Z_Malloc(80, PU_LEVEL, NULL);
    if (owner != b) return 1;
    memset((void *)a, 1, 16); memset(b, 2, 100); memset(c, 3, 80);
    Z_Free(b);
    if (owner != NULL) return 1;
    Z_Free((void *)a); Z_Free(c); Z_CheckHeap();
    a = Z_Malloc(17, PU_STATIC, NULL);
    memset((void *)a, 0, 17); Z_Free((void *)a);
    b = Z_Malloc(80, PU_LEVEL, NULL); (void)b;
    c = Z_Malloc(80, PU_CACHE, &owner); (void)c;
    Z_FreeTags(PU_LEVEL, PU_CACHE); Z_CheckHeap();
    if (owner != NULL) return 1;
    free(zone);
    puts("Zone: allocation, owners, coalescing, tags and reuse OK");
    return 0;
}
