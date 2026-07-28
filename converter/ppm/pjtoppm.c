/* pjtoppm.c - convert an HP PainJetXL image to a PPM
**
** Copyright (C) 1990 by Christos Zoulas (christos@ee.cornell.edu)
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

#include "ppm.h"
#include "pm_c_util.h"
#include "mallocvar.h"



static unsigned int
uintProduct(unsigned int const multiplicand,
            unsigned int const multiplier) {

    if (UINT_MAX / multiplier < multiplicand)
        pm_error("Airthmetic overflow");

    return multiplicand * multiplier;
}



struct Raster {
    unsigned int planes;  /* constant */
    unsigned int allocRowCt;
    unsigned char ** rowplane;  /* malloc'ed */
    int * rowLen;  /* malloc'ed */
};



static void
rasterInit(struct Raster * const rasterP,
           unsigned int    const planes) {

    rasterP->planes = planes;

    rasterP->allocRowCt = 0;
    rasterP->rowplane = NULL;
    rasterP->rowLen = NULL;
}



static void
rasterRealloc(struct Raster * const rasterP,
              unsigned int    const minRowCt) {

    if (minRowCt > rasterP->allocRowCt) {
        unsigned int const newRowCt = MAX(minRowCt, rasterP->allocRowCt + 100);
        unsigned int const newRowplaneCt =
            uintProduct(newRowCt, rasterP->planes);

        REALLOCARRAY(rasterP->rowplane, newRowplaneCt);
        REALLOCARRAY(rasterP->rowLen,   newRowplaneCt);

        if (rasterP->rowplane == NULL || rasterP->rowLen == NULL)
            pm_error("Failed to allocate buffer space for %u rows", newRowCt);

        rasterP->allocRowCt = newRowCt;
    }
}



static void
rasterAddRows(struct Raster * const rasterP,
              unsigned int    const startRow,
              unsigned int    const endRow) {

    unsigned int row;

    rasterRealloc(rasterP, endRow);

    for (row = startRow; row < endRow; ++row) {
        unsigned int plane;

        for (plane = 0; plane < 3; ++plane) {
            unsigned int const rowplaneIndex = row * rasterP->planes + plane;
            rasterP->rowLen  [rowplaneIndex] = 0;
            rasterP->rowplane[rowplaneIndex] = NULL;
        }
    }
}



static void
rasterAllocRowplane(struct Raster * const rasterP,
                    unsigned int    const row,
                    unsigned int    const plane,
                    unsigned int    const length) {

    unsigned int const rowplaneIndex = row * rasterP->planes + plane;

    rasterP->rowLen[rowplaneIndex] = length;

    MALLOCARRAY(rasterP->rowplane[rowplaneIndex], length);

    if (rasterP->rowplane[rowplaneIndex] == NULL) {
        pm_error("out of memory allocating space for %u pixels for Plane %u "
                 "of Row %u", length, plane, row);
    }
}



static int
egetc(FILE * const ifP) {

    int c;

    c = fgetc(ifP);

    if (c == -1)
        pm_error("unexpected end of file");

    return c;
}



static void
decompressImageMode1(unsigned int          const rows,
                     unsigned int          const planes,
                     const struct Raster * const rasterP,
                     unsigned int *        const colsP) {
/*----------------------------------------------------------------------------
   Mode 1 appears to be a compressed raster format where each element of
   a rowplane is two bytes -- the first one is a repeat count and the second
   is 8 black-or-white pixels.

   As input, *rasterP has that compressed format.  We replace it in its
   entirety with the decompressed data (one byte per 8 pixels).

   Return as *colsP the number of pixels in the longest line in the raster.
-----------------------------------------------------------------------------*/
    unsigned int const newcols = 10240;
    /* It could not be larger than that! */

    unsigned int cols;
    unsigned int row;

    for (row = 0, cols = 0; row < rows; ++row) {
        if (rasterP->rowplane[row * planes + 0]) {
            unsigned int plane;
            for (plane = 0; plane < planes; ++plane) {
                unsigned int const rowplaneIndex = row * planes + plane;

                unsigned int i;
                unsigned int col;
                unsigned char * buf;

                MALLOCARRAY(buf, newcols);
                if (buf == NULL)
                    pm_error("out of memory");
                for (i = 0, col = 0;
                     col < rasterP->rowLen[rowplaneIndex];
                     col += 2) {
                    int cmd, val;
                    for (cmd = rasterP->rowplane[rowplaneIndex][col],
                             val = rasterP->rowplane[rowplaneIndex][col+1];
                         cmd >= 0 && i < newcols; --cmd, ++i)
                        buf[i] = val;
                }
                cols = MAX(cols, i);
                free(rasterP->rowplane[rowplaneIndex]);

                /*
                 * This is less than what we have so it realloc should
                 * not return null. Even if it does, tough! We will
                 * lose a line, and probably die on the next line anyway
                 */
                rasterP->rowplane[rowplaneIndex] = realloc(buf, i);
                rasterP->rowLen[rowplaneIndex]   = i;
            }
        }
    }
    *colsP = cols * 8;  assert(cols < UINT_MAX/8);
}



static void
writePpm(FILE *                const ofP,
         unsigned int          const cols,
         unsigned int          const rows,
         unsigned int          const planes,
         const struct Raster * const rasterP,
         int                   const mode) {

    pixel * pixrow;
    unsigned int row;

    ppm_writeppminit(stdout, cols, rows, 255, 0);
    pixrow = ppm_allocrow(cols);

    for (row = 0; row < rows; ++row) {
        if (rasterP->rowplane[row * planes + 0] == NULL) {
            /* This row is not present in the raster; make a row of padding */
            unsigned int col;
            for (col = 0; col < cols; ++col)
                PPM_ASSIGN(pixrow[col], 0, 0, 0);
        } else {
            unsigned int col;
            for (col = 0; col < cols; col += 8) {
                unsigned int i;
                for (i = 0; i < 8 && col + i < cols; ++i) {
                    unsigned int plane;
                    unsigned char bf[3];

                    assert(planes == 3);

                    for (plane = 0; plane < planes; ++plane) {
                        unsigned int const rowplaneIndex =
                            row * planes + plane;

                        /* Oddly enough, *rasterP can contain rowplanes of
                           varying widths ('cols' is just a maximum) and can
                           skip rowplanes altogether.  rasterP->rowLen[i] tells
                           how many bytes of data are in rowplane i, and can
                           be zero to mean the entire rowplane is absent.
                        */
                        if (col/8 >= rasterP->rowLen[rowplaneIndex])
                            bf[plane] = 0;
                        else {
                            bf[plane] =
                                (rasterP->rowplane[rowplaneIndex][col/8] &
                                 (1 << (7 - i))) ? 255 : 0;
                        }
                    }
                    PPM_ASSIGN(pixrow[col + i], bf[0], bf[1], bf[2]);
                }
            }
        }
        ppm_writeppmrow(stdout, pixrow, cols, 255, 0);
    }
}



int
main(int argc, const char ** argv) {

    int cmd, val;
    char buffer[BUFSIZ];
    unsigned int planes;
    unsigned int height;
    unsigned int rows;
    unsigned int cols;
    bool colsIsSet;
    FILE * ifP;
    int mode;
    bool modeIsSet;
    int c;
    unsigned int plane;
    unsigned int row;
    struct Raster raster;
    bool rasterIsSetUp;

    pm_proginit(&argc, argv);

    if (argc-1 > 0)
        ifP = pm_openr(argv[1]);
    else
        ifP = stdin;

    if (argc-1 > 2)
        pm_error("Too many arguments (%u).  Only possible argument is planes "
                 "input file name", argc-1);

    row = 0;  /* initial value */
    plane = 0;  /* initial value */
    height = 0;  /* initial value */
    planes = 3;  /* initial value */
    modeIsSet = false;  /* initial value */
    colsIsSet = false;  /* initial value */
    rasterIsSetUp = false;  /* initial value */

    while ((c = fgetc(ifP)) != -1) {
        if (c != '\033')
            continue;
        switch (c = egetc(ifP)) {
        case 'E':   /* reset */
            break;
        case '*': {
            bool argPresent;
            unsigned int i;
            cmd = egetc(ifP);
            for (i = 0; i < BUFSIZ-1; ++i) {
                if (!isdigit(c = egetc(ifP)) && c != '+' && c != '-')
                    break;
                buffer[i] = c;
            }
            /* 'c' is now the character after the numerial argument */
            if (i == 0) {
                argPresent = false;
            } else {
                buffer[i] = '\0';
                if (sscanf(buffer, "%d", &val) != 1)
                    pm_error("bad value `%s' at <ESC>*%c%c", buffer, cmd, c);
                argPresent = true;
            }
            switch (cmd) {
            case 't':
                switch (c) {
                case 'J':   /* render */
                    break;
                case 'K':   /* back scale */
                    break;
                case 'I':   /* gamma */
                    break;
                case 'R':
                    break;  /* set resolution */
                default:
                    pm_message("Ignoring unimplemented <ESC>*%c%d%c",
                               cmd, val, c);
                    break;
                }
                break;
            case 'r':
                switch (c) {
                case 'S':   /* width */
                    if (!argPresent)
                        pm_error("Missing argument for <ESC>*rS command");
                    else if (val < 0)
                        pm_error("negative width value");
                    else {
                        cols = val;
                        colsIsSet = true;
                    }
                    break;
                case 'T':   /* height */
                    if (!argPresent)
                        pm_error("Missing argument for <ESC>*rT command");
                    else if (val < 0)
                        pm_error("negative height value");
                    else
                        height = val;
                    break;
                case 'U':   /* planes */
                    if (!argPresent)
                        pm_error("Missing argument for <ESC>*rU command");
                    else if (val < 0)
                        pm_error("negative planes value");
                    else {
                        planes = val;
                        if (planes != 3)
                            pm_error("can handle only 3-plane images");
                    }
                    break;
                case 'A':   /* begin raster */
                    break;
                case 'B':
                case 'C':   /* end raster */
                    break;
                case 'V':
                    break;  /* set deci height */
                case 'H':
                    break;  /* set deci width */
                default:
                    pm_message("Ignoring unimplemented <ESC>*%c%c "
                               "command class", cmd, c);
                    break;
                }
                break;
            case 'b':
                switch (c) {
                case 'M':   /* transmission mode */
                    if (!argPresent)
                        pm_error("missing argument for *bM");
                    if (val != 0 && val != 1)
                        pm_error("unimplemented transmission mode %d", val);
                    mode = val;
                    modeIsSet = true;
                    break;
                case 'V':   /* send plane */
                case 'W':   /* send last plane */
                {
                    unsigned int const dataLen = val;
                    if (!rasterIsSetUp) {
                        rasterInit(&raster, planes);
                        rasterRealloc(&raster, height);
                        rasterIsSetUp = true;
                    }

                    rasterRealloc(&raster, row+1);

                    if (plane >= planes)
                        pm_error("too many planes");
                    if (!colsIsSet)
                        pm_error("missing width command");

                    if (!argPresent)
                        pm_error("missing argument in "
                                 "<ESC>*bV or <ESC> *bW command");
                    cols = MAX(cols, dataLen);
                    rasterAllocRowplane(&raster, row, plane, dataLen);
                    {
                        size_t itemReadCt;
                        itemReadCt =
                            fread(raster.rowplane[row * planes + plane],
                                  1, dataLen, ifP);
                        if (itemReadCt != dataLen)
                            pm_error("short data");
                    }
                    if (c == 'V')
                        ++plane;
                    else {
                        plane = 0;
                        if (row > UINT_MAX/planes-100)
                            pm_error("Too many rows (more than %u) "
                                     "for computation", row);
                        ++row;
                    }
                } break;
                default:
                    pm_message("Ignoring unimplemented <ESC>*%c%d%c",
                               cmd, val, c);
                    break;
                }
                break;
            case 'p': /* Position */
                if (!argPresent)
                    pm_error("missing argument in <ESC>*p command");
                if (plane != 0)
                    pm_error("changed position in the middle of "
                             "transferring planes");
                switch (c) {
                case 'X':
                    pm_message("can position only in Y");
                    break;
                case 'Y': {
                    unsigned int targetRow;

                    /* A signed argument is relative to current row; an
                       unsigned argument is an absolute row.
                    */
                    if (buffer[0] == '+') {
                        if (val > UINT_MAX-row)
                            pm_error("Relative Y position command generates "
                                     "uncomputably high row number.");
                        targetRow = row + val;
                    } else if (buffer[0] == '-') {
                        unsigned int const absval = -val;
                        assert(val <= 0);
                        if (absval > row)
                            pm_error("relative Y position command "
                                     "positions before top of image");
                        targetRow = row - absval;
                    } else
                        targetRow = val;

                    if (!rasterIsSetUp) {
                        rasterInit(&raster, planes);
                        rasterIsSetUp = true;
                    }
                    rasterAddRows(&raster, row, targetRow);

                    row = targetRow;

                    if (row > UINT_MAX/planes-100)
                        pm_error("Too many rows (more than %u) "
                                 "for computation", row);
                } break;
                default:
                    pm_message("Ignoring unimplemented <ESC>*p%d%c", val, c);
                    break;
                }
                break;
            default:
                pm_message("Ignoring unimplemented <ESC>*%c%c", cmd, c);
                break;
             }
        } /* case */
        } /* switch */
    }
    pm_close(ifP);

    if (!modeIsSet)
        pm_error("Input does not contain a 'bM' transmission mode order");

    rows = row;

    if (mode == 1)
        decompressImageMode1(rows, planes, &raster, &cols);

    writePpm(stdout, cols, rows, planes, &raster, mode);

    pm_close(stdout);

    return 0;
}



