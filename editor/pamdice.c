/*============================================================================
                                  pamdice
==============================================================================
  Slice a Netpbm image vertically and/or horizontally into multiple images.

  By Bryan Henderson, San Jose CA 2001.01.31

  Contributed to the public domain.
============================================================================*/

#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "pm_c_util.h"
#include "pam.h"
#include "shhopt.h"
#include "nstring.h"
#include "mallocvar.h"

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* '-' if stdin */
    const char * outstem;
    unsigned int sliceVertically;    /* boolean */
    unsigned int sliceHorizontally;  /* boolean */
    unsigned int width;    /* Meaningless if !sliceVertically */
    unsigned int height;   /* Meaningless if !sliceHorizontally */
    unsigned int hoverlap;
        /* Meaningless if !sliceVertically.  Guaranteed < width */
    unsigned int voverlap;
        /* Meaningless if !sliceHorizontally.  Guaranteed < height */
    unsigned int numberwidth;
    unsigned int numberwidthSpec;
    const char * indexfile;
    unsigned int indexfileSpec;
    const char * listfile;
    unsigned int listfileSpec;
    unsigned int dry_run;
    unsigned int verbose;
};


static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int outstemSpec, hoverlapSpec, voverlapSpec;
    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "width",       OPT_UINT,    &cmdlineP->width,
            &cmdlineP->sliceVertically,       0);
    OPTENT3(0, "height",      OPT_UINT,    &cmdlineP->height,
            &cmdlineP->sliceHorizontally,     0);
    OPTENT3(0, "hoverlap",    OPT_UINT,    &cmdlineP->hoverlap,
            &hoverlapSpec,                    0);
    OPTENT3(0, "voverlap",    OPT_UINT,    &cmdlineP->voverlap,
            &voverlapSpec,                    0);
    OPTENT3(0, "outstem",     OPT_STRING,  &cmdlineP->outstem,
            &outstemSpec,                     0);
    OPTENT3(0, "numberwidth", OPT_UINT,    &cmdlineP->numberwidth,
            &cmdlineP->numberwidthSpec,         0);
    OPTENT3(0, "indexfile",   OPT_STRING,  &cmdlineP->indexfile,
            &cmdlineP->indexfileSpec,         0);
    OPTENT3(0, "listfile",    OPT_STRING,  &cmdlineP->listfile,
            &cmdlineP->listfileSpec,          0);
    OPTENT3(0, "dry-run",     OPT_FLAG,    NULL,
            &cmdlineP->dry_run,               0);
    OPTENT3(0, "verbose",     OPT_FLAG,    NULL,
            &cmdlineP->verbose,               0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdline_p and others. */

    if (cmdlineP->sliceVertically) {
        if (cmdlineP->width < 1)
            pm_error("-width value must not be zero");
        else if (hoverlapSpec) {
            if (cmdlineP->hoverlap > cmdlineP->width - 1)
                pm_error("-hoverlap value must be less than -width (%u).  "
                         "You specified %u.",
                         cmdlineP->width, cmdlineP->hoverlap);
        } else
            cmdlineP->hoverlap = 0;
    }
    if (cmdlineP->sliceHorizontally) {
        if (cmdlineP->height < 1)
            pm_error("-height value must not be zero");
        else if (voverlapSpec) {
            if (cmdlineP->voverlap > cmdlineP->height - 1)
                pm_error("-voverlap value must be less than -height (%u).  "
                         "You specified %u.",
                         cmdlineP->height, cmdlineP->voverlap);
        } else
            cmdlineP->voverlap = 0;
    }

    if (cmdlineP->numberwidthSpec) {
        if (cmdlineP->numberwidth == 0)
            pm_error("-numberwidth value must be positive");
        else if (cmdlineP->numberwidth > 10)
            pm_error("-numberwidth value too large");
        /* Max maxval of the index file is 65535 (5 decimal digits)
           32 bit ULONG_MAX is 4294967295 (10 decimal digits)       */
    }

    if (cmdlineP->indexfileSpec && cmdlineP->listfileSpec &&
        streq(cmdlineP->indexfile,cmdlineP->listfile))
        pm_error("-indexfile name and -listfile name must be different");

    if (!outstemSpec)
        pm_error("You must specify the -outstem option to indicate where to "
                 "put the output images");

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileName = argv[1];
    else
        pm_error("Program takes at most 1 parameter: the file specification.  "
                 "You specified %u", argc-1);

    free(option_def);
}



static unsigned int
divup(unsigned int const dividend,
      unsigned int const divisor) {
/*----------------------------------------------------------------------------
   Divide 'dividend' by 'divisor' and round up to the next whole number.
-----------------------------------------------------------------------------*/
    return (dividend + divisor - 1) / divisor;
}



static void
computeSliceGeometry(struct CmdlineInfo const cmdline,
                     struct pam         const inpam,
                     bool               const verbose,
                     unsigned int *     const nHorizSliceP,
                     unsigned int *     const sliceHeightP,
                     unsigned int *     const bottomSliceHeightP,
                     unsigned int *     const nVertSliceP,
                     unsigned int *     const sliceWidthP,
                     unsigned int *     const rightSliceWidthP
                     ) {
/*----------------------------------------------------------------------------
   Compute the geometry of the slices, both common slices and possibly
   smaller remainder slices at the top and right.

   We return the following.

   *nHorizSliceP is the number of horizontal slices.  *sliceHeightP is the
   height of every slice except possibly the bottom one.  *bottomSliceHeightP
   is the height of the bottom slice.

   *nVertSliceP is the number of vertical slices.  *sliceWidthP is the width
   of every slice except possibly the rightmost one.  *rightSliceWidthP is the
   width of the rightmost slice.
-----------------------------------------------------------------------------*/
    if (cmdline.sliceHorizontally) {
        if (cmdline.height >= inpam.height)
            *nHorizSliceP = 1;
        else
            *nHorizSliceP = 1 + divup(inpam.height - cmdline.height,
                                      cmdline.height - cmdline.voverlap);
        *sliceHeightP = cmdline.height;

        *bottomSliceHeightP = inpam.height -
            (*nHorizSliceP-1) * (cmdline.height - cmdline.voverlap);
    } else {
        *nHorizSliceP       = 1;
        *sliceHeightP       = inpam.height;
        *bottomSliceHeightP = inpam.height;
    }

    if (cmdline.sliceVertically) {
        if (cmdline.width >= inpam.width)
            *nVertSliceP = 1;
        else
            *nVertSliceP = 1 + divup(inpam.width - cmdline.width,
                                     cmdline.width - cmdline.hoverlap);
        *sliceWidthP = cmdline.width;
        *rightSliceWidthP = inpam.width -
            (*nVertSliceP-1) * (cmdline.width - cmdline.hoverlap);
    } else {
        *nVertSliceP      = 1;
        *sliceWidthP      = inpam.width;
        *rightSliceWidthP = inpam.width;
    }

    if (verbose) {
        pm_message("Creating %u images, %u across by %u down; "
                   "each %u w x %u h",
                   *nVertSliceP * *nHorizSliceP,
                   *nVertSliceP, *nHorizSliceP,
                   *sliceWidthP, *sliceHeightP);
        if (*rightSliceWidthP != *sliceWidthP)
            pm_message("Right vertical slice is only %u wide",
                       *rightSliceWidthP);
        if (*bottomSliceHeightP != *sliceHeightP)
            pm_message("Bottom horizontal slice is only %u high",
                       *bottomSliceHeightP);
    }
}



static unsigned int
digitCt(unsigned int const arg) {
/*----------------------------------------------------------------------------
   Return the minimum number of digits it takes to represent the number 'arg'
   in decimal.
-----------------------------------------------------------------------------*/
    unsigned int leftover;
    unsigned int i;

    for (leftover = arg, i = 0; leftover > 0; leftover /= 10, ++i);

    return MAX(1, i);
}



static void
computeOutputFilenameFormat(int           const format,
                            unsigned int  const nHorizSlice,
                            unsigned int  const nVertSlice,
                            bool          const numberwidthSpec,
                            unsigned int  const numberwidth,
                            const char ** const filenameFormatP) {

    unsigned int const digitCtVert  =
        numberwidthSpec ? numberwidth : digitCt(nHorizSlice);
    unsigned int const digitCtHoriz =
        numberwidthSpec ? numberwidth : digitCt(nVertSlice);

    const char * filenameSuffix;

    switch(PAM_FORMAT_TYPE(format)) {
    case PPM_TYPE: filenameSuffix = "ppm"; break;
    case PGM_TYPE: filenameSuffix = "pgm"; break;
    case PBM_TYPE: filenameSuffix = "pbm"; break;
    case PAM_TYPE: filenameSuffix = "pam"; break;
    default:
        pm_error("INTERNAL ERROR: impossible value for libnetpbm image "
                 "fomat code: %d", format);
    }

    pm_asprintf(filenameFormatP, "%%s_%%0%uu_%%0%uu.%s",
                digitCtVert, digitCtHoriz, filenameSuffix);

    if (*filenameFormatP == NULL)
        pm_error("Unable to allocate memory for filename format string");
}



static void
openOutStreams(struct pam   const inpam,
               struct pam * const outpam,  /* array */
               unsigned int const horizSlice,
               unsigned int const nHorizSlice,
               unsigned int const nVertSlice,
               unsigned int const sliceHeight,
               unsigned int const sliceWidth,
               unsigned int const rightSliceWidth,
               unsigned int const hOverlap,
               bool         const numberwidthSpec,
               unsigned int const numberwidth,
               const char * const outstem) {
/*----------------------------------------------------------------------------
   Open the output files for a single horizontal slice (there's one file
   for each vertical slice) and write the Netpbm headers to them.  Also
   compute the pam structures to control each.
-----------------------------------------------------------------------------*/
    const char * filenameFormat;
    unsigned int vertSlice;

    computeOutputFilenameFormat(inpam.format, nHorizSlice, nVertSlice,
                                numberwidthSpec, numberwidth, &filenameFormat);

    for (vertSlice = 0; vertSlice < nVertSlice; ++vertSlice) {
        const char * filename;

        pm_asprintf(&filename, filenameFormat, outstem, horizSlice, vertSlice);

        if (filename == NULL)
            pm_error("Unable to allocate memory for output filename");
        else {
            outpam[vertSlice] = inpam;  /* initial value */
            outpam[vertSlice].file = pm_openw(filename);

            outpam[vertSlice].width =
                vertSlice < nVertSlice-1 ? sliceWidth : rightSliceWidth;

            outpam[vertSlice].height = sliceHeight;

            pnm_writepaminit(&outpam[vertSlice]);

            pm_strfree(filename);
        }
    }
    pm_strfree(filenameFormat);
}



static void
closeOutFiles(struct pam * const pam,
              unsigned int const nVertSlice) {

    unsigned int vertSlice;

    for (vertSlice = 0; vertSlice < nVertSlice; ++vertSlice)
        pm_close(pam[vertSlice].file);
}



static void
sliceRow(tuple *      const inputRow,
         struct pam * const outpam,
         unsigned int const nVertSlice,
         unsigned int const hOverlap) {
/*----------------------------------------------------------------------------
   Distribute the row inputRow[] across the 'nVerticalSlice' output
   files described by outpam[].  Each outpam[x] tells how many columns
   of inputRow[] to take and what their composition is.

   'hOverlap', which is meaningful only when nVertSlice is greater than 1,
   is the amount by which slices overlap each other.
-----------------------------------------------------------------------------*/
    unsigned int const sliceWidth = outpam[0].width;
    unsigned int const stride =
        nVertSlice > 1 ? sliceWidth - hOverlap : sliceWidth;

    tuple *      outputRow;
    unsigned int vertSlice;

    for (vertSlice = 0, outputRow = inputRow;
         vertSlice < nVertSlice;
         outputRow += stride, ++vertSlice) {
        pnm_writepamrow(&outpam[vertSlice], outputRow);
    }
}


/*----------------------------------------------------------------------------
   The input reader.  This just reads the input image row by row, except
   that it lets us back up up to a predefined amount (the window size).
   When we're overlapping horizontal slices, that's useful.  It's not as
   simple as just reading the entire image into memory at once, but uses
   a lot less memory.
-----------------------------------------------------------------------------*/

struct inputWindow {
    unsigned int windowSize;
    unsigned int firstRowInWindow;
    struct pam   pam;
    tuple **     rows;
};



static void
initInput(struct inputWindow * const inputWindowP,
          struct pam           const inpam,
          unsigned int         const windowSize) {

    struct pam allocPam;  /* Just for allocating the window array */
    unsigned int i;

    inputWindowP->pam = inpam;
    inputWindowP->windowSize = windowSize;

    allocPam = inpam;  /* initial value */
    allocPam.height = windowSize;

    inputWindowP->rows = pnm_allocpamarray(&allocPam);

    inputWindowP->firstRowInWindow = 0;

    /* Fill the window with the beginning of the image */
    for (i = 0; i < windowSize && i < inpam.height; ++i)
        pnm_readpamrow(&inputWindowP->pam, inputWindowP->rows[i]);
}



static void
termInputWindow(struct inputWindow * const inputWindowP) {

    struct pam freePam;  /* Just for freeing window array */

    freePam = inputWindowP->pam;
    freePam.height = inputWindowP->windowSize;

    pnm_freepamarray(inputWindowP->rows, &freePam);
}



static tuple *
getInputRow(struct inputWindow * const inputWindowP,
            unsigned int         const row) {

    if (row < inputWindowP->firstRowInWindow)
        pm_error("INTERNAL ERROR: attempt to back up too far with "
                 "getInputRow() (row %u)", row);
    if (row >= inputWindowP->pam.height)
        pm_error("INTERNAL ERROR: attempt to read beyond bottom of "
                 "input image (row %u)", row);

    while (row >= inputWindowP->firstRowInWindow + inputWindowP->windowSize) {
        tuple * const oldRow0 = inputWindowP->rows[0];
        unsigned int i;
        /* Slide the window down one row */
        for (i = 0; i < inputWindowP->windowSize - 1; ++i)
            inputWindowP->rows[i] = inputWindowP->rows[i+1];
        ++inputWindowP->firstRowInWindow;

        /* Read in the new last row in the window */
        inputWindowP->rows[i] = oldRow0;  /* Reuse the memory */
        pnm_readpamrow(&inputWindowP->pam, inputWindowP->rows[i]);
    }

    return inputWindowP->rows[row - inputWindowP->firstRowInWindow];
}

/*-----  end of input reader ----------------------------------------------*/



static void
allocOutpam(unsigned int  const nVertSlice,
            struct pam ** const outpamArrayP) {

    struct pam * outpamArray;

    MALLOCARRAY(outpamArray, nVertSlice);

    if (outpamArray == NULL)
        pm_error("Unable to allocate array for %u output pam structures.",
                 nVertSlice);

    *outpamArrayP = outpamArray;
}



static void
writeTiles(const char * const outstem,
           unsigned int const hoverlap,
           unsigned int const voverlap,
           FILE       * const ifP,
           struct pam   const inpam,
           unsigned int const sliceWidth,
           unsigned int const rightSliceWidth,
           unsigned int const sliceHeight,
           unsigned int const bottomSliceHeight,
           unsigned int const horizSliceCt,
           unsigned int const vertSliceCt,
           bool         const numberwidthSpec,
           unsigned int const numberwidth) {

    unsigned int horizSlice;
        /* Number of the current horizontal slice.  Slices are numbered
           sequentially starting at 0.
        */

    struct inputWindow inputWindow;
    struct pam * outpam;
        /* malloc'ed array.  outpam[x] is the pam structure that controls
           the current horizontal slice of vertical slice x.
        */

    allocOutpam(vertSliceCt, &outpam);

    initInput(&inputWindow, inpam, horizSliceCt > 1 ? voverlap + 1 : 1);

    for (horizSlice = 0; horizSlice < horizSliceCt; ++horizSlice) {
        unsigned int const thisSliceFirstRow =
            horizSlice > 0 ? horizSlice * (sliceHeight - voverlap) : 0;
            /* Note that 'voverlap' is not defined when there is only
               one horizontal slice
            */
        unsigned int const thisSliceHeight =
            horizSlice < horizSliceCt-1 ? sliceHeight : bottomSliceHeight;

        unsigned int row;

        openOutStreams(inpam, outpam, horizSlice, horizSliceCt, vertSliceCt,
                       thisSliceHeight, sliceWidth, rightSliceWidth,
                       hoverlap, numberwidthSpec, numberwidth, outstem);

        for (row = 0; row < thisSliceHeight; ++row) {
            tuple * const inputRow =
                getInputRow(&inputWindow, thisSliceFirstRow + row);

            sliceRow(inputRow, outpam, vertSliceCt, hoverlap);
        }
        closeOutFiles(outpam, vertSliceCt);
    }

    termInputWindow(&inputWindow);

    free(outpam);
}



static unsigned int
exp10uint(unsigned int const exponent) {
/*----------------------------------------------------------------------------
   10 raised to the power of 'exponent'.

   Abort program if result would not fit in an unsigned int.
-----------------------------------------------------------------------------*/
    unsigned int i;
    unsigned int retval;

    for (i = 0, retval = 1; i < exponent; ++i) {
        if (UINT_MAX / retval < 10)
            abort();
        retval *= 10;
    }

    return retval;
}



static sample
indexFileMaxval(unsigned int const horizSliceCt,
                unsigned int const vertSliceCt,
                bool         const numberwidthSpec,
                unsigned int const numberwidth) {
/*----------------------------------------------------------------------------
   The maxval for an index file that contains coordinates for a grid that is
   'horizSliceCt' by 'vertSliceCt'.

   Abort program if this grid dimension is not possible within the limits
   of the PAM format.
-----------------------------------------------------------------------------*/
    unsigned int maxval;

    if (horizSliceCt > PAM_OVERALL_MAXVAL + 1)
        pm_error("Too many ranks for index file.  Max is %lu",
                 PAM_OVERALL_MAXVAL + 1);
    else if (vertSliceCt > PAM_OVERALL_MAXVAL + 1)
        pm_error("Too many files for index file.  Max is %lu",
                 PAM_OVERALL_MAXVAL + 1);
    else if (numberwidthSpec)
        maxval = (unsigned int) MIN(exp10uint(numberwidth) - 1,
                                    PAM_OVERALL_MAXVAL);
    else
        maxval = MAX(horizSliceCt, vertSliceCt) <= 256 ?
            255 : PAM_OVERALL_MAXVAL;

    return maxval;
}



static void
setIndexPam(FILE *       const ofP,
            unsigned int const horizSliceCt,
            unsigned int const vertSliceCt,
            bool         const numberwidthSpec,
            unsigned int const numberwidth,
            struct pam * const indexpamP) {

    indexpamP->size        = sizeof(*indexpamP);
    indexpamP->len         = PAM_STRUCT_SIZE(tuple_type);
    indexpamP->file        = ofP;
    indexpamP->format      = PAM_FORMAT;
    indexpamP->plainformat = 0;

    indexpamP->width       = vertSliceCt;
    indexpamP->height      = horizSliceCt;
    indexpamP->depth       = 2;

    indexpamP->maxval = indexFileMaxval(horizSliceCt, vertSliceCt,
                                        numberwidthSpec, numberwidth);
    indexpamP->bytes_per_sample = pnm_bytespersample(indexpamP->maxval);
    strcpy(indexpamP->tuple_type, "grid_coord");
}



static void
writeIndexFile(const char * const indexFileNm,
               unsigned int const horizSliceCt,
               unsigned int const vertSliceCt,
               bool         const numberwidthSpec,
               unsigned int const numberwidth) {

    struct pam indexpam;
    FILE * ofP;
    tuple * indexRow;
    unsigned int horizSlice, vertSlice;

    ofP = pm_openw(indexFileNm);

    setIndexPam(ofP, horizSliceCt, vertSliceCt, numberwidthSpec, numberwidth,
                &indexpam);

    pnm_writepaminit(&indexpam);

    indexRow = pnm_allocpamrow(&indexpam);

    for (horizSlice = 0; horizSlice < horizSliceCt; ++horizSlice) {
        for (vertSlice = 0; vertSlice < vertSliceCt; ++vertSlice) {
             indexRow[vertSlice][0] = horizSlice;
             indexRow[vertSlice][1] = vertSlice;
        }
        pnm_writepamrow(&indexpam, indexRow);
    }

    pnm_freepamrow(indexRow);

    pm_close(ofP);
}



static void
writeListFile(const char * const listFileNm,
              unsigned int const horizSliceCt,
              unsigned int const vertSliceCt,
              int          const format,
              const char * const outstem,
              bool         const numberwidthSpec,
              unsigned int const numberwidth) {

    FILE * ofP;
    unsigned int horizSlice;
    const char * filenameFormat;

    ofP = pm_openw(listFileNm);

    computeOutputFilenameFormat(format, horizSliceCt, vertSliceCt,
                                numberwidthSpec, numberwidth, &filenameFormat);

    for (horizSlice = 0; horizSlice < horizSliceCt; ++horizSlice) {
        unsigned int vertSlice;
        for (vertSlice = 0; vertSlice < vertSliceCt; ++vertSlice) {
            const char * filename;

            pm_asprintf(&filename, filenameFormat,
                        outstem, horizSlice, vertSlice);

            fprintf(ofP, "%s\n", filename);

            pm_strfree(filename);
        }
    }
    pm_close(ofP);
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;
    FILE    * ifP;
    struct pam inpam;
    unsigned int sliceWidth;
        /* Width in pam columns of each vertical slice, except
           the rightmost slice, which may be narrower.  If we aren't slicing
           vertically, that means one slice, i.e. the slice width
           is the image width.
        */
    unsigned int rightSliceWidth;
        /* Width in pam columns of the rightmost vertical slice. */
    unsigned int sliceHeight;
        /* Height in pam rows of each horizontal slice, except
           the bottom slice, which may be shorter.  If we aren't slicing
           horizontally, that means one slice, i.e. the slice height
           is the image height.
        */
    unsigned int bottomSliceHeight;
        /* Height in pam rows of the bottom horizontal slice. */
    unsigned int horizSliceCt;
    unsigned int vertSliceCt;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    computeSliceGeometry(cmdline, inpam, !!cmdline.verbose,
                         &horizSliceCt, &sliceHeight, &bottomSliceHeight,
                         &vertSliceCt, &sliceWidth, &rightSliceWidth);

    if (cmdline.indexfileSpec)
        writeIndexFile(cmdline.indexfile, horizSliceCt, vertSliceCt,
                       cmdline.numberwidthSpec, cmdline.numberwidth);

    if (cmdline.listfileSpec)
        writeListFile(cmdline.listfile, horizSliceCt, vertSliceCt,
                      inpam.format, cmdline.outstem,
                      cmdline.numberwidthSpec, cmdline.numberwidth);

    if (!cmdline.dry_run)
        writeTiles(cmdline.outstem, cmdline.hoverlap, cmdline.voverlap, ifP,
                   inpam, sliceWidth, rightSliceWidth,
                   sliceHeight, bottomSliceHeight, horizSliceCt, vertSliceCt,
                   cmdline.numberwidthSpec, cmdline.numberwidth);

    pm_close(ifP);

    return 0;
}



