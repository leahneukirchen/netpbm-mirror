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



static int
egetc(FILE * const ifP) {
    int c;

    c = fgetc(ifP);

    if (c == -1)
        pm_error("unexpected end of file");

    return c;
}



static void
writePpm(FILE *           const ofP,
         unsigned int     const cols,
         unsigned int     const rows,
         unsigned int     const planes,
         unsigned char ** const image,
         int              const mode,
         const int *      const imlen) {

    pixel * pixrow;
    unsigned int row;

    ppm_writeppminit(stdout, cols, rows, (pixval) 255, 0);
    pixrow = ppm_allocrow(cols);

    for (row = 0; row < rows; ++row) {
        if (image[row * planes] == NULL) {
            unsigned int col;
            for (col = 0; col < cols; ++col)
                PPM_ASSIGN(pixrow[col], 0, 0, 0);
            continue;
        }
        {
            unsigned int col;
            unsigned int cmd;
            for (cmd = 0, col = 0; col < cols; col += 8, ++cmd) {
                unsigned int i;
                for (i = 0; i < 8 && col + i < cols; ++i) {
                    unsigned int plane;
                    unsigned char bf[3];

                    assert(planes == 3);

                    for (plane = 0; plane < planes; ++plane) {
                        if (mode == 0 && cmd >= imlen[row * planes + plane])
                            bf[plane] = 0;
                        else
                            bf[plane] = (image[row * planes + plane][cmd] &
                                     (1 << (7 - i))) ? 255 : 0;
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
    int planes = 3;
    unsigned int rows;
    unsigned int rowsX;
    unsigned int cols;
    bool colsIsSet;
    unsigned char **image = NULL;
    int *imlen;
    FILE * ifP;
    int mode;
    bool modeIsSet;
    int c;
    int plane;
    int row;

    pm_proginit(&argc, argv);

    if (argc-1 > 0)
        ifP = pm_openr(argv[1]);
    else
        ifP = stdin;

    if (argc-1 > 2)
        pm_error("Too many arguments (%u).  Only possible argument is "
                 "input file name", argc-1);

    row = 0;  /* initial value */
    plane = 0;  /* initial value */
    modeIsSet = false;  /* initial value */
    colsIsSet = false;  /* initial value */
    rowsX = 0;  /* initial value */

    while ((c = fgetc(ifP)) != -1) {
        if (c != '\033')
            continue;
        switch (c = egetc(ifP)) {
        case 'E':   /* reset */
            break;
        case '*': {
            unsigned int i;
            cmd = egetc(ifP);
            for (i = 0; i < BUFSIZ; i++) {
                if (!isdigit(c = egetc(ifP)) && c != '+' && c != '-')
                    break;
                buffer[i] = c;
            }
            if (i != 0) {
                buffer[i] = '\0';
                if (sscanf(buffer, "%d", &val) != 1)
                    pm_error("bad value `%s' at <ESC>*%c%c", buffer, cmd, c);
            }
            else
                val = -1;
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
                    pm_message("uninmplemented <ESC>*%c%d%c", cmd, val, c);
                    break;
                }
                break;
            case 'r':
                switch (c) {
                case 'S':   /* width */
                    if (val < 0)
                        pm_error("invalid width value");
                    else {
                        cols = val;
                        colsIsSet = true;
                    }
                    break;
                case 'T':   /* height */
                    if (val < 0)
                        pm_error ("invalid height value");
                    else
                        rowsX = val;
                    break;
                case 'U':   /* planes */
                    planes = val;
                    if (planes != 3)
                        pm_error("can handle only 3 plane files");
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
                    pm_message("uninmplemented <ESC>*%c%d%c", cmd, val, c);
                    break;
                }
                break;
            case 'b':
                switch (c) {
                case 'M':   /* transmission mode */
                    if (val != 0 && val != 1)
                        pm_error("unimplemented transmission mode %d", val);
                    mode = val;
                    modeIsSet = true;
                    break;
                case 'V':   /* send plane */
                case 'W':   /* send last plane */
                    if (row >= rowsX || image == NULL) {
                        if (row >= rowsX)
                            rowsX += 100;
                        if (image == NULL) {
                            MALLOCARRAY(image, uintProduct(rowsX, planes));
                            MALLOCARRAY(imlen, uintProduct(rowsX, planes));
                        } else {
                            REALLOCARRAY(image, uintProduct(rowsX, planes));
                            REALLOCARRAY(imlen, uintProduct(rowsX, planes));
                        }
                    }
                    if (image == NULL || imlen == NULL)
                        pm_error("out of memory");
                    if (plane >= planes)
                        pm_error("too many planes");
                    if (!colsIsSet)
                        pm_error("missing width value");

                    cols = MAX(cols, val);
                    imlen[row * planes + plane] = val;
                    MALLOCARRAY(image[row * planes + plane], val);
                    if (image[row * planes + plane] == NULL)
                        pm_error("out of memory");
                    if (fread(image[row * planes + plane], 1, val, ifP) != val)
                        pm_error("short data");
                    if (c == 'V')
                        ++plane;
                    else {
                        plane = 0;
                        ++row;
                    }
                    break;
                default:
                    pm_message("uninmplemented <ESC>*%c%d%c", cmd, val, c);
                    break;
                }
                break;
            case 'p': /* Position */
                if (plane != 0)
                    pm_error("changed position in the middle of "
                             "transferring planes");
                switch (c) {
                case 'X':
                    pm_message("can only position in y");
                    break;
                case 'Y':
                    if (buffer[0] == '+')
                        val = row + val;
                    if (buffer[0] == '-')
                        val = row - val;
                    for (; val > row; ++row)
                        for (plane = 0; plane < 3; ++plane) {
                            imlen[row * planes + plane] = 0;
                            image[row * planes + plane] = NULL;
                        }
                    row = val;
                    break;
                default:
                    pm_message("uninmplemented <ESC>*%c%d%c", cmd, val, c);
                    break;
                }
            default:
                pm_message("uninmplemented <ESC>*%c%d%c", cmd, val, c);
                break;
             }
        } /* case */
        } /* switch */
    }
    pm_close(ifP);

    if (!modeIsSet)
        pm_error("Input does not contain a 'bM' transmission mode order");

    rows = row;
    if (mode == 1) {
        unsigned int const newcols = 10240;
            /* It could not be larger than that! */

        unsigned char * buf;
        unsigned int row;

        for (row = 0, cols = 0; row < rows; ++row) {
            unsigned int plane;
            if (image[row * planes] == NULL)
                continue;
            for (plane = 0; plane < planes; ++plane) {
                unsigned int i;
                unsigned int col;
                MALLOCARRAY(buf, newcols);
                if (buf == NULL)
                    pm_error("out of memory");
                for (i = 0, col = 0;
                     col < imlen[plane + row * planes];
                     col += 2)
                    for (cmd = image[plane + row * planes][col],
                             val = image[plane + row * planes][col+1];
                         cmd >= 0 && i < newcols; cmd--, i++)
                        buf[i] = val;
                cols = MAX(cols, i);
                free(image[plane + row * planes]);
                /*
                 * This is less than what we have so it realloc should
                 * not return null. Even if it does, tough! We will
                 * lose a line, and probably die on the next line anyway
                 */
                image[plane + row * planes] = realloc(buf, i);
            }
        }
        cols *= 8;
    }

    writePpm(stdout, cols, rows, planes, image, mode, imlen);

    pm_close(stdout);

    return 0;
}



