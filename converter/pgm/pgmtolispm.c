/* pgmtolispm.c - read a pgm and write a file acceptable to the
** tv:read-bit-array-file function of TI Explorer and Symbolics Lisp Machines.
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
**   treat this as a graymap instead.  To convert a color image to Lispm
**   format, you must convert it to a pgm, and hand-edit a color map...  Ick.
**
** Feb 2010 afu
** Added dimension check to prevent short int from overflowing
** Changed code style (ANSI-style function definitions, etc.)
*/

#include "pm_c_util.h"
#include "pm.h"
#include "pgm.h"

#define LISPM_MAGIC  "This is a BitMap file"
#define INT16MAX 32767


static unsigned int item;
static unsigned int bitsperitem, maxbitsperitem, bitshift;

static unsigned int
wordSzFmDepth(unsigned int const depth) {

    /* Lispm architecture specific - if a bitmap is written out with a depth
       of 5, it really has a depth of 8, and is stored that way in the file.
    */

    unsigned int const wordSz =
        depth ==  1 ?  1 :
        depth ==  2 ?  2 :
        depth <=  4 ?  4 :
        depth <=  8 ?  8 :
        depth <= 16 ? 16 :
        depth <= 32 ? 32 :
        0;

    if (depth > 32)
        pm_error("depth is %u; must be in the range 1-32", depth);

    return wordSz;
}



static void
putinit(unsigned int const cols,
        unsigned int const rows,
        unsigned int const depth) {

    unsigned int const cols32 = ROUNDUP(cols, 32);

    unsigned int i;

    /* Lispms are able to write bit files that are not a multiple of 32 pixels
       wide, but we don't.  This should be ok, since bit arrays which are not
       a multiple of 32 pixels wide are pretty useless on a lispm (can't hand
       them to bitblt).
    */

    if (rows > INT16MAX || cols > INT16MAX || cols32 > INT16MAX)
        pm_error("Input image is too large.");

    printf(LISPM_MAGIC);

    pm_writelittleshort(stdout, cols);
    pm_writelittleshort(stdout, rows);
    pm_writelittleshort(stdout, cols32);
    putchar(depth & 0xFF);

    for (i = 0; i < 9; ++i)
        putchar(0);   /* pad bytes */

    item           = 0;
    bitsperitem    = 0;
    maxbitsperitem = wordSzFmDepth(depth);
    bitshift       = 0;
}



static void
putitem(void) {

    pm_writelittlelong(stdout, ~item);

    item        = 0;
    bitsperitem = 0;
    bitshift    = 0;
}



static void
putval(gray const b) {

    if (bitsperitem == 32)
        putitem();

    item        = item | (b << bitshift);
    bitsperitem = bitsperitem + maxbitsperitem;
    bitshift    = bitshift + maxbitsperitem;
}



static void
putrest(void) {

    if (bitsperitem > 0)
        putitem();
}



int
main(int argc, const char * argv[]) {

    FILE * ifP;
    gray * grayrow;
    int rows;
    int cols;
    unsigned int depth;
    int format;
    unsigned int row;
    gray maxval;
    const char * inputFile;

    pm_proginit(&argc, argv);

    if (argc-1 < 1)
        inputFile = "-";
    else {
        inputFile = argv[1];

        if (argc-1 > 2)
            pm_error("Too many arguments.  The only argument is the optional "
                     "input file name");
    }

    ifP = pm_openr(inputFile);

    pgm_readpgminit(ifP, &cols, &rows, &maxval, &format);

    depth = pm_maxvaltobits(maxval);

    putinit(cols, rows, depth);

    grayrow = pgm_allocrow(cols);

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        pgm_readpgmrow(ifP, grayrow, cols, maxval, format);

        for (col = 0; col < cols; ++col)
            putval(grayrow[col]);

        /* Pad the row to a multiple of 32 pixels */

        for (; col < ROUNDUP(cols, 32); ++col)
            putval(0);
    }

    pgm_freerow(grayrow);

    pm_close(ifP);

    putrest();

    return 0;
}



