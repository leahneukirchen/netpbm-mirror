#include <stdio.h>

#include "pbm.h"

/* By Bryan Henderson, San Jose CA 2003.09.06.

   Contributed to the public domain by its author.

   This is a replacement of Joseph Sheedy's (hbarover2@yahoo.com) Perl
   program of the same name, distributed in his Pbmtomatrixorbital
   package.  This version uses Netpbm libraries and is fully
   consistent with other Netpbm programs.
*/


static void
generateMo(FILE *       const ofP, 
           bit **       const bits,
           unsigned int const cols,
           unsigned int const rows) {

    unsigned int col;

    fputc(cols, ofP);
    fputc(rows, ofP);

    for (col = 0; col < cols; ++col) {
        unsigned int row;
        unsigned int outbitpos;
        unsigned char outchar;
        
        outbitpos = 0;  /* Start at 1st bit of 1st output byte */

        for (row = 0; row < rows; ++row) {
            if (outbitpos == 0)
                /* We're starting a new byte; initialize it to zeroes */
                outchar = 0;

            outchar |= bits[row][col] << outbitpos;

            if (outbitpos == 7) 
                /* We filled up a byte.  Output it. */
                fputc(outchar, ofP);

            outbitpos = (outbitpos + 1) % 8;
        }
        if (outbitpos != 0)
            /* Our last byte is partial, so must be output now. */
            fputc(outchar, ofP);
    }
}



int
main(int argc, const char ** argv) {

    FILE * ifP;
    bit ** bits;
    int rows, cols;
    const char * inputFilename;

    pm_proginit(&argc, argv);

    if (argc-1 > 1)
        pm_error("Too many arguments (%u).  The only valid argument is an "
                 "input file name.", argc-1);
    else if (argc-1 == 1) 
        inputFilename = argv[1];
    else
        inputFilename = "-";

    ifP = pm_openr(inputFilename);
    
    bits = pbm_readpbm(ifP, &cols, &rows);

    if (rows > 255)
        pm_error("Image is too high:  %u rows.  Max height: 255 rows", rows);
    if (cols > 255)
        pm_error("Image is too wide:  %u cols.  Max width: 255 cols", cols);

    generateMo(stdout, bits, cols, rows);
    
    pm_close(ifP);

    pbm_freearray(bits, rows);

    return 0;
}



