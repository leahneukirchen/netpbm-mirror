/* lispmtopgm.c - read a file written by the tv:write-bit-array-file function
** of TI Explorer and Symbolics Lisp Machines, and write a PGM.
**
** Written by Jamie Zawinski based on code (C) 1988 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
**   When one writes a multi-plane bitmap with tv:write-bit-array-file, it is
**   usually a color image; but a color map is not written in the file, so we
**   treat this as a graymap instead.  Since the pgm reader can also read pbms,
**   this doesn't matter if you're using only single plane images.
*/

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "nstring.h"
#include "pgm.h"

#define LISPM_MAGIC  "This is a BitMap file"



static long item, bitmask;
static unsigned int bitsperitem, maxbitsperitem, bitshift;

static unsigned int
wordSizeFmDepth(unsigned int const depth) {
/*----------------------------------------------------------------------------
  Lispm architecture specific - if a bitmap is written out with a depth of 5,
  it really has a depth of 8, and is stored that way in the file.
-----------------------------------------------------------------------------*/
    if (depth==0 || depth==1) return  1;
    else if (depth ==  2)     return  2;
    else if (depth <=  4)     return  4;
    else if (depth <=  8)     return  8;
    else if (depth <= 16)     return 16;
    else if (depth <= 32)     return 32;
    else {
        pm_error("depth was %u, which is not in the range 1-32.", depth);
        assert(false);
    }
}



static void
getinit(FILE *         const ifP,
        unsigned int * const colsP,
        unsigned int * const rowsP,
        unsigned int * const depthP,
        unsigned int * const padrightP) {

    short cols, rows, cols32;
    char magic[sizeof(LISPM_MAGIC)];
    unsigned int i;

    for (i = 0; i < sizeof(magic)-1; ++i)
        magic[i] = getc(ifP);

    magic[i]='\0';

    if (!streq(LISPM_MAGIC, magic))
        pm_error("bad id string in Lispm file");

    pm_readlittleshort(ifP, &cols);
    pm_readlittleshort(ifP, &rows);
    pm_readlittleshort(ifP, &cols32);

    *colsP = cols;
    *rowsP = rows;

    *depthP = getc(ifP);

    if (*depthP == 0)
        *depthP = 1;    /* very old file */

    assert(*colsP < UINT_MAX - 31);

    *padrightP = ROUNDUP(*colsP, 32) - *colsP;

# if 0
    if (*colsP != (cols32 - *padrightP)) {
        pm_message("inconsistent input: "
                   "Width and Width(mod32) fields don't agree" );
        *padrightP = cols32 - *colsP;   /*    hmmmm....   */

        /* This is a dilemma.  Usually the output is rounded up to mod32, but
           not always.  For the Lispm code to not round up, the array size
           must be the same size as the portion being written - that is, the
           array itself must be an odd size, not just the selected portion.
           Since arrays that are odd sizes can't be handed to bitblt, such
           arrays are probably not image data - so punt on it for now.

           Also, the lispm code for saving bitmaps has a bug, in that if you
           are writing a bitmap which is not mod32 across, the file may be up
           to 7 bits too short!  They round down instead of up.

            The code in 'pgmtolispm.c' always rounds up to mod32, which is
            totally reasonable.
       */
    }
#endif
    bitsperitem = 0;
    maxbitsperitem = wordSizeFmDepth(*depthP);
    bitmask = (1 << maxbitsperitem) - 1;     /* for depth=3, mask=00000111 */

    for (i = 0; i < 9; ++i)
        getc(ifP);   /* discard bytes reserved for future use */
}



static unsigned int
getval(FILE * const ifP) {

    unsigned int b;

    if (bitsperitem == 0) {
        pm_readlittlelong(ifP, &item);
        bitsperitem = 32;
        bitshift = 0;
        item = ~item;
    }
    b           = ((item >> bitshift ) & bitmask);
    bitsperitem = bitsperitem - maxbitsperitem;
    bitshift    = bitshift + maxbitsperitem;
    return b;
}



int
main(int argc, const char ** argv) {

    FILE * ifP;
    gray * grayrow;
    unsigned int rows, cols, depth, padright;
    unsigned int row;
    gray maxval;


    pm_proginit(&argc, argv);

    if (argc-1 > 1)
        pm_error("Too many arguments.  The only possible argument is the "
                 "input file name");

    if (argc-1 == 1)
        ifP = pm_openr(argv[1]);
    else
        ifP = stdin;

    getinit(ifP, &cols, &rows, &depth, &padright);

    if (depth > 16)
        pm_error("Invalid depth (%u bits).  Maximum is 15", depth);

    maxval = (1 << depth);

    pgm_writepgminit(stdout, cols, rows, maxval, 0);

    grayrow = pgm_allocrow(ROUNDUP(cols, 8));

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        for (col = 0; col < cols; ++col)
            grayrow[col] = getval(ifP);

        pgm_writepgmrow(stdout, grayrow, cols, maxval, 0);
    }
    pm_close(ifP);
    pm_close(stdout);
    exit(0);
}



