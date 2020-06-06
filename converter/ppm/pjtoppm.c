/* pjtoppm.c - convert an HP PainJetXL image to a portable pixmap file
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

#include "ppm.h"
#include "pm_c_util.h"
#include "mallocvar.h"

static char usage[] =  "[paintjetfile]";



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



int
main(int argc, const char ** argv) {

    int cmd, val;
    char buffer[BUFSIZ];
    int planes = 3, rows = -1, cols = -1;
    unsigned char **image = NULL;
    int *imlen;
    FILE * ifP;
    int mode;
    bool modeIsSet;
    int argn;
    unsigned char bf[3];
    pixel * pixrow;
    int c;
    int row;
    int plane;

    pm_proginit(&argc, argv);

    argn = 1;
    if (argn != argc)
        ifP = pm_openr(argv[argn++]);
    else
        ifP = stdin;

    if (argn != argc)
        pm_usage(usage);

    row = 0;  /* initial value */
    plane = 0;  /* initial value */
    modeIsSet = false;  /* initial value */

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
                    cols = val;
                    break;
                case 'T':   /* height */
                    rows = val;
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
                        pm_error("unimplemented trasmission mode %d", val);
                    mode = val;
                    modeIsSet = true;
                    break;
                case 'V':   /* send plane */
                case 'W':   /* send last plane */
                    if (rows == -1 || row >= rows || image == NULL) {
                        if (rows == -1 || row >= rows)
                            rows += 100;
                        if (image == NULL) {
                            MALLOCARRAY(image, uintProduct(rows, planes));
                            MALLOCARRAY(imlen, uintProduct(rows, planes));
                        } else {
                            REALLOCARRAY(image, uintProduct(rows, planes));
                            REALLOCARRAY(imlen, uintProduct(rows, planes));
                        }
                    }
                    if (image == NULL || imlen == NULL)
                        pm_error("out of memory");
                    if (plane >= planes)
                        pm_error("too many planes");
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
        int const newcols = 10240;  /* It could not be larger that that! */
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
                    for (plane = 0; plane < planes; ++plane)
                        if (mode == 0 && cmd >= imlen[row * planes + plane])
                            bf[plane] = 0;
                        else
                            bf[plane] = (image[row * planes + plane][cmd] &
                                     (1 << (7 - i))) ? 255 : 0;
                    PPM_ASSIGN(pixrow[col + i], bf[0], bf[1], bf[2]);
                }
            }
        }
        ppm_writeppmrow(stdout, pixrow, cols, 255, 0);
    }
    pm_close(stdout);

    return 0;
}



