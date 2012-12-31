/*----------------------------------------------------------------------------
                               pamtompfont
------------------------------------------------------------------------------
  Part of the Netpbm package.

  Convert a PAM image to an Mplayer bitmap font.

  It is obvious that this format was designed to be an image format and
  adopted by Mplayer for it's fonts (before Mplayer got the ability to
  use Freetype to read standard font formats such as TrueType).  But
  I have no idea what the format was originally.

  In the Mplayer font subset of the format, the image is always grayscale
  (one byte per pixel) with no palette.

  By Bryan Henderson, San Jose CA 2008.05.18

  Contributed to the public domain by its author.
-----------------------------------------------------------------------------*/

#include <string.h>
#include <assert.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pm.h"
#include "pam.h"

struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFilename;
};


static void
parseCommandLine(int argc, char ** argv,
                 struct cmdlineInfo *cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */

    OPTENTINIT;

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 == 0) 
        cmdlineP->inputFilename = "-";
    else if (argc-1 != 1)
        pm_error("Program takes zero or one argument (filename).  You "
                 "specified %d", argc-1);
    else
        cmdlineP->inputFilename = argv[1];

    free(option_def);
}



static void
validateInput(struct pam * const inpamP) {

    /* The image format does provide for RGB images, but Mplayer doesn't
       understand that format (and doesn't even recognize it as something
       it doesn't understand)
    */

    if (inpamP->depth != 1)
        pm_error("Input must have depth 1.  This image's depth is %u",
                 inpamP->depth);
}



static void
writeMpFontHeader(FILE *       const ofP,
                  struct pam * const inpamP) {
/*----------------------------------------------------------------------------
   Write the 32 byte header.
-----------------------------------------------------------------------------*/
    fwrite("mhwanh", 1, 6, ofP);  /* Signature */

    fputc(0, ofP);  /* pad */
    fputc(0, ofP);  /* pad */

    /* Write the old 16 bit width field.  Zero means use the 32 bit one
       below instead.
    */
    pm_writebigshort(ofP, 0);

    /* Height */
    pm_writebigshort(ofP, inpamP->height);

    /* Number of colors in palette.  Zero means not paletted image */
    pm_writebigshort(ofP, 0);

    {
        unsigned int i;
        for (i = 0; i < 14; ++i)
            fputc(0, ofP);  /* pad */
    }
    /* Width */
    pm_writebiglong(ofP, inpamP->width);
}



static void
convertRaster(struct pam * const inpamP,
              FILE *       const ofP) {
            
    tuple * tuplerow;
    unsigned char * outrow;
    unsigned int row;

    assert(inpamP->depth == 1);

    tuplerow = pnm_allocpamrow(inpamP);

    MALLOCARRAY(outrow, inpamP->width);

    if (outrow == NULL)
        pm_error("Unable to allocate space for a %u-column output buffer",
                 inpamP->width);

    for (row = 0; row < inpamP->height; ++row) {
        unsigned int col;

        pnm_readpamrow(inpamP, tuplerow);

        for (col = 0; col < inpamP->width; ++col) {
            outrow[col] =
                pnm_scalesample(tuplerow[col][0], inpamP->maxval, 255);
        }
        
        fwrite(outrow, 1, inpamP->width, ofP);
    }
    free(outrow);
    pnm_freepamrow(tuplerow);
}



int
main(int argc, char *argv[]) {

    struct cmdlineInfo cmdline;
    FILE * ifP;
    struct pam inpam;   /* Input PAM image */

    pnm_init(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFilename);

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    validateInput(&inpam);

    writeMpFontHeader(stdout, &inpam);

    convertRaster(&inpam, stdout);

    return 0;
}
