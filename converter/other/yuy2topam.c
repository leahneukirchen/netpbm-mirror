/* Convert an YUY2 image to a PAM image
 *
 * See
 * http://msdn.microsoft.com/en-us/library/aa904813%28VS.80%29.aspx#yuvformats_2
 * and http://www.digitalpreservation.gov/formats/fdd/fdd000364.shtml for
 * details.
 *
 * By Michael Haardt 2014.
 *
 * Contributed to the public domain by its author.
 *
 * Recoded in Netpbm style by Bryan Henderson
 */

#include <stdio.h>
#include <string.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "pm.h"
#include "pam.h"
#include "shhopt.h"



struct CmdlineInfo {
    const char * inputFileName;
    unsigned int width;
    unsigned int height;
};



static void 
parseCommandLine(int argc, const char ** argv, 
                 struct CmdlineInfo * const cmdlineP) {
/* --------------------------------------------------------------------------
   Parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.  

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
--------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options. */
    optStruct3 opt;

    unsigned int widthSpec, heightSpec;
    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "width",    OPT_UINT,
            &cmdlineP->width,   &widthSpec,                             0);
    OPTENT3(0, "height",   OPT_UINT,
            &cmdlineP->height,  &heightSpec,                            0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;   /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!widthSpec)
        pm_error("You must specify the image width with -width");
    if (cmdlineP->width == 0)
        pm_error("-width cannot be zero");

    if (cmdlineP->width % 2 != 0)
        pm_error("-width %u is odd, but YUY2 images must have an even width.",
                 cmdlineP->width);

    if (!heightSpec)
        pm_error("You must specify the image height with -height");
    if (cmdlineP->height == 0)
        pm_error("-height cannot be zero");

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else {
        cmdlineP->inputFileName = argv[1];
        
        if (argc-1 > 1)
            pm_error("Too many arguments (%u).  The only non-option argument "
                     "is the input file name.", argc-1);
    }
}



typedef struct {
    int y0;
    int y1;
    int u;
    int v;
} Yuy2Pixel;



static Yuy2Pixel
readPixel(FILE * const ifP) {
/*----------------------------------------------------------------------------
   Read one pixel from the YUY2 input.  YUY2 represents a pixel in 4 bytes.
-----------------------------------------------------------------------------*/
    Yuy2Pixel retval;
    unsigned char c;

    pm_readcharu(ifP, &c); retval.y0 = c -  16;
    pm_readcharu(ifP, &c); retval.u  = c - 128;
    pm_readcharu(ifP, &c); retval.y1 = c -  16;
    pm_readcharu(ifP, &c); retval.v  = c - 128;

    return retval;
}



typedef struct {
    int a1;
    int a2;
    int a3;
    int a4;
} UvCoeff;

typedef struct {
    int a0a;
    int a0b;
    UvCoeff uv;
} Coeff;



static Coeff
coeffFromYuy2(Yuy2Pixel const yuy2) {

    Coeff retval;

    retval.a0a   = 298 * yuy2.y0;
    retval.a0b   = 298 * yuy2.y1;
    retval.uv.a1 = 409 * yuy2.v;
    retval.uv.a2 = 100 * yuy2.u;
    retval.uv.a3 = 208 * yuy2.v;
    retval.uv.a4 = 516 * yuy2.u;

    return retval;
}



typedef struct {
    int r;
    int g;
    int b;
} Rgb;



static Rgb
rgbFromCoeff(int     const a0,
             UvCoeff const uv) {

    Rgb retval;

    retval.r = (a0 + uv.a1 + 128) >> 8;
    retval.g = (a0 - uv.a2 - uv.a3 + 128) >> 8;
    retval.b = (a0 + uv.a4 + 128) >> 8;

    return retval;
}



static Rgb
rgbFromCoeff0(Coeff const coeff) {

    return rgbFromCoeff(coeff.a0a, coeff.uv);
}



static Rgb
rgbFromCoeff1(Coeff const coeff) {

    return rgbFromCoeff(coeff.a0b, coeff.uv);
}



static void
rgbToTuple(Rgb   const rgb,
           tuple const out) {

    out[PAM_RED_PLANE] = MIN(255, MAX(0, rgb.r));
    out[PAM_GRN_PLANE] = MIN(255, MAX(0, rgb.g));
    out[PAM_BLU_PLANE] = MIN(255, MAX(0, rgb.b));
}



static void
yuy2topam(const char * const fileName,
          unsigned int const width,
          unsigned int const height) {

    FILE * ifP;
    struct pam outpam;
    tuple * tuplerow;
    unsigned int row;

    outpam.size             = sizeof(struct pam);
    outpam.len              = PAM_STRUCT_SIZE(allocation_depth);
    outpam.file             = stdout;
    outpam.format           = PAM_FORMAT;
    outpam.plainformat      = 0;
    outpam.width            = width;
    outpam.height           = height;
    outpam.depth            = 3;
    outpam.maxval           = 255;
    outpam.bytes_per_sample = 1;
    strcpy(outpam.tuple_type, PAM_PPM_TUPLETYPE);
    outpam.allocation_depth = 3;

    ifP = pm_openr(fileName);

    pnm_writepaminit(&outpam);

    tuplerow = pnm_allocpamrow(&outpam);

    for (row = 0; row < outpam.height; ++row) {
        unsigned int col;

        for (col = 0; col < outpam.width; col += 2) {
            Yuy2Pixel const yuy2 = readPixel(ifP);

            Coeff const coeff = coeffFromYuy2(yuy2);

            rgbToTuple(rgbFromCoeff0(coeff), tuplerow[col]);
            rgbToTuple(rgbFromCoeff1(coeff), tuplerow[col+1]);
        }
        pnm_writepamrow(&outpam, tuplerow);
    }
    pnm_freepamrow(tuplerow);

    pm_closer(ifP);
}



int
main(int argc, const char *argv[]) {

    struct CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    yuy2topam(cmdline.inputFileName, cmdline.width, cmdline.height);

    return 0;
}
