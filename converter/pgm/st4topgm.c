/*=============================================================================
                               st4topgm
===============================================================================

  Convert an SBIG ST-4 image (not to be confused with the more sophisticated
  SBIG format that every other SBIG camera produces) to PGM.

  By Bryan Henderson January 2015.

  Contributed to the public domain by its author.

  This program was intended to substitute for the program of the same name in
  the Debian version of Netpbm, by Justin Pryzby <justinpryzby@users.sf.net>
  in December 2003.

=============================================================================*/
#include <string.h>

#include "pm_config.h"
#include "pm_c_util.h"
#include "pm.h"
#include "pam.h"



static unsigned int const st4Height = 165;
static unsigned int const st4Width  = 192;
static unsigned int const st4Maxval = 255;



static void
validateFileSize(FILE * const ifP) {
/*----------------------------------------------------------------------------
   Abort program if *ifP is not the proper size for an ST-4 SBIG file.

   Don't change file position.
-----------------------------------------------------------------------------*/
    pm_filepos const st4FileSize = (st4Height+1) * st4Width;

    pm_filepos oldFilePos;
    pm_filepos endFilePos;

    pm_tell2(ifP, &oldFilePos, sizeof(endFilePos));

    fseek(ifP, 0, SEEK_END);

    pm_tell2(ifP, &endFilePos, sizeof(endFilePos));

    pm_seek2(ifP, &oldFilePos, sizeof(oldFilePos));

    if (endFilePos != st4FileSize)
        pm_error("File is the wrong size for an ST-4 SBIG file.  "
                 "It is %u bytes; it should be %u bytes",
                 (unsigned)endFilePos, (unsigned)st4FileSize);
}


static void
writeRaster(FILE *       const ifP,
            struct pam * const pamP) {

    tuple * tuplerow;
    unsigned int row;

    tuplerow = pnm_allocpamrow(pamP);

    for (row = 0; row < st4Height; ++row) {
        unsigned int col;

        for (col = 0; col < st4Width; ++col) {
            char c;

            pm_readchar(ifP, &c);

            tuplerow[col][0] = (unsigned char)c;
        }
        pnm_writepamrow(pamP, tuplerow);
    }

    pnm_freepamrow(tuplerow);
}



struct St4Footer {
/*----------------------------------------------------------------------------
   The information in an ST-4 SBIG footer.
-----------------------------------------------------------------------------*/
    /* Note that numerical information is in decimal text, because we're lazy.
    */

    char comment[78+1];
    char exposureTime[10+1];
    char focalLength[10+1];
    char apertureArea[10+1];
    char calibrationFactor[10+1];
};



static void
stripTrailing(char * const arg) {

    if (strlen(arg) > 0) {
        char * p;
        for (p = arg + strlen(arg); p > arg && *(p-1) == ' '; --p);

        *p = '\0';
    }
}



static void
stripLeading(char * const arg) {

    const char * p;

    for (p = &arg[0]; *p == ' '; ++p);

    if (p > arg)
        memmove(arg, p, strlen(p) + 1);
}



static void
readFooter(FILE *             const ifP,
           struct St4Footer * const footerP) {
/*----------------------------------------------------------------------------
   Read the footer of the ST-4 image from *ifP, assuming *ifP is positioned
   to the footer.

   Return its contents as *footerP.
-----------------------------------------------------------------------------*/
    char buffer[192];
    size_t bytesReadCt;

    /* The footer is laid out as follows.

       off len description
       --- --- -----------
       000   1 Signature: 'v'
       001  78 Freeform comment
       079  10 Exposure time in 1/100s of a second
       089  10 Focal length in inches
       099  10 Aperture area in square inches
       109  10 Calibration factor
       119  73 Reserved

       Note tha the footer is the same length as a raster row.
    */

    bytesReadCt = fread(buffer, 1, sizeof(buffer), ifP);

    if (bytesReadCt != 192)
        pm_error("Failed to read footer of image");

    if (buffer[0] != 'v')
        pm_error("Input is not an ST-4 file.  We know because the "
                 "signature byte (first byte of the footer) is not 'v'");

    buffer[191] = '\0';
    memmove(footerP->comment, &buffer[1], 78);
    footerP->comment[78] = '\0';
    stripTrailing(footerP->comment);

    memmove(footerP->exposureTime, &buffer[79], 10);
    footerP->exposureTime[10] = '\0';
    stripLeading(footerP->exposureTime);

    memmove(footerP->focalLength, &buffer[89], 10);
    footerP->focalLength[10] = '\0';
    stripLeading(footerP->focalLength);

    memmove(footerP->apertureArea, &buffer[99], 10);
    footerP->apertureArea[10] = '\0';
    stripLeading(footerP->apertureArea);

    memmove(footerP->calibrationFactor, &buffer[109], 10);
    footerP->calibrationFactor[10] = '\0';
    stripLeading(footerP->calibrationFactor);
}



static void
reportFooter(struct St4Footer const footer) {

	pm_message("Comment:                 %s", footer.comment);

	pm_message("Exposure time (1/100 s): %s", footer.exposureTime);

	pm_message("Focal length (in):       %s", footer.focalLength);

	pm_message("Aperture area (sq in):   %s", footer.apertureArea);

	pm_message("Calibration factor:      %s", footer.calibrationFactor);
}



int
main(int argc, const char **argv) {

    FILE * ifP;
    const char * inputFileName;
    struct pam outpam;
    struct St4Footer footer;

    pm_proginit(&argc, argv);

    if (argc-1 < 1)
        inputFileName = "-";
    else {
        inputFileName = argv[1];
        if (argc-1 > 1)
            pm_error("Too many arguments: %u.  "
                     "The only possible argument is the "
                     "optional input file name", argc-1);
    }        

    /* We check the file size to catch the common problem of the input not
       being valid ST-4 SBIG input.  Unlike most formats, this one does not
       have any signature at the head of the file.

       More checks on the validity of the format happens when we process
       the image footer.
    */

    ifP = pm_openr_seekable(inputFileName);

    validateFileSize(ifP);

    outpam.size = sizeof(outpam);
    outpam.len = PAM_STRUCT_SIZE(maxval);
    outpam.file = stdout;
    outpam.format = PGM_FORMAT;
    outpam.plainformat = false;
    outpam.height = st4Height;
    outpam.width = st4Width;
    outpam.depth = 1;
    outpam.maxval = st4Maxval;

    pnm_writepaminit(&outpam);

    writeRaster(ifP, &outpam);

    readFooter(ifP, &footer);

    reportFooter(footer);

    pm_close(ifP);
    pm_close(stdout);

    return 0;
}


