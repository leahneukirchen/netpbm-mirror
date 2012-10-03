/* icontopbm.c - read a Sun icon file and produce a Netbpbm image.
**
** Copyright (C) 1988 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

/*
  Most icon images are monochrome: Depth=1
  Depth=8 images are extremely rare.  At least some of these are color
  images but we can't tell the palette color order.
  Output will be in pgm.  Convert to ppm with pgmtoppm or pamlookup
  if necessary.
*/

#include <assert.h>
#include <string.h>

#include "nstring.h"
#include "pbm.h"
#include "pgm.h"



static void
ReadIconFileHeader(FILE * const file, 
                   int *  const widthP, 
                   int *  const heightP, 
                   int *  const depthP,
                   int *  const bitsPerItemP) {

    unsigned int fieldCt;

    fieldCt = 0;
    *widthP = *heightP = -1;

    for ( ; ; ) {
        char variable[80+1];
        int ch;
        unsigned int i;
        int value;

        while ((ch = getc(file)) == ',' || ch == '\n' || ch == '\t' ||
                ch == ' ')
            ;
        for (i = 0;
             ch != '=' && ch != ',' && ch != '\n' && ch != '\t' && 
                 ch != ' ' && (i < (sizeof(variable) - 1));
             ++i) {
            variable[i] = ch;
            if ((ch = getc( file )) == EOF)
                pm_error( "invalid input file -- premature EOF" );
        }
        variable[i] = '\0';

        if (streq(variable, "*/") && fieldCt > 0)
            break;

        if (fscanf( file, "%d", &value ) != 1)
            continue;

        if (streq( variable, "Width")) {
            *widthP = value;
            ++fieldCt;
        } else if (streq( variable, "Height")) {
            *heightP = value;
            ++fieldCt;
        } else if (streq( variable, "Depth")) {
            if (value != 1 && value != 8)
                pm_error("invalid depth");
            *depthP = value;
            ++fieldCt;
        } else if (streq(variable, "Format_version")) {
            if (value != 1)
                pm_error("invalid Format_version");
            ++fieldCt;
        } else if (streq(variable, "Valid_bits_per_item")) {
            if (value != 16 && value !=32)
                pm_error("invalid Valid_bits_per_item");
            *bitsPerItemP = value; 
            ++fieldCt;
        }
    }

    if (fieldCt < 5)
        pm_error("invalid sun icon file header: "
                 "only %u out of required 5 fields present", fieldCt);

    if (*widthP <= 0)
        pm_error("invalid width (must be positive): %d", *widthP);
    if (*heightP <= 0)
        pm_error("invalid height (must be positive): %d", *heightP);

}


int
main(int argc, const char ** argv) {

    FILE * ifP;
    bit * bitrow;
    gray * grayrow;
    int rows, cols, depth, row, format, maxval, colChars, bitsPerItem;

    pm_proginit(&argc, argv);

    if (argc-1 > 1)
        pm_error("Too many arguments (%u).  Program takes at most one: "
                 "name of input file", argc-1);

    if (argc-1 == 1)
        ifP = pm_openr(argv[1]);
    else
        ifP = stdin;

    ReadIconFileHeader(ifP, &cols, &rows, &depth, &bitsPerItem);

    if (depth == 1) {
        format = PBM_TYPE;
        maxval = 1;
        pbm_writepbminit(stdout, cols, rows, 0);
        bitrow = pbm_allocrow_packed(cols);
        colChars = cols / 8;
    } else {
        assert(depth == 8);
        format = PGM_TYPE;
        maxval = 255;
        pgm_writepgminit(stdout, cols, rows, maxval, 0);
        grayrow = pgm_allocrow(cols);
        colChars = cols;
    }

    for (row = 0; row < rows; ++row) {
        unsigned int colChar;
        for (colChar = 0; colChar < colChars; ++colChar) {
            unsigned int data;
            int status;

            /* read 8 bits */
            if (row==0 && colChar == 0)
                status = fscanf(ifP, " 0x%2x", &data);
            else if (colChar % (bitsPerItem/8) == 0)
                status = fscanf(ifP, ", 0x%2x", &data);
            else
                status = fscanf(ifP, "%2x", &data);

            /* write 8 bits */
            if (status == 1) {
                if (format == PBM_TYPE)
                    bitrow[colChar]  = data;
                else
                    grayrow[colChar] = data;
            } else
                pm_error("error scanning bits item %u" , colChar);
        }

        /* output row */
        if (format == PBM_TYPE)
            pbm_writepbmrow_packed(stdout, bitrow, cols, 0);
        else
            pgm_writepgmrow(stdout, grayrow, cols, maxval, 0);
    }

    pm_close(ifP);
    pm_close(stdout);
    return 0;
}
