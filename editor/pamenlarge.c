/*=============================================================================
                             pamenlarge
===============================================================================
  By Bryan Henderson 2004.09.26.  Contributed to the public domain by its
  author.
=============================================================================*/

#include <stdbool.h>
#include <assert.h>

#include "netpbm/mallocvar.h"
#include "netpbm/pm_c_util.h"
#include "netpbm/pam.h"
#include "netpbm/pbm.h"
#include "netpbm/shhopt.h"
#include "netpbm/nstring.h"


struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFilespec;
    unsigned int xScaleFactor;
    unsigned int yScaleFactor;
};



static void
parseCommandLine(int                  argc,
                 const char        ** argv,
                 struct cmdlineInfo * cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optStruct3 opt;  /* set by OPTENT3 */
    optEntry * option_def;
    unsigned int option_def_index;

    unsigned int scale;
    unsigned int xscaleSpec;
    unsigned int yscaleSpec;
    unsigned int scaleSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "xscale", OPT_UINT, &cmdlineP->xScaleFactor,  &xscaleSpec, 0);
    OPTENT3(0, "yscale", OPT_UINT, &cmdlineP->yScaleFactor,  &yscaleSpec, 0);
    OPTENT3(0, "scale",  OPT_UINT, &scale,                   &scaleSpec, 0);

    opt.opt_table = option_def;
    opt.short_allowed = false; /* We have some short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (scaleSpec && scale == 0)
        pm_error("-scale must be positive.  You specified zero");

    if (xscaleSpec && cmdlineP->xScaleFactor == 0)
        pm_error("-xscale must be positive.  You specified zero");

    if (yscaleSpec && cmdlineP->yScaleFactor == 0)
        pm_error("-yscale must be positive.  You specified zero");

    if (scaleSpec && xscaleSpec)
        pm_error("You cannot specify both -scale and -xscale");

    if (scaleSpec && yscaleSpec)
        pm_error("You cannot specify both -scale and -yscale");

    if (scaleSpec) {
        cmdlineP->xScaleFactor = scale;
        cmdlineP->yScaleFactor = scale;
    }

    if (xscaleSpec && !yscaleSpec)
        cmdlineP->yScaleFactor = 1;

    if (yscaleSpec && !xscaleSpec)
        cmdlineP->xScaleFactor = 1;

    if (scaleSpec || xscaleSpec || yscaleSpec) {
        /* Scale options specified.  Naked scale argument not allowed */

        if ((argc-1) > 1)
            pm_error("Too many arguments (%u).  With a scale option, "
                     "the only argument is the "
                     "optional file specification", argc-1);

        if (argc-1 > 0)
            cmdlineP->inputFilespec = argv[1];
        else
            cmdlineP->inputFilespec = "-";
    } else {
        /* scale must be specified in an argument */
        if ((argc-1) != 1 && (argc-1) != 2)
            pm_error("Wrong number of arguments (%d).  Without scale options, "
                     "you must supply 1 or 2 arguments:  scale and "
                     "optional file specification", argc-1);

        {
            const char * error;   /* error message of pm_string_to_uint */
            unsigned int scale;

            pm_string_to_uint(argv[1], &scale, &error);

            if (error == NULL) {
                if (scale == 0)
                    pm_error("Scale argument must be positive.  "
                             "You specified zero");
                else
                    cmdlineP->xScaleFactor = cmdlineP->yScaleFactor = scale;
            } else
                pm_error("Invalid scale factor: %s", error);

        }
        if (argc-1 > 1)
            cmdlineP->inputFilespec = argv[2];
        else
            cmdlineP->inputFilespec = "-";
    }
    free(option_def);
}



static void
makeOutputRowMap(tuple **     const outTupleRowP,
                 struct pam * const outpamP,
                 struct pam * const inpamP,
                 tuple *      const inTuplerow) {
/*----------------------------------------------------------------------------
   Create a tuple *outTupleRowP which is actually a row of pointers into
   inTupleRow[], so as to map input pixels to output pixels by stretching.
-----------------------------------------------------------------------------*/
    tuple * newtuplerow;
    int col;

    MALLOCARRAY_NOFAIL(newtuplerow, outpamP->width);

    for (col = 0 ; col < inpamP->width; ++col) {
        unsigned int const scaleFactor = outpamP->width / inpamP->width;
        unsigned int subcol;

        for (subcol = 0; subcol < scaleFactor; ++subcol)
            newtuplerow[col * scaleFactor + subcol] = inTuplerow[col];
    }
    *outTupleRowP = newtuplerow;
}



static void
validateComputableDimensions(unsigned int const width,
                             unsigned int const height,
                             unsigned int const xScaleFactor,
                             unsigned int const yScaleFactor) {
/*----------------------------------------------------------------------------
   Make sure that multiplication for output image width and height do not
   overflow.

   See validateComputetableSize() in libpam.c and pbm_readpbminitrest() in
   libpbm2.c
-----------------------------------------------------------------------------*/
    unsigned int const maxWidthHeight = INT_MAX - 2;
    unsigned int const maxScaleFactor = maxWidthHeight / MAX(height, width);
    unsigned int const greaterScaleFactor = MAX(xScaleFactor, yScaleFactor);

    if (greaterScaleFactor > maxScaleFactor)
        pm_error("Scale factor '%u' too large.  "
                 "The maximum for this %u x %u input image is %u.",
                 greaterScaleFactor, width, height, maxScaleFactor);
}



static void
enlargePbmRowHorizontally(struct pam *          const inpamP,
                          const unsigned char * const inrow,
                          unsigned int          const inColChars,
                          unsigned int          const outColChars,
                          unsigned int          const scaleFactor,
                          unsigned char *       const outrow) {

    static unsigned char const dbl[16] = {
        0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F,
        0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF };

    static unsigned char const trp1[8] = {
        0x00, 0x03, 0x1C, 0x1F, 0xE0, 0xE3, 0xFC, 0xFF };

    static unsigned char const trp2[16] = {
        0x00, 0x01, 0x0E, 0x0F, 0x70, 0x71, 0x7E, 0x7F,
        0x80, 0x81, 0x8E, 0x8F, 0xF0, 0xF1, 0xFE, 0xFF };

    static unsigned char const trp3[8] = {
        0x00, 0x07, 0x38, 0x3F, 0xC0, 0xC7, 0xF8, 0xFF };

    static unsigned char const quad[4] = { 0x00, 0x0F, 0xF0, 0xFF };

    static unsigned char const quin2[8] = {
        0x00, 0x01, 0x3E, 0x3F, 0xC0, 0xC1, 0xFE, 0xFF };

    static unsigned char const quin4[8] = {
        0x00, 0x03, 0x7C, 0x7F, 0x80, 0x83, 0xFC, 0xFF };

    static unsigned int const pair[4] = { 0x0000, 0x00FF, 0xFF00, 0xFFFF };

    unsigned int colChar;

    switch (scaleFactor) {
    case 1:  break; /* outrow set to inrow */
    case 2:  /* Make outrow using prefabricated parts (same for 3, 5). */
        for (colChar = 0; colChar < inColChars; ++colChar) {
            outrow[colChar*2]   = dbl[(inrow[colChar] & 0xF0) >> 4];
            outrow[colChar*2+1] = dbl[(inrow[colChar] & 0x0F) >> 0];
            /* Possible outrow overrun by one byte. */
        }
        break;

    case 3:
        for (colChar = 0; colChar < inColChars; ++colChar) {
            outrow[colChar*3]   = trp1[(inrow[colChar] & 0xF0) >> 5];
            outrow[colChar*3+1] = trp2[(inrow[colChar] >> 2) & 0x0F];
            outrow[colChar*3+2] = trp3[(inrow[colChar] >> 0) & 0x07];
        }
        break;

    case 4:
        for (colChar = 0; colChar < inColChars; ++colChar) {
            unsigned int i;
            for (i = 0; i < 4; ++i)
                outrow[colChar*4+i]=
                    quad[(inrow[colChar] >> (6 - 2 * i)) & 0x03];
        }
        break;

    case 5:
        for (colChar = 0; colChar < inColChars; ++colChar) {
            outrow[colChar*5]   = pair [(inrow[colChar] >> 6) & 0x03] >> 5;
            outrow[colChar*5+1] = quin2[(inrow[colChar] >> 4) & 0x07] >> 0;
            outrow[colChar*5+2] = quad [(inrow[colChar] >> 3) & 0x03] >> 0;
            outrow[colChar*5+3] = quin4[(inrow[colChar] >> 1) & 0x07] >> 0;
            outrow[colChar*5+4] = pair [(inrow[colChar] >> 0) & 0x03] >> 3;
        }
        break;

    case 6:  /* Compound of 2 and 3 */
        for (colChar = 0; colChar < inColChars; ++colChar) {
            unsigned char const hi = dbl[(inrow[colChar] & 0xF0) >> 4];
            unsigned char const lo = dbl[(inrow[colChar] & 0x0F) >> 0];

            outrow[colChar*6]   = trp1[(hi & 0xF0) >> 5];
            outrow[colChar*6+1] = trp2[(hi >> 2) & 0x0F];
            outrow[colChar*6+2] = trp3[hi & 0x07];

            outrow[colChar*6+3] = trp1[(lo & 0xF0) >> 5];
            outrow[colChar*6+4] = trp2[(lo >> 2) & 0x0F];
            outrow[colChar*6+5] = trp3[lo & 0x07];
        }
        break;

    case 7:
        for (colChar = 0; colChar < inColChars; ++colChar) {
            uint32_t hi, lo;

            hi = inrow[colChar] >> 4;
            hi = ((((hi>>1) * 0x00082080) | (0x01 & hi)) & 0x00204081 ) * 0x7F;
            hi >>= 4;
            outrow[colChar*7]   =  (unsigned char) ( hi >> 16);
            outrow[colChar*7+1] =  (unsigned char) ((hi >>  8) & 0xFF);
            outrow[colChar*7+2] =  (unsigned char) ((hi >>  0) & 0xFF);

            lo = inrow[colChar] & 0x001F;
            lo = ((((lo>>1) * 0x02082080) | (0x01 & lo)) & 0x10204081 ) * 0x7F;
            outrow[colChar*7+3] =  (unsigned char) ((lo >> 24) & 0xFF);
            outrow[colChar*7+4] =  (unsigned char) ((lo >> 16) & 0xFF);
            outrow[colChar*7+5] =  (unsigned char) ((lo >>  8) & 0xFF);
            outrow[colChar*7+6] =  (unsigned char) ((lo >>  0) & 0xFF);
        }
        break;

    case 8:
        for (colChar = 0; colChar < inColChars; ++colChar) {
            unsigned int i;
            for (i = 0; i < 8; ++i) {
                outrow[colChar*8+i] =
                    ((inrow[colChar] >> (7-i)) & 0x01) *0xFF;
            }
        }
        break;

    case 9:
        for (colChar = 0; colChar < inColChars; ++colChar) {
            outrow[colChar*9]   =  ((inrow[colChar] >> 7) & 0x01) * 0xFF;
            outrow[colChar*9+1] =  pair[(inrow[colChar] >> 6) & 0x03] >> 1;
            outrow[colChar*9+2] =  pair[(inrow[colChar] >> 5) & 0x03] >> 2;
            outrow[colChar*9+3] =  pair[(inrow[colChar] >> 4) & 0x03] >> 3;
            outrow[colChar*9+4] =  pair[(inrow[colChar] >> 3) & 0x03] >> 4;
            outrow[colChar*9+5] =  pair[(inrow[colChar] >> 2) & 0x03] >> 5;
            outrow[colChar*9+6] =  pair[(inrow[colChar] >> 1) & 0x03] >> 6;
            outrow[colChar*9+7] =  pair[(inrow[colChar] >> 0) & 0x03] >> 7;
            outrow[colChar*9+8] =  (inrow[colChar] & 0x01) * 0xFF;
        }
        break;

    case 10:
        for (colChar = 0; colChar < inColChars; ++colChar) {
            outrow[colChar*10]   = ((inrow[colChar] >> 7) & 0x01 ) * 0xFF;
            outrow[colChar*10+1] = pair[(inrow[colChar] >> 6) & 0x03] >> 2;
            outrow[colChar*10+2] = pair[(inrow[colChar] >> 5) & 0x03] >> 4;
            outrow[colChar*10+3] = pair[(inrow[colChar] >> 4) & 0x03] >> 6;
            outrow[colChar*10+4] = ((inrow[colChar] >> 4) & 0x01) * 0xFF;
            outrow[colChar*10+5] = ((inrow[colChar] >> 3) & 0x01) * 0xFF;
            outrow[colChar*10+6] = pair[(inrow[colChar] >> 2) & 0x03] >> 2;
            outrow[colChar*10+7] = pair[(inrow[colChar] >> 1) & 0x03] >> 4;
            outrow[colChar*10+8] = pair[(inrow[colChar] >> 0) & 0x03] >> 6;
            outrow[colChar*10+9] = ((inrow[colChar] >> 0) & 0x01) * 0xFF;
        }
        break;


    default:
        /*  Unlike the above cases, we iterate through outrow.  To compute the
            color composition of each outrow byte, we consult a single bit or
            two consecutive bits in inrow.

            Color changes never happen twice in a single outrow byte.

            This is a generalization of above routines for scale factors
            9 and 10.

            Logic works for scale factors 4, 6, 7, 8, and above (but not 5).
        */

        for (colChar = 0; colChar < outColChars; ++colChar) {
            unsigned int const mult = scaleFactor;
            unsigned int const mod = colChar % mult;
            unsigned int const bit = (mod*8)/mult;
            /* source bit position, leftmost=0 */
            unsigned int const offset = mult - (mod*8)%mult;
            /* number of outrow bits derived from the same
               "source" inrow bit, starting at and to the right
               of leftmost bit of outrow byte, inclusive
            */

            if (offset >= 8)  /* Bits in outrow byte are all 1 or 0 */
                outrow[colChar] =
                    (inrow[colChar/mult] >> (7-bit) & 0x01) * 0xFF;
            else           /* Two inrow bits influence this outrow byte */
                outrow[colChar] = (unsigned char)
                    (pair[inrow[colChar/mult] >> (6-bit) & 0x03] >> offset)
                    & 0xFF;
        }
    }
}



static void
enlargePbm(struct pam * const inpamP,
           unsigned int const xScaleFactor,
           unsigned int const yScaleFactor,
           FILE *       const ofP) {

    unsigned char * inrow;
    unsigned char * outrow;

    unsigned int row;

    unsigned int const outcols = inpamP->width * xScaleFactor;
    unsigned int const outrows = inpamP->height * yScaleFactor;
    unsigned int const inColChars  = pbm_packed_bytes(inpamP->width);
    unsigned int const outColChars = pbm_packed_bytes(outcols);

    inrow  = pbm_allocrow_packed(inpamP->width);

    if (xScaleFactor == 1)
        outrow = inrow;
    else  {
        /* Allow writes beyond outrow data end when xScaleFactor is
           one of the special fast cases: 2, 3, 4, 5, 6, 7, 8, 9, 10.
        */
        unsigned int const rightPadding =
            xScaleFactor > 10 ? 0 : (xScaleFactor - 1) * 8;
        outrow = pbm_allocrow_packed(outcols + rightPadding);
    }

    pbm_writepbminit(ofP, outcols, outrows, 0);

    for (row = 0; row < inpamP->height; ++row) {
        unsigned int i;

        pbm_readpbmrow_packed(inpamP->file, inrow, inpamP->width,
                              inpamP->format);

        if (outcols % 8 > 0)           /* clean final partial byte */
            pbm_cleanrowend_packed(inrow, inpamP->width);

        enlargePbmRowHorizontally(inpamP, inrow, inColChars, outColChars,
                                  xScaleFactor, outrow);

        for (i = 0; i < yScaleFactor; ++i)
            pbm_writepbmrow_packed(ofP, outrow, outcols, 0);
    }

    if (outrow != inrow)
        pbm_freerow(outrow);

    pbm_freerow(inrow);
}



static void
enlargeGeneral(struct pam * const inpamP,
               unsigned int const xScaleFactor,
               unsigned int const yScaleFactor,
               FILE *       const ofP) {
/*----------------------------------------------------------------------------
   Enlarge the input image described by *pamP.

   Assume the dimensions won't cause an arithmetic overflow.

   This works on all kinds of images, but is slower than enlargePbm on
   PBM.
-----------------------------------------------------------------------------*/
    struct pam outpam;
    tuple * tuplerow;
    tuple * newtuplerow;
    unsigned int row;

    outpam = *inpamP;
    outpam.file   = ofP;
    outpam.width  = inpamP->width  * xScaleFactor;
    outpam.height = inpamP->height * yScaleFactor;

    pnm_writepaminit(&outpam);

    tuplerow = pnm_allocpamrow(inpamP);

    makeOutputRowMap(&newtuplerow, &outpam, inpamP, tuplerow);

    for (row = 0; row < inpamP->height; ++row) {
        pnm_readpamrow(inpamP, tuplerow);
        pnm_writepamrowmult(&outpam, newtuplerow, yScaleFactor);
    }

    free(newtuplerow);

    pnm_freepamrow(tuplerow);
}



int
main(int           argc,
     const char ** const argv) {

    struct cmdlineInfo cmdline;
    FILE * ifP;
    struct pam inpam;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFilespec);

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    assert(cmdline.xScaleFactor > 0);
    assert(cmdline.yScaleFactor > 0);

    validateComputableDimensions(inpam.width, inpam.height,
                                 cmdline.xScaleFactor, cmdline.yScaleFactor);

    if (PNM_FORMAT_TYPE(inpam.format) == PBM_TYPE)
        enlargePbm(&inpam, cmdline.xScaleFactor, cmdline.yScaleFactor,
                   stdout);
    else
        enlargeGeneral(&inpam, cmdline.xScaleFactor, cmdline.yScaleFactor,
                       stdout);

    pm_close(ifP);
    pm_close(stdout);

    return 0;
}


