/*=============================================================================
                               pamrestack
===============================================================================
  Part of the Netpbm package.

  Rearrange pixels of a Netpbm image into different size rows.

  E.g. if an image is 100 pixels wide and 50 pixels high, you can rearrange it
  to 125 wide and 40 high.  In that case, 25 pixels from the 2nd row of the
  input would be moved to the end of the 1st row of input, 50 pixels from the
  3rd row would be moved to the 2nd row, etc.

  If new width is less than the input image width, move the excess pixels
  to the start (=left edge) of the next row.

  If new width is larger, complete row by bringing pixels from the start
  of the next row.

  By Akira F. Urushibata

  Contributed to the public domain by its author.
=============================================================================*/

#include <assert.h>
#include <math.h>
#include <limits.h>
#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "pam.h"
#include "shhopt.h"

static unsigned int const maxSize = INT_MAX - 10;

static void
validateWidth(double       const width,
              const char * const message) {
/*----------------------------------------------------------------------------
  Check width.  Ensure it is a value accepted by other Netpbm programs.
-----------------------------------------------------------------------------*/
    assert(maxSize < INT_MAX);

    if (width > maxSize)
        pm_error("%s %.0f is too large.", message, width);
}



static void
validateHeight(double const height) {
/*----------------------------------------------------------------------------
  Fail if image height of 'height' is too great for the computations in
  this program to work.
-----------------------------------------------------------------------------*/
    if (height > maxSize)
        pm_error("Input image is large and -width value is small."
                 "Calulated height %.0f is too large.", height);
}



enum TrimMode {TRIMMODE_NOP, TRIMMODE_FILL, TRIMMODE_CROP, TRIMMODE_ABORT};

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char *  inputFileName;
    unsigned int  width;
    unsigned int  widthSpec;
    enum TrimMode trim;
    unsigned int  verbose;
};

static void
parseCommandLine(int argc, const char ** const argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options. */
    optStruct3 opt;

    const char * trimOpt;
    unsigned int trimSpec;

    unsigned int option_def_index;

    MALLOCARRAY(option_def, 100);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = true;  /* We have no parms that are negative numbers */

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "width",         OPT_UINT,    &cmdlineP->width,
            &cmdlineP->widthSpec,     0);
    OPTENT3(0, "trim",          OPT_STRING, &trimOpt,
            &trimSpec,                0);
    OPTENT3(0, "verbose",       OPT_FLAG,   NULL,
            &cmdlineP->verbose,       0);

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
    /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    free(option_def);

    if (cmdlineP->widthSpec) {
        if (cmdlineP->width == 0)
            pm_error("Width value must be positive.  You specified 0");
        else
            validateWidth((double) cmdlineP->width,
                          "Specified -width value");
    }

    if (trimSpec) {
        if (streq(trimOpt, "fill")) {
            cmdlineP->trim = TRIMMODE_FILL;
        } else if (streq(trimOpt, "crop")) {
            cmdlineP->trim = TRIMMODE_CROP;
        } else if (streq(trimOpt, "abort")) {
            cmdlineP->trim = TRIMMODE_ABORT;
        } else
            /* NOP is not specified from the command line */
            pm_error("Invalid value for -trim: '%s'", trimOpt);
    } else
        cmdlineP->trim = TRIMMODE_FILL;  /* default */

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else {
        cmdlineP->inputFileName = argv[1];

        if (argc-1 > 1)
            pm_error("Too many arguments (%u). "
                     "The only possible argument is the input file name.",
                     argc-1);
    }
}



static void
adjustTrimMode(double          const inPixels,
               double          const outWidth,
               double          const outHeight,
               bool            const verbose,
               enum TrimMode   const originalMode,
               enum TrimMode * const adjustedModeP) {
/*----------------------------------------------------------------------------
   Adjust trim mode, taking into account the number of pixels in the
   input image and the width and height of the output image.

   Check whether conditions are met for abort.
   Set mode to NOP if all output rows will be full.
-----------------------------------------------------------------------------*/
    double const outPixels = outWidth * outHeight;

    enum TrimMode adjustedMode;

    if (inPixels == outPixels)
        adjustedMode = TRIMMODE_NOP;
    else {
        if (originalMode == TRIMMODE_ABORT)
            pm_error("Abort mode specified and input image has %.0f pixels "
                     "which is %s specified width value %.0f",
                     inPixels,
                     inPixels < outWidth ? "less than" : "not a multiple of",
                     outWidth);
        else
            adjustedMode = originalMode;
    }

    validateHeight(outHeight + (adjustedMode == TRIMMODE_FILL) ? 1 : 0);

    switch (adjustedMode) {
    case TRIMMODE_NOP:
        if (verbose)
            pm_message("Input image and output image have the same "
                       "number of pixels.");
        break;
    case TRIMMODE_FILL:
        if (verbose)
            pm_message("Output image will have %.0f more pixels "
                       "than input image.  Incomplete final row "
                       "will be padded.", inPixels - outPixels);
        break;
    case TRIMMODE_CROP:
        if (outHeight == 0)
            pm_error("No row left after cropping incomplete row. "
                     "Aborting.");
        else if (verbose)
            pm_message("Incomplete final row will be cropped.  %.0f "
                       "pixels lost.", inPixels - outPixels);
        break;
    case TRIMMODE_ABORT:
        pm_error("internal error");  /* Suppress compiler warning */
        break;
    }

    *adjustedModeP = adjustedMode;
}



/*----------------------------------------------------------------------------
  Width conversion using pointer arrays

  This program reads input rows and converts to output rows of desired
  width using a device which employs pointer arrays on both sides.
  Conceptually similar, yet more simple, devices are used in pamcut,
  pnmpad, pamflip and pnmcat.

  inputPointers[] is an expanded version of incols[] seen in many pam
  programs.  It reads multiple rows: as many rows as necessary to
  complete at least one output row.

  The read positions within inputPointers[] are fixed.  For example, if
  the input row width is 100 and inputPointers has 400 elements, the read
  positions will be: 0-99, 100-199, 200-299, 300-399.

  The outPointers[] array is set up to allow selecting elements for
  write from inputPointers[].  outPointers[] is at least as large as
  inPointers[].  The write position migrates as necessary in a cycle.
  If input width and output width are coprime and output has a
  sufficient number of rows, all positions within outPointers[]
  will be utilized.

  Once set up, the conversion device is not altered until the input image
  is completely read.

  The following are special cases in which inPointers[] and outPointers[]
  are set to the same size:

  (1) Input width and output width are equal.
  (2) Output width is an integer multiple of input width.
  (3) Input width is an integer multiple of output width.

  In cases (1) (2), the output position is fixed.
  In case (3) the output position is mobile, but all of them will start
  at integer multiples of output width.

  Note that width, height and width * height variables are of type
  "double" as a safeguard against overflows.
-----------------------------------------------------------------------------*/



static void
setOutputDimensions(struct CmdlineInfo * const cmdlineP,
                    double               const inPixelCt,
                    int *                const outWidthP,
                    int *                const outHeightP,
                    enum TrimMode *      const trimModeP) {
/*-----------------------------------------------------------------------------
  Calculate the width and height of output from the number of pixels in
  the input and command line arguments, most notably desired width.
-----------------------------------------------------------------------------*/
    double outWidth, outHeight;
    enum TrimMode adjustedMode;

    if (!cmdlineP->widthSpec) {
        outWidth = inPixelCt;
        outHeight = 1;
        validateWidth(outWidth,
                      "Input image is large and -width not specified. "
                      "Output width");
        adjustedMode = cmdlineP->trim;
    } else {
        double preAdjustedOutHeight;

        outWidth  = cmdlineP->width;
        preAdjustedOutHeight = floor(inPixelCt / outWidth);

        adjustTrimMode(inPixelCt, outWidth, preAdjustedOutHeight,
                       cmdlineP->verbose,
                       cmdlineP->trim, &adjustedMode);

        outHeight = adjustedMode == TRIMMODE_FILL ?
            preAdjustedOutHeight + 1 : preAdjustedOutHeight;
    }

    *outWidthP  = (unsigned int)outWidth;
    *outHeightP = (unsigned int)outHeight;
    *trimModeP  = adjustedMode;
}



static void
calculateInOutSize(unsigned int   const inWidth,
                   unsigned int   const outWidth,
                   unsigned int * const inputPointersWidthP,
                   unsigned int * const outputPointersWidthP) {
/*----------------------------------------------------------------------------
  Calculate array size of inPointers[] and outPointers[] from
  input width and output width.
-----------------------------------------------------------------------------*/
    double inputPointersWidth;
    double outputPointersWidth;

    if (outWidth > inWidth) {
        if (outWidth % inWidth == 0)
            inputPointersWidth = outputPointersWidth = outWidth;
        else {
            inputPointersWidth =
              (outWidth / inWidth + 1) * inWidth * 2;
            outputPointersWidth = inputPointersWidth + outWidth - 1;
        }
    }
    else if (outWidth == inWidth)
            inputPointersWidth = outputPointersWidth = outWidth;
    else { /* outWidth < inWidth) */
        if (inWidth % outWidth == 0)
            inputPointersWidth = outputPointersWidth = inWidth;
        else {
            inputPointersWidth = inWidth * 2;
            outputPointersWidth = inputPointersWidth + outWidth - 1;
        }
    }

    if(inputPointersWidth > SIZE_MAX || outputPointersWidth > SIZE_MAX)
        pm_error("Failed to set up conversion array.  Either input width, "
                 "output width or their difference is too large.");

    *inputPointersWidthP  = (unsigned int) inputPointersWidth;
    *outputPointersWidthP = (unsigned int) outputPointersWidth;
}



static void
restack(struct pam    * const inpamP,
        struct pam    * const outpamP,
        tuple         * const inputPointers,
        tuple         * const outputPointers,
        unsigned int    const inputPointersWidth,
        unsigned int    const outputPointersWidth,
        enum TrimMode   const trimMode) {
/*----------------------------------------------------------------------------
  Convert image, using inputPointers[] and outputPointers[]
-----------------------------------------------------------------------------*/
    unsigned int inoffset;
    unsigned int outoffset;
    unsigned int inPixelCt; /* Count of pixels read since last write */
    unsigned int row;

    /* Read all input and write all rows with the exception of the final
       partial row */

    for (row = 0, inoffset = 0, outoffset = 0, inPixelCt = 0;
         row < inpamP->height; ++row) {

        pnm_readpamrow(inpamP, &inputPointers[inoffset]);
        inPixelCt += inpamP->width;

        for ( ; inPixelCt >= outpamP->width; inPixelCt -= outpamP->width) {
            pnm_writepamrow(outpamP, &outputPointers[outoffset]);
            outoffset = (outoffset + outpamP->width ) % inputPointersWidth;
        }
        inoffset = (inoffset + inpamP->width) % inputPointersWidth;
    }

    /* Fill remainder of last row with black pixels and output */

    if (inPixelCt > 0 && trimMode == TRIMMODE_FILL) {
        tuple blackTuple;
        unsigned int col;

        pnm_createBlackTuple(outpamP, &blackTuple);

        for (col = inPixelCt; col < outpamP->width; ++col) {
            unsigned int const outoffset2 =
                (outoffset + col) % outputPointersWidth;
            outputPointers[outoffset2] = blackTuple;
        }

        /* output final row */
        pnm_writepamrow(outpamP, &outputPointers[outoffset]);
    }
}



static void
restackSingleImage(FILE *               const ifP,
                   struct CmdlineInfo * const cmdlineP) {

    struct pam inpam;   /* Input PAM image */
    struct pam mpam;    /* Adjusted PAM structure to read multiple rows */
    struct pam outpam;  /* Output PAM image */

    double        inPixelCt;
    enum TrimMode trimMode;
    tuple *       inputPointers;
    tuple *       outputPointers;
    unsigned int  inputPointersWidth;
    unsigned int  outputPointersWidth;

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    inPixelCt = inpam.width * inpam.height;

    outpam = inpam;

    setOutputDimensions(cmdlineP, inPixelCt, &outpam.width, &outpam.height,
                        &trimMode);

    outpam.file = stdout;

    pnm_writepaminit(&outpam);

    calculateInOutSize(inpam.width, outpam.width,
                       &inputPointersWidth, &outputPointersWidth);

    mpam = inpam;
    mpam.width = inputPointersWidth;

    inputPointers = pnm_allocpamrow(&mpam);

    if (outputPointersWidth > inputPointersWidth) {
        unsigned int col;

        MALLOCARRAY(outputPointers, outputPointersWidth);

        if (!outputPointers) {
            pm_error("Unable to allocate memory for %u output pointers",
                     outputPointersWidth);
        }

        /* Copy pointers as far as inputPointers[] goes, then wrap around */
        for (col = 0; col < outputPointersWidth; ++col)
            outputPointers[col] = inputPointers[col % inputPointersWidth];

    } else
        outputPointers = inputPointers;

    restack(&inpam, &outpam, inputPointers, outputPointers,
            inputPointersWidth, outputPointersWidth, trimMode);

    if (inputPointers != outputPointers)
        free(outputPointers);

    pnm_freepamrow(inputPointers);
}



int
main(int argc, const char * argv[]) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    int    eof;     /* no more images in input stream */

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    for (eof = false; !eof; ) {
        restackSingleImage(ifP, &cmdline);
        pnm_nextimage(ifP, &eof);
    }

    pm_close(ifP);

    return 0;
}
