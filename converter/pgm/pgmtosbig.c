/*=============================================================================
                                 pgmtosbig
===============================================================================

  This program converts from PGM to a simple subset of SBIG.

  By Bryan Henderson January 19, 2015.

  Contributed to the public domain by its author.
=============================================================================*/
#include <string.h>

#include "pm.h"
#include "nstring.h"
#include "pgm.h"



#define SBIG_HEADER_LENGTH  2048      /* File header length */

#define CTLZ "\x1A"


struct SbigHeader {
/*----------------------------------------------------------------------------
   The information in an SBIG file header.

   This is only the information this program cares about; the header
   may have much more information in it.
-----------------------------------------------------------------------------*/
    unsigned int height;
    unsigned int width;
    unsigned int saturationLevel;
};



static void
addUintParm(char *       const buffer,
            const char * const name,
            unsigned int const value) {

    const char * line;

    pm_asprintf(&line, "%s=%u\n\r", name, value);

    strcat(buffer, line);

    pm_strfree(line);
}



static void
writeSbigHeader(FILE *            const ofP,
                struct SbigHeader const hdr) {

    char buffer[SBIG_HEADER_LENGTH];

    memset(&buffer[0], 0x00, sizeof(buffer));

    buffer[0] = '\0';

    /* N.B. LF-CR instead of CRLF.  That's what the spec says. */

    strcat(buffer, "ST-6 Image\n\r" );

    addUintParm(buffer, "Height", hdr.height);

    addUintParm(buffer, "Width", hdr.width);

    addUintParm(buffer, "Sat_level", hdr.saturationLevel);

    strcat(buffer, "End\n\r" CTLZ);

    fwrite(buffer, 1, sizeof(buffer), ofP);
}



int
main(int argc, const char * argv[]) {

    FILE * ifP;
    gray * grayrow;
    int rows;
    int cols;
    int format;
    struct SbigHeader hdr;
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

    grayrow = pgm_allocrow(cols);

    hdr.height = rows;
    hdr.width = cols;
    hdr.saturationLevel = maxval;

    writeSbigHeader(stdout, hdr);

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        pgm_readpgmrow(ifP, grayrow, cols, maxval, format);

        for (col = 0; col < cols; ++col)
            pm_writelittleshort(stdout, grayrow[col]);
    }

    pm_close(ifP);

    return 0;
}
