/* pbmmask.c - create a mask bitmap from a portable bitmap
**
** Copyright (C) 1989, 1991 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdbool.h>
#include <assert.h>

#include "pbm.h"
#include "shhopt.h"
#include "mallocvar.h"

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* File name of input file */
    unsigned int expand;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo *  const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optStruct3 opt;  /* set by OPTENT3 */
    optEntry * option_def;
    unsigned int option_def_index;

    MALLOCARRAY(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "expand",          OPT_FLAG, NULL, &cmdlineP->expand, 0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = true;  /* We sort of allow negative numbers as parms */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    free(option_def);

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileName = argv[1];
    else
        pm_error("You specified too many arguments (%u).  The only "
                 "possible argument is the optional input file specification.",
                 argc-1);
}



static short * fcols;
static short * frows;
static int fstacksize = 0;
static int fstackp = 0;



static void
clearMask(bit ** const mask,
          unsigned int const cols,
          unsigned int const rows) {

    /* Clear out the mask. */
    unsigned int row;

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        for (col = 0; col < cols; ++col)
            mask[row][col] = PBM_BLACK;
    }
}



static bit
backcolorFmImage(bit **       const bits,
                 unsigned int const cols,
                 unsigned int const rows) {

    /* Figure out the background color, by counting along the edge. */

    unsigned int row;
    unsigned int col;
    unsigned int wcount;

    assert(cols > 0); assert(rows > 0);

    wcount = 0;
    for (row = 0; row < rows; ++row) {
        if (bits[row][0] == PBM_WHITE)
            ++wcount;
        if (bits[row][cols - 1] == PBM_WHITE)
            ++wcount;
    }
    for (col = 1; col < cols - 1; ++col) {
        if (bits[0][col] == PBM_WHITE)
            ++wcount;
        if (bits[rows - 1][col] == PBM_WHITE)
            ++wcount;
    }

    return (wcount >= rows + cols - 2) ? PBM_WHITE : PBM_BLACK;
}



static void
addflood(bit **       const bits,
         bit **       const mask,
         unsigned int const col,
         unsigned int const row,
         bit          const backcolor) {

    if (bits[row][col] == backcolor && mask[row][col] == PBM_BLACK) {
        if (fstackp >= fstacksize) {
            if (fstacksize == 0) {
                fstacksize = 1000;
                MALLOCARRAY(fcols, fstacksize);
                MALLOCARRAY(frows, fstacksize);
                if (fcols == NULL || frows == NULL)
                    pm_error("out of memory");
            } else {
                fstacksize *= 2;
                fcols = (short*) realloc(
                    (char*) fcols, fstacksize * sizeof(short));
                frows = (short*) realloc(
                    (char*) frows, fstacksize * sizeof(short));
                if (fcols == (short*) 0 || frows == (short*)0)
                    pm_error("out of memory");
            }
        }
        fcols[fstackp] = col;
        frows[fstackp] = row;
        ++fstackp;
    }
}



static void
floodEdge(bit **       const bits,
          unsigned int const cols,
          unsigned int const rows,
          bit          const backcolor,
          bit **       const mask) {

    int col;
    int row;

    /* Flood the entire edge.  Probably the first call will be enough, but
       might as well be sure.
    */
    assert(cols > 0); assert(rows > 0);

    for (col = cols - 3; col >= 2; col -= 2) {
        addflood(bits, mask, col, rows - 1, backcolor);
        addflood(bits, mask, col, 0, backcolor);
    }
    for (row = rows - 1; row >= 0; row -= 2) {
        addflood(bits, mask, cols - 1, row, backcolor);
        addflood(bits, mask, 0, row, backcolor);
    }
}



static void
flood(bit **       const bits,
      unsigned int const cols,
      unsigned int const rows,
      bit          const backcolor,
      bit **       const mask) {

    assert(cols > 0); assert(rows > 0);

    floodEdge(bits, cols, rows, backcolor, mask);

    while (fstackp > 0) {
        int col, row;
        --fstackp;
        col = fcols[fstackp];
        row = frows[fstackp];
        if (bits[row][col] == backcolor && mask[row][col] == PBM_BLACK) {
            int c;
            mask[row][col] = PBM_WHITE;
            if (row - 1 >= 0)
                addflood(bits, mask, col, row - 1, backcolor);
            if (row + 1 < rows)
                addflood(bits, mask, col, row + 1, backcolor);
            for (c = col + 1; c < cols; ++c) {
                if (bits[row][c] == backcolor && mask[row][c] == PBM_BLACK) {
                    mask[row][c] = PBM_WHITE;
                    if (row - 1 >= 0 &&
                        (bits[row - 1][c - 1] != backcolor ||
                         mask[row - 1][c - 1] != PBM_BLACK))
                        addflood(bits, mask, c, row - 1, backcolor);
                    if (row + 1 < rows &&
                         (bits[row + 1][c - 1] != backcolor ||
                          mask[row + 1][c - 1] != PBM_BLACK))
                        addflood(bits, mask, c, row + 1, backcolor);
                }
                else
                    break;
            }
            for (c = col - 1; c >= 0; --c) {
                if (bits[row][c] == backcolor && mask[row][c] == PBM_BLACK) {
                    mask[row][c] = PBM_WHITE;
                    if (row - 1 >= 0 &&
                        (bits[row - 1][c + 1] != backcolor ||
                         mask[row - 1][c + 1] != PBM_BLACK))
                        addflood(bits, mask, c, row - 1, backcolor);
                    if (row + 1 < rows &&
                         (bits[row + 1][c + 1] != backcolor ||
                          mask[row + 1][c + 1] != PBM_BLACK))
                        addflood(bits, mask, c, row + 1, backcolor);
                } else
                    break;
            }
        }
    }
}



static bit **
expandedByOnePixel(bit **       const mask,
                   unsigned int const cols,
                   unsigned int const rows) {

    /* Expand by one pixel. */

    bit ** const emask = pbm_allocarray(cols, rows);

    unsigned int row;

    for (row = 0; row < rows; ++row) {
        unsigned int col;
        for (col = 0; col < cols; ++col)
            if (mask[row][col] == PBM_BLACK)
                emask[row][col] = PBM_BLACK;
            else {
                unsigned int srow;

                emask[row][col] = PBM_WHITE;

                for (srow = row - 1; srow <= row + 1; ++srow) {
                    unsigned int scol;

                    for (scol = col - 1; scol <= col + 1; ++scol) {
                        if (srow >= 0 && srow < rows &&
                            scol >= 0 && scol < cols &&
                            mask[srow][scol] == PBM_BLACK) {

                            emask[row][col] = PBM_BLACK;
                            break;
                        }
                    }
                }
            }
    }
    return emask;
}



static void
pbmmask(FILE *             const ifP,
        FILE *             const ofP,
        struct CmdlineInfo const cmdline) {

    int cols, rows;
    bit ** mask;
    bit ** bits;
    bit backcolor;

    bits = pbm_readpbm(ifP, &cols, &rows);

    if (cols == 0 || rows == 0)
        pm_error("Image contains no pixels, so there is no such thing "
                 "as background and foreground");

    mask = pbm_allocarray(cols, rows);

    clearMask(mask, cols, rows);

    backcolor = backcolorFmImage(bits, cols, rows);

    flood(bits, cols, rows, backcolor, mask);

    if (!cmdline.expand) {
        /* Done. */
        pbm_writepbm(stdout, mask, cols, rows, 0);
    } else {
        bit ** const emask = expandedByOnePixel(mask, cols, rows);

        pbm_writepbm(stdout, emask, cols, rows, 0);

        pbm_freearray(emask, rows);
    }
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;
    FILE * ifP;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    pbmmask(ifP, stdout, cmdline);

    pm_close(ifP);
    pm_close(stdout);

    return 0;
}


