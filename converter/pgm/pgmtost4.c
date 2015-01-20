/*=============================================================================
                                 pgmtost4
===============================================================================

  This program converts from PGM to a simple subset of SBIG ST-4.

  By Bryan Henderson January 19, 2015.

  Contributed to the public domain by its author.
=============================================================================*/
#include <string.h>

#include "pm.h"
#include "nstring.h"
#include "pam.h"



static unsigned int const st4Height = 165;
static unsigned int const st4Width  = 192;
static unsigned int const st4Maxval = 255;



static void
writeSt4Footer(FILE * const ofP) {

    const char * const comment = "This was created by Pgmtost4";
    char buffer[192];

    memset(buffer, ' ', sizeof(buffer));  /* initial value */

    buffer[0] = 'v';

    memcpy(&buffer[  0], "v", 1);
    memcpy(&buffer[  1], comment, strlen(comment));
    memcpy(&buffer[ 79], "         7", 10);
    memcpy(&buffer[ 89], "         8", 10);
    memcpy(&buffer[ 99], "         9", 10);
    memcpy(&buffer[109], "        10", 10);

    fwrite(buffer, 1, sizeof(buffer), ofP);
}



int
main(int argc, const char * argv[]) {

    FILE * ifP;
    tuple * tuplerow;
    struct pam inpam;
    unsigned int row;
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

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    if (inpam.height != st4Height)
        pm_error("Image is wrong height for ST-4 SBIG: %u pixels.  "
                 "Must be %u", inpam.height, st4Height);

    if (inpam.width != st4Width)
        pm_error("Image is wrong width for ST-4 SBIG: %u pixels.  "
                 "Must be %u", inpam.width, st4Width);
    
    /* Really, we should just scale to maxval 255.  There are library routines
       for that, but we're too lazy even for that, since nobody is really
       going to use this program.
    */
    if (inpam.maxval != st4Maxval)
        pm_error("Image is wrong maxval for ST-4 SBIG: %u.  "
                 "Must be %u", (unsigned)inpam.maxval, st4Maxval);

    tuplerow = pnm_allocpamrow(&inpam);

    for (row = 0; row < inpam.height; ++row) {
        unsigned int col;

        pnm_readpamrow(&inpam, tuplerow);

        for (col = 0; col < inpam.width; ++col)
            pm_writechar(stdout, (char)tuplerow[col][0]);
    }

    writeSt4Footer(stdout);

    pm_close(ifP);

    return 0;
}
