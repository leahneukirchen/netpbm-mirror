#define _XOPEN_SOURCE   /* Make sure unistd.h contains swab() */
#include <unistd.h>

#include "netpbm/pm.h"
#include "netpbm/mallocvar.h"

#include "global_variables.h"
#include "util.h"
#include "stdio_nofail.h"

#ifndef LONG_BITS
#define LONG_BITS (8 * sizeof(long))
#endif
/*
   Get a 2-byte integer, making no assumptions about CPU byte order.
   Nor should we assume that the compiler evaluates left-to-right.
 */
unsigned short
get2(FILE * const ifp)
{
    unsigned char a, b;

    a = fgetc_nofail(ifp);  b = fgetc_nofail(ifp);

    if (order == 0x4949)      /* "II" means little-endian */
        return a | b << 8;
    else              /* "MM" means big-endian */
        return a << 8 | b;
}



/*
   Same for a 4-byte integer.
 */
int
get4(FILE * const ifp)
{
    unsigned char a, b, c, d;

    a = fgetc_nofail(ifp);  b = fgetc_nofail(ifp);
    c = fgetc_nofail(ifp);  d = fgetc_nofail(ifp);

    if (order == 0x4949)
        return a | b << 8 | c << 16 | d << 24;
    else
        return a << 24 | b << 16 | c << 8 | d;
}



/*
   Faster than calling get2() multiple times.
 */
void
read_shorts (FILE * const ifp, unsigned short *pixel, int count)
{
    unsigned short * buffer;

    MALLOCARRAY(buffer, count);

    if (!buffer)
        pm_error("Failed to allocate a buffer for reading %u short "
                 "integers from file", count);
    else {
        fread_nofail(buffer, 2, count, ifp);

        if ((order == 0x4949) == (BYTE_ORDER == BIG_ENDIAN))
            swab(buffer, pixel, count*2);

        free(buffer);
    }
}



/*
   getbits(-1) initializes the buffer
   getbits(n) where 0 <= n <= 25 returns an n-bit integer
 */
unsigned
getbits (FILE * const ifp, int nbits)
{
    static unsigned long bitbuf=0;
    static int vbits=0;
    unsigned c, ret;

    if (nbits == 0) return 0;
    if (nbits == -1)
        ret = bitbuf = vbits = 0;
    else {
        ret = bitbuf << (LONG_BITS - vbits) >> (LONG_BITS - nbits);
        vbits -= nbits;
    }
    while (vbits < LONG_BITS - 7) {
        c = fgetc_nofail(ifp);
        bitbuf = (bitbuf << 8) + c;
        if (c == 0xff && zero_after_ff)
            fgetc_nofail(ifp);
        vbits += 8;
    }
    return ret;
}



