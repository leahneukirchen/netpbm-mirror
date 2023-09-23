/* imgtoppm.c - read an Img-whatnot file and produce a portable pixmap
**
** Based on a simple conversion program posted to comp.graphics by Ed Falk.
**
** Copyright (C) 1989 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdbool.h>
#include <string.h>
#include "nstring.h"

#include "ppm.h"



int
main(int argc, const char ** argv) {

    FILE * ifP;
    pixel * pixelrow;
    pixel colormap[256];
    unsigned int cols;
    unsigned int rows;
    pixval maxval;
    unsigned int cmaplen;
    int len;
    bool gotAt, gotCm, gotPd;
    unsigned char buf[4096];

    pm_proginit(&argc, argv);

    if (argc-1 >= 1)
        ifP = pm_openr(argv[1]);
    else
        ifP = stdin;

    if (argc-1 > 1)
        pm_error("Too many arguments (%d).  "
                 "The only possible argument is the input file name", argc-1);

    /* Get signature. */
    fread(buf, 8, 1, ifP);
    buf[8] = '\0';

    /* Get entries. */
    gotAt = false; /* initial value */
    gotCm = false; /* initial value */
    gotPd = false; /* initial value */
    while (fread( buf, 2, 1, ifP) == 1) {
        if (strneq((char*) buf, "AT", 2)) {
            if (fread(buf, 8, 1, ifP) != 1)
                pm_error("bad attributes header");
            buf[8] = '\0';
            len = atoi((char*) buf);
            if (fread(buf, len, 1, ifP) != 1)
                pm_error("bad attributes buf");
            buf[len] = '\0';
            sscanf((char*) buf, "%4u%4u%4u", &cols, &rows, &cmaplen);
            maxval = 255;
            gotAt = true;
        } else if (strneq((char*) buf, "CM", 2)) {
            unsigned int i;
            if (!gotAt)
                pm_error("missing attributes header");
            if (fread(buf, 8, 1, ifP) != 1)
                pm_error("bad colormap header");
            buf[8] = '\0';
            len = atoi((char*) buf);
            if (fread(buf, len, 1, ifP) != 1)
                pm_error("bad colormap buf");
            if (cmaplen * 3 != len) {
                pm_message(
                    "cmaplen (%d) and colormap buf length (%d) do not match",
                    cmaplen, len);
                if (cmaplen * 3 < len)
                    len = cmaplen * 3;
                else if (cmaplen * 3 > len)
                    cmaplen = len / 3;
            }
            for (i = 0; i < len; i += 3)
                PPM_ASSIGN(colormap[i / 3], buf[i], buf[i + 1], buf[i + 2]);
            gotCm = true;
        } else if (strneq((char*) buf, "PD", 2)) {
            unsigned int row;

            if (fread(buf, 8, 1, ifP) != 1)
                pm_error("bad pixel data header");
            buf[8] = '\0';
            len = atoi((char*) buf);
            if (len != cols * rows)
                pm_message(
                    "pixel data length (%d) does not match image size (%d)",
                    len, cols * rows);

            ppm_writeppminit(stdout, cols, rows, maxval, 0);
            pixelrow = ppm_allocrow(cols);

            for (row = 0; row < rows; ++row) {
                unsigned int col;

                if (fread(buf, 1, cols, ifP) != cols)
                    pm_error("EOF / read error");
                for (col = 0; col < cols; ++col) {
                    if (gotCm)
                        pixelrow[col] = colormap[buf[col]];
                    else
                        PPM_ASSIGN(pixelrow[col],
                                   buf[col], buf[col], buf[col]);
                }
                ppm_writeppmrow(stdout, pixelrow, cols, maxval, 0);
            }
            gotPd = true;
        }
    }
    if (!gotPd)
        pm_error("missing pixel data header");

    pm_close(ifP);
    /* If the program failed, it previously aborted with nonzero completion
       code, via various function calls.
    */
    return 0;
}
