/* sirtopnm.c - read a Solitaire Image Recorder file and write a portable anymap
**
** Copyright (C) 1991 by Marvin Landis.
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

#include "mallocvar.h"
#include "pnm.h"



static void
readSirHeader(FILE *         const ifP,
              int *          const formatP,
              unsigned int * const rowsP,
              unsigned int * const colsP) {

    short info;

    pm_readlittleshort(ifP, &info);
    if (info != 0x3a4f)
        pm_error( "Input file is not a Solitaire file");

    pm_readlittleshort(ifP, &info);

    pm_readlittleshort(ifP, &info);
    if (info == 17)
        *formatP = PGM_TYPE;
    else if (info == 11)
        *formatP = PPM_TYPE;
    else
        pm_error( "Input is not MGI TYPE 11 or MGI TYPE 17" );

    pm_readlittleshort(ifP, &info);
    *colsP = info;

    pm_readlittleshort(ifP, &info);
    *rowsP = info;

    {
        unsigned int i;
        for (i = 1; i < 1531; ++i)
            pm_readlittleshort(ifP, &info);
    }
}



static void
convertPgm(FILE *       const ifP,
           FILE *       const ofP,
           unsigned int const rows,
           unsigned int const cols,
           xel *        const xelrow) {

    unsigned int row;

    pm_message("Writing a PGM file");

    for (row = 0; row < rows; ++row) {
        unsigned int col;
        for (col = 0; col < cols; ++col)
            PNM_ASSIGN1(xelrow[col], fgetc(ifP));

        pnm_writepnmrow(ofP, xelrow, cols, 255, PGM_TYPE, 0);
    }
}



static void
convertPpm(FILE *       const ifP,
           FILE *       const ofP,
           unsigned int const rows,
           unsigned int const cols,
           xel *        const xelrow) {

    unsigned int const picsize = cols * rows * 3;
    unsigned int const planesize = cols * rows;

    unsigned char * sirarray;  /* malloc'ed array */
    unsigned int row;

    MALLOCARRAY(sirarray, picsize);

    if (!sirarray)
        pm_error( "Not enough memory to load %u x %u x %u SIR file",
                  cols, rows, 3);

    if (fread(sirarray, 1, picsize, ifP) != picsize)
        pm_error("Error reading SIR file");

    pm_message("Writing a PPM file");
    for (row = 0; row < rows; ++row) {
        unsigned int col;

        for (col = 0; col < cols; col++)
            PPM_ASSIGN(xelrow[col], sirarray[row*cols+col],
                       sirarray[planesize + (row*cols+col)],
                       sirarray[2*planesize + (row*cols+col)]);

        pnm_writepnmrow(ofP, xelrow, cols, 255, PPM_TYPE, 0);
    }
    free(sirarray);
}



int
main(int argc, const char ** argv) {

    FILE * ifP;
    xel * xelrow;
    unsigned int rows, cols;
    int format;

    pm_proginit(&argc, argv);

    if (argc-1 > 1)
        pm_error ("Too many arguments.  The only possible argument is "
                  "the input file name");
    else if (argc-1 >= 1)
        ifP = pm_openr(argv[1]);
    else
        ifP = stdin;

    readSirHeader(ifP, &format, &rows, &cols);

    pnm_writepnminit(stdout, cols, rows, 255, format, 0);

    xelrow = pnm_allocrow(cols);

    switch (PNM_FORMAT_TYPE(format)) {
    case PGM_TYPE:
        convertPgm(ifP, stdout, rows, cols, xelrow);
        break;
    case PPM_TYPE:
        convertPpm(ifP, stdout, rows, cols, xelrow);
        break;
    default:
        assert(false);
    }
    pnm_freerow(xelrow);

    pm_close(ifP);

    exit(0);
}
