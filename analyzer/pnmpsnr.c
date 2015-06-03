/*
 *  pnmpsnr.c: Compute error (RMSE, PSNR) between images
 *
 *
 *  Derived from pnmpnsmr by Ulrich Hafner, part of his fiasco package,
 *  On 2001.03.04.

 *  Copyright (C) 1994-2000 Ullrich Hafner <hafner@bigfoot.de>
 */

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "pam.h"
#include "shhopt.h"



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFile1Name;  /* Name of first input file */
    const char * inputFile2Name;  /* Name of second input file */
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to as as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);
    
    option_def_index = 0;   /* incremented by OPTENT3 */

    opt.opt_table     = option_def;
    opt.short_allowed = FALSE; /* We have no short (old-fashioned) options */
    opt.allowNegNum   = FALSE; /* We have no parms that are negative numbers */
    
    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others */

    if (argc-1 < 2) 
        pm_error("Takes two arguments:  names of the two files to compare");
    else {
        cmdlineP->inputFile1Name = argv[1];
        cmdlineP->inputFile2Name = argv[2];

        if (argc-1 > 2)
            pm_error("Too many arguments (%u).  The only arguments are "
                     "the names of the two files to compare", argc-1);
    }

    free(option_def);
}



static int
udiff(unsigned int const subtrahend,
      unsigned int const subtractor) {

    return subtrahend - subtractor;
}



static double
square(double const arg) {
    return(arg * arg);
}



static void
validateInput(struct pam const pam1,
              struct pam const pam2) {

    if (pam1.width != pam2.width)
        pm_error("images are not the same width, so can't be compared.  "
                 "The first is %d columns wide, "
                 "while the second is %d columns wide.",
                 pam1.width, pam2.width);
    if (pam1.height != pam2.height)
        pm_error("images are not the same height, so can't be compared.  "
                 "The first is %d rows high, "
                 "while the second is %d rows high.",
                 pam1.height, pam2.height);

    if (pam1.maxval != pam2.maxval)
        pm_error("images do not have the same maxval.  This programs works "
                 "only on like maxvals.  "
                 "The first image has maxval %u, "
                 "while the second has %u.  Use Pamdepth to change the "
                 "maxval of one of them.",
                 (unsigned int) pam1.maxval, (unsigned int) pam2.maxval);

    if (!streq(pam1.tuple_type, pam2.tuple_type))
        pm_error("images are not of the same type.  The tuple types are "
                 "'%s' and '%s', respectively.",
                 pam1.tuple_type, pam2.tuple_type);

    if (!streq(pam1.tuple_type, PAM_PBM_TUPLETYPE) &&
        !streq(pam1.tuple_type, PAM_PGM_TUPLETYPE) &&
        !streq(pam1.tuple_type, PAM_PPM_TUPLETYPE))
        pm_error("Images are not of a PNM type.  Tuple type is '%s'",
                 pam1.tuple_type);
}


enum ColorSpaceId { COLORSPACE_YCBCR, COLORSPACE_GRAYSCALE };

typedef struct {

    enum ColorSpaceId id;

    unsigned int componentCt;

    const char * componentName[3];
        /* Only first 'componentCt' elements are valid */

} ColorSpace;


#define Y_INDEX  0
#define CR_INDEX 1
#define CB_INDEX 2



static ColorSpace
yCbCrColorSpace() {

    ColorSpace retval;

    retval.id = COLORSPACE_YCBCR;

    retval.componentCt = 3;

    retval.componentName[Y_INDEX]  = "Y";
    retval.componentName[CR_INDEX] = "CR";
    retval.componentName[CB_INDEX] = "CB";

    return retval;
}



static ColorSpace
grayscaleColorSpace() {

    ColorSpace retval;

    retval.id = COLORSPACE_GRAYSCALE;

    retval.componentCt = 1;

    retval.componentName[Y_INDEX]  = "luminance";

    return retval;
}



struct SqDiff {
/*----------------------------------------------------------------------------
   The square-differences of the components of two pixels, for some
   component set.
-----------------------------------------------------------------------------*/
    double sqDiff[3];
};



static struct SqDiff
zeroSqDiff() {

    struct SqDiff retval;
    unsigned int i;

    for (i = 0; i < 3; ++i)
        retval.sqDiff[i] = 0.0;

    return retval;
}



static struct SqDiff
sqDiffSum(ColorSpace    const colorSpace,
          struct SqDiff const addend,
          struct SqDiff const adder) {

    struct SqDiff retval;
    unsigned int i;

    for (i = 0; i < colorSpace.componentCt; ++i)
        retval.sqDiff[i] = addend.sqDiff[i] + adder.sqDiff[i];

    return retval;
}



static struct SqDiff
sqDiffYCbCr(tuple    const tuple1,
            tuple    const tuple2) {

    struct SqDiff retval;

    double y1, y2, cb1, cb2, cr1, cr2;
    
    pnm_YCbCrtuple(tuple1, &y1, &cb1, &cr1);
    pnm_YCbCrtuple(tuple2, &y2, &cb2, &cr2);
    
    retval.sqDiff[Y_INDEX]  = square(y1  - y2);
    retval.sqDiff[CB_INDEX] = square(cb1 - cb2);
    retval.sqDiff[CR_INDEX] = square(cr1 - cr2);

    return retval;
}



static struct SqDiff
sqDiffGrayscale(tuple    const tuple1,
                tuple    const tuple2) {

    struct SqDiff sqDiff;

    sqDiff.sqDiff[Y_INDEX] = square(udiff(tuple1[0], tuple2[0]));

    return sqDiff;
}



static struct SqDiff
sumSqDiffFromRaster(struct pam * const pam1P,
                    struct pam * const pam2P,
                    ColorSpace   const colorSpace) {

    struct SqDiff sumSqDiff;
    tuple *tuplerow1, *tuplerow2;  /* malloc'ed */
    unsigned int row;

    tuplerow1 = pnm_allocpamrow(pam1P);
    tuplerow2 = pnm_allocpamrow(pam2P);
    
    sumSqDiff = zeroSqDiff();

    for (row = 0; row < pam1P->height; ++row) {
        unsigned int col;
        
        pnm_readpamrow(pam1P, tuplerow1);
        pnm_readpamrow(pam2P, tuplerow2);

        assert(pam1P->width == pam2P->width);

        for (col = 0; col < pam1P->width; ++col) {
            struct SqDiff sqDiff;

            switch (colorSpace.id) {
            case COLORSPACE_YCBCR:
                sqDiff = sqDiffYCbCr(tuplerow1[col], tuplerow2[col]);
                break;
            case COLORSPACE_GRAYSCALE:
                sqDiff = sqDiffGrayscale(tuplerow1[col], tuplerow2[col]);
                break;
            }
            sumSqDiff = sqDiffSum(colorSpace, sumSqDiff, sqDiff);
        }
    }

    pnm_freepamrow(tuplerow1);
    pnm_freepamrow(tuplerow2);

    return sumSqDiff;
}



static void
reportPsnr(struct pam    const pam,
           struct SqDiff const sumSqDiff,
           ColorSpace    const colorSpace,
           const char *  const fileName1,
           const char *  const fileName2) {

    double const maxSumSqDiff = square(pam.maxval) * pam.width * pam.height;
        /* Maximum possible sum square difference, i.e. the sum of the squares
           of the sample differences between an entirely white image and
           entirely black image of the given dimensions.
        */

    unsigned int i;


    /* The PSNR is the ratio of the maximum possible mean square difference
       to the actual mean square difference, which is also the ratio of
       the maximum possible sum square difference to the actual sum square
       difference.
   
       Note that in the important special case that the images are
       identical, the sum square differences are identically 0.0.
       No precision error; no rounding error.
    */

    pm_message("PSNR between '%s' and '%s':", fileName1, fileName2);

    for (i = 0; i < colorSpace.componentCt; ++i) {
        if (sumSqDiff.sqDiff[i] > 0)
            pm_message("  %s: %.2f dB",
                       colorSpace.componentName[i],
                       10 * log10(maxSumSqDiff/sumSqDiff.sqDiff[i]));
        else
            pm_message("  %s: no difference", colorSpace.componentName[i]);
    }
}



int
main (int argc, const char **argv) {
    const char * fileName1;  /* name of first file to compare */
    const char * fileName2;  /* name of second file to compare */
    FILE * if1P;
    FILE * if2P;
    struct pam pam1, pam2;
    ColorSpace colorSpace;
    
    struct CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    if1P = pm_openr(cmdline.inputFile1Name);
    if2P = pm_openr(cmdline.inputFile2Name);

    pnm_readpaminit(if1P, &pam1, PAM_STRUCT_SIZE(tuple_type));
    pnm_readpaminit(if2P, &pam2, PAM_STRUCT_SIZE(tuple_type));

    validateInput(pam1, pam2);

    if (streq(pam1.tuple_type, PAM_PPM_TUPLETYPE)) 
        colorSpace = yCbCrColorSpace();
    else
        colorSpace = grayscaleColorSpace();

    {
        struct SqDiff const sumSqDiff =
            sumSqDiffFromRaster(&pam1, &pam2, colorSpace);

        reportPsnr(pam1, sumSqDiff, colorSpace, fileName1, fileName2);
    }
    pm_close(if2P);
    pm_close(if1P);

    return 0;
}



