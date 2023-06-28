/*============================================================================
                                pamcut
==============================================================================
  Cut a rectangle out of a Netpbm image

  This is inspired by and intended as a replacement for Pnmcut by
  Jef Poskanzer, 1989.

  By Bryan Henderson, San Jose CA.  Contributed to the public domain
  by its author.
============================================================================*/

#include <limits.h>
#include <assert.h>

#include "pm_c_util.h"
#include "pam.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "nstring.h"

#define UNSPEC INT_MAX
    /* UNSPEC is the value we use for an argument that is not specified
       by the user.  Theoretically, the user could specify this value,
       but we hope not.
       */

typedef struct {
/*----------------------------------------------------------------------------
   A location in one dimension (row or column) in the image.
-----------------------------------------------------------------------------*/
    enum { LOCTYPE_NONE, LOCTYPE_FROMNEAR, LOCTYPE_FROMFAR } locType;

    unsigned int n;
        /* Row or column count.

           If LOCTYPE_NONE: Meaningless

           If LOCTYPE_FROMFAR: Number of columns from the far edge of the image
           (right or bottom).  Last column/row is 1.

           If LOCTYPE_FROMNEAR: Number of columns from the near edge of the
           image (left or top).  First column/row is 0.
        */
} Location;



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* File name of input file */

    /* The following describe the rectangle the user wants to cut out.
       the value UNSPEC for any of them indicates that value was not
       specified.  A negative value means relative to the far edge.
       'width' and 'height' are not negative.  These specifications
       do not necessarily describe a valid rectangle; they are just
       what the user said.

       These do not follow the Netpbm convention of having members of this
       structure that are identical to the name of an option class being
       the value of that option.  'left', for example, is not the value of
       the -left option; it could reflect the value of a -cropleft option
       instead.
    */
    Location leftLoc;
    Location rghtLoc;
    Location topLoc;
    Location botLoc;
    unsigned int widthSpec;
    unsigned int width;
    unsigned int heightSpec;
    unsigned int height;
    unsigned int pad;
    unsigned int verbose;
};



static void
parseLegacyLocationArgs(const char **        const argv,
                        struct CmdlineInfo * const cmdlineP) {

    int leftArg, topArg, widthArg, heightArg;

    {
        const char * error;
        pm_string_to_int(argv[1], &leftArg,   &error);
        if (error)
            pm_error("Invalid number for left column argument.  %s", error);
    }
    {
        const char * error;
        pm_string_to_int(argv[2], &topArg,    &error);
        if (error)
            pm_error("Invalid number for top row argument.  %s",     error);
    }
    {
        const char * error;
        pm_string_to_int(argv[3], &widthArg,  &error);
        if (error)
            pm_error("Invalid number for width argument.  %s",       error);
    }
    {
        const char * error;
        pm_string_to_int(argv[4], &heightArg, &error);
        if (error)
            pm_error("Invalid number for height argument.  %s",      error);
    }

    if (leftArg < 0) {
        cmdlineP->leftLoc.locType = LOCTYPE_FROMFAR;
        cmdlineP->leftLoc.n       = -leftArg;
    } else {
        cmdlineP->leftLoc.locType = LOCTYPE_FROMNEAR;
        cmdlineP->leftLoc.n       = leftArg;
    }
    if (topArg < 0) {
        cmdlineP->topLoc.locType = LOCTYPE_FROMFAR;
        cmdlineP->topLoc.n       = -topArg;
    } else {
        cmdlineP->topLoc.locType = LOCTYPE_FROMNEAR;
        cmdlineP->topLoc.n       = topArg;
    }
    if (widthArg > 0) {
        cmdlineP->width = widthArg;
        cmdlineP->widthSpec = 1;
        cmdlineP->rghtLoc.locType = LOCTYPE_NONE;
    } else {
        cmdlineP->widthSpec = 0;
        cmdlineP->rghtLoc.locType = LOCTYPE_FROMFAR;
        cmdlineP->rghtLoc.n = -(widthArg - 1);
    }
    if (heightArg > 0) {
        cmdlineP->height = heightArg;
        cmdlineP->heightSpec = 1;
        cmdlineP->botLoc.locType = LOCTYPE_NONE;
    } else {
        cmdlineP->heightSpec = 0;
        cmdlineP->botLoc.locType = LOCTYPE_FROMFAR;
        cmdlineP->botLoc.n = -(heightArg - 1);
    }
}



static void
parseCommandLine(int argc, const char ** const argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;
    unsigned int option_def_index;

    int left, right, top, bottom;
    unsigned int cropleftSpec, croprightSpec, croptopSpec, cropbottomSpec;
    unsigned int cropleft, cropright, croptop, cropbottom;
    unsigned int leftSpec, rightSpec, topSpec, bottomSpec;

    bool haveLegacyLocationArgs;
        /* The user specified location with top, left, height, and width
           arguments like in original Pnmcut instead of with named options.
        */

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "left",       OPT_INT,    &left,       &leftSpec,          0);
    OPTENT3(0,   "right",      OPT_INT,    &right,      &rightSpec,         0);
    OPTENT3(0,   "top",        OPT_INT,    &top,        &topSpec,           0);
    OPTENT3(0,   "bottom",     OPT_INT,    &bottom,     &bottomSpec,        0);
    OPTENT3(0,   "cropleft",   OPT_UINT,   &cropleft,   &cropleftSpec,      0);
    OPTENT3(0,   "cropright",  OPT_UINT,   &cropright,  &croprightSpec,     0);
    OPTENT3(0,   "croptop",    OPT_UINT,   &croptop,    &croptopSpec,       0);
    OPTENT3(0,   "cropbottom", OPT_UINT,   &cropbottom, &cropbottomSpec,    0);
    OPTENT3(0,   "width",      OPT_UINT,   &cmdlineP->width,
            &cmdlineP->widthSpec,       0);
    OPTENT3(0,   "height",     OPT_UINT,   &cmdlineP->height,
            &cmdlineP->heightSpec,      0);
    OPTENT3(0,   "pad",        OPT_FLAG,   NULL, &cmdlineP->pad,           0);
    OPTENT3(0,   "verbose",    OPT_FLAG,   NULL, &cmdlineP->verbose,       0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = TRUE;  /* We may have parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (cmdlineP->widthSpec && cmdlineP->width == 0)
        pm_error("-width may not be zero.");
    if (cmdlineP->heightSpec && cmdlineP->height == 0)
        pm_error("-height may not be zero.");

    if ((argc-1) != 0 && (argc-1) != 1 && (argc-1) != 4 && (argc-1) != 5)
        pm_error("Wrong number of arguments: %u.  The only argument in "
                 "the preferred syntax is an optional input file name.  "
                 "In older syntax, there are also forms with 4 and 5 "
                 "arguments.", argc-1);

    switch (argc-1) {
    case 0:
        cmdlineP->inputFileName = "-";
        haveLegacyLocationArgs = false;
        break;
    case 1:
        cmdlineP->inputFileName = argv[1];
        haveLegacyLocationArgs = false;
        break;
    case 4:
        cmdlineP->inputFileName = "-";
        haveLegacyLocationArgs = true;
        break;
    case 5:
        cmdlineP->inputFileName = argv[5];
        haveLegacyLocationArgs = true;
        break;
    }

    if (haveLegacyLocationArgs)
        parseLegacyLocationArgs(argv, cmdlineP);
    else {
        if (leftSpec && cropleftSpec)
            pm_error("You cannot specify both -left and -cropleft");
        if (leftSpec) {
            if (left >= 0) {
                cmdlineP->leftLoc.locType = LOCTYPE_FROMNEAR;
                cmdlineP->leftLoc.n       = left;
            } else {
                cmdlineP->leftLoc.locType = LOCTYPE_FROMFAR;
                cmdlineP->leftLoc.n       = -left;
            }
        } else if (cropleftSpec) {
            cmdlineP->leftLoc.locType = LOCTYPE_FROMNEAR;
            cmdlineP->leftLoc.n       = cropleft;
        } else
            cmdlineP->leftLoc.locType = LOCTYPE_NONE;

        if (rightSpec && croprightSpec)
            pm_error("You cannot specify both -right and -cropright");
        if (rightSpec) {
            if (right >= 0) {
                cmdlineP->rghtLoc.locType = LOCTYPE_FROMNEAR;
                cmdlineP->rghtLoc.n       = right;
            } else {
                cmdlineP->rghtLoc.locType = LOCTYPE_FROMFAR;
                cmdlineP->rghtLoc.n       = -right;
            }
        } else if (croprightSpec) {
            cmdlineP->rghtLoc.locType = LOCTYPE_FROMFAR;
            cmdlineP->rghtLoc.n       = 1 + cropright;
        } else
            cmdlineP->rghtLoc.locType = LOCTYPE_NONE;

        if (topSpec && croptopSpec)
            pm_error("You cannot specify both -top and -croptop");
        if (topSpec) {
            if (top >= 0) {
                cmdlineP->topLoc.locType = LOCTYPE_FROMNEAR;
                cmdlineP->topLoc.n       = top;
            } else {
                cmdlineP->topLoc.locType = LOCTYPE_FROMFAR;
                cmdlineP->topLoc.n       = -top;
            }
        } else if (croptopSpec) {
            cmdlineP->topLoc.locType = LOCTYPE_FROMNEAR;
            cmdlineP->topLoc.n       = croptop;
        } else
            cmdlineP->topLoc.locType = LOCTYPE_NONE;

        if (bottomSpec && cropbottomSpec)
            pm_error("You cannot specify both -bottom and -cropbottom");
        if (bottomSpec) {
            if (bottom >= 0) {
                cmdlineP->botLoc.locType = LOCTYPE_FROMNEAR;
                cmdlineP->botLoc.n       = bottom;
            } else {
                cmdlineP->botLoc.locType = LOCTYPE_FROMFAR;
                cmdlineP->botLoc.n       = -bottom;
            }
        } else if (cropbottomSpec) {
            cmdlineP->botLoc.locType = LOCTYPE_FROMFAR;
            cmdlineP->botLoc.n       = 1 + cropbottom;
        } else
            cmdlineP->botLoc.locType = LOCTYPE_NONE;
    }
}


static int
near(Location     const loc,
     unsigned int const edge) {

    int retval;

    switch (loc.locType) {
    case LOCTYPE_NONE:
        assert(false);
        retval = 0;
        break;
    case LOCTYPE_FROMNEAR:
        retval = loc.n;
        break;
    case LOCTYPE_FROMFAR:
        retval = (int)edge - (int)loc.n;
    }

    return retval;
}



static void
computeCutBounds(unsigned int const cols,
                 unsigned int const rows,
                 Location     const leftArg,
                 Location     const rghtArg,
                 Location     const topArg,
                 Location     const botArg,
                 bool         const widthSpec,
                 unsigned int const widthArg,
                 bool         const heightSpec,
                 unsigned int const heightArg,
                 int *        const leftColP,
                 int *        const rghtColP,
                 int *        const topRowP,
                 int *        const botRowP) {
/*----------------------------------------------------------------------------
   From the values given on the command line 'leftArg', 'rghtArg', 'topArg',
   'botArg', 'widthArg', and 'heightArg', determine what rectangle the user
   wants cut out.

   Return the location of the rectangle as *leftcolP, *rghtcolP, *toprowP, and
   *botrowP.  Any of these can be outside the image, including by being
   negative.
-----------------------------------------------------------------------------*/
    /* Find left and right bounds */

    if (widthSpec)
        assert(widthArg > 0);

    if (leftArg.locType == LOCTYPE_NONE) {
        if (rghtArg.locType == LOCTYPE_NONE) {
            *leftColP = 0;
            if (widthSpec)
                *rghtColP = 0 + (int)widthArg - 1;
            else
                *rghtColP = (int)cols - 1;
        } else {
            *rghtColP = near(rghtArg, cols);
            if (widthSpec)
                *leftColP = near(rghtArg, cols) - (int)widthArg + 1;
            else
                *leftColP = 0;
        }
    } else {
        *leftColP = near(leftArg, cols);
        if (rghtArg.locType == LOCTYPE_NONE) {
            if (widthSpec)
                *rghtColP = near(leftArg, cols) + (int)widthArg - 1;
            else
                *rghtColP = (int)cols - 1;
        } else {
            if (widthSpec) {
                pm_error("You may not specify left, right, and width.  "
                         "Choose at most two of these.");
            } else
                *rghtColP = near(rghtArg, cols);
        }
    }

    /* Find top and bottom bounds */

    if (heightSpec)
        assert(heightArg > 0);

    if (topArg.locType == LOCTYPE_NONE) {
        if (botArg.locType == LOCTYPE_NONE) {
            *topRowP = 0;
            if (heightSpec)
                *botRowP = 0 + (int)heightArg - 1;
            else
                *botRowP = (int)rows - 1;
        } else {
            *botRowP = near(botArg, rows);
            if (heightSpec)
                *topRowP = near(botArg, rows) - (int)heightArg + 1;
            else
                *topRowP = 0;
        }
    } else {
        *topRowP = near(topArg, rows);
        if (botArg.locType == LOCTYPE_NONE) {
            if (heightSpec)
                *botRowP = near(topArg, rows) + (int)heightArg - 1;
            else
                *botRowP = (int)rows - 1;
        } else {
            if (heightSpec) {
                pm_error("You may not specify top, bottom, and height.  "
                         "Choose at most two of these.");
            } else
                *botRowP = near(botArg, rows);
        }
    }
}



static void
rejectOutOfBounds(unsigned int const cols,
                  unsigned int const rows,
                  int          const leftcol,
                  int          const rightcol,
                  int          const toprow,
                  int          const bottomrow,
                  bool         const pad) {

     /* Reject coordinates off the edge */

    if (!pad) {
        if (leftcol < 0)
            pm_error("You have specified a left edge (%d) that is beyond "
                     "the left edge of the image (0)", leftcol);
        if (leftcol > (int)(cols-1))
            pm_error("You have specified a left edge (%d) that is beyond "
                     "the right edge of the image (%u)", leftcol, cols-1);
        if (rightcol < 0)
            pm_error("You have specified a right edge (%d) that is beyond "
                     "the left edge of the image (0)", rightcol);
        if (rightcol > (int)(cols-1))
            pm_error("You have specified a right edge (%d) that is beyond "
                     "the right edge of the image (%u)", rightcol, cols-1);
        if (toprow < 0)
            pm_error("You have specified a top edge (%d) that is above "
                     "the top edge of the image (0)", toprow);
        if (toprow > (int)(rows-1))
            pm_error("You have specified a top edge (%d) that is below "
                     "the bottom edge of the image (%u)", toprow, rows-1);
        if (bottomrow < 0)
            pm_error("You have specified a bottom edge (%d) that is above "
                     "the top edge of the image (0)", bottomrow);
        if (bottomrow > (int)(rows-1))
            pm_error("You have specified a bottom edge (%d) that is below "
                     "the bottom edge of the image (%u)", bottomrow, rows-1);
    }

    if (leftcol > rightcol)
        pm_error("You have specified a left edge (%d) that is to the right of "
                 "the right edge you specified (%d)",
                 leftcol, rightcol);

    if (toprow > bottomrow)
        pm_error("You have specified a top edge (%d) that is below "
                 "the bottom edge you specified (%d)",
                 toprow, bottomrow);
}



static void
writeBlackRows(const struct pam * const outpamP,
               int                const rows) {
/*----------------------------------------------------------------------------
   Write out 'rows' rows of black tuples of the image described by *outpamP.

   Unless our input image is PBM, PGM, or PPM, or PAM equivalent, we
   don't really know what "black" means, so this is just something
   arbitrary in that case.
-----------------------------------------------------------------------------*/
    tuple blackTuple;
    tuple * blackRow;
    int col;

    pnm_createBlackTuple(outpamP, &blackTuple);

    MALLOCARRAY_NOFAIL(blackRow, outpamP->width);

    for (col = 0; col < outpamP->width; ++col)
        blackRow[col] = blackTuple;

    pnm_writepamrowmult(outpamP, blackRow, rows);

    free(blackRow);

    pnm_freepamtuple(blackTuple);
}



struct rowCutter {
/*----------------------------------------------------------------------------
   This is an object that gives you pointers you can use to effect the
   horizontal cutting and padding of a row just by doing one
   pnm_readpamrow() and one pnm_writepamrow().  It works like this:

   The array inputPointers[] contains an element for each pixel in an input
   row.  If it's a pixel that gets discarded in the cutting process,
   inputPointers[] points to a special "discard" tuple.  All thrown away
   pixels have the same discard tuple to save CPU cache space.  If it's
   a pixel that gets copied to the output, inputPointers[] points to some
   tuple to which outputPointers[] also points.

   The array outputPointers[] contains an element for each pixel in an
   output row.  If the pixel is one that gets copied from the input,
   outputPointers[] points to some tuple to which inputPointers[] also
   points.  If it's a pixel that gets padded with black, outputPointers[]
   points to a constant black tuple.  All padded pixels have the same
   constant black tuple to save CPU cache space.

   For example, if you have a three pixel input row and are cutting
   off the right two pixels, inputPointers[0] points to copyTuples[0]
   and inputPointers[1] and inputPointers[2] point to discardTuple.
   outputPointers[0] points to copyTuples[0].

   We arrange to have the padded parts of the output row filled with
   black tuples.  Unless the input image is PBM, PGM, or PPM, or PAM
   equivalent, we don't really know what "black" means, so we fill with
   something arbitrary in that case.
-----------------------------------------------------------------------------*/
    tuple * inputPointers;
    tuple * outputPointers;

    unsigned int inputWidth;
    unsigned int outputWidth;

    /* The following are the tuples to which inputPointers[] and
       outputPointers[] may point.
    */
    tuple * copyTuples;
    tuple blackTuple;
    tuple discardTuple;
};



/* In a typical multi-image stream, all the images have the same
   dimensions, so this program creates and destroys identical row
   cutters for each image in the stream.  If that turns out to take a
   significant amount of resource to do, we should create a cache:
   keep the last row cutter made, tagged by the parameters used to
   create it.  If the parameters are the same for the next image, we
   just use that cached row cutter; otherwise, we discard it and
   create a new one then.
*/



static void
createRowCutter(const struct pam *  const inpamP,
                const struct pam *  const outpamP,
                int                 const leftcol,
                int                 const rightcol,
                struct rowCutter ** const rowCutterPP) {

    struct rowCutter * rowCutterP;
    tuple * inputPointers;
    tuple * outputPointers;
    tuple * copyTuples;
    tuple blackTuple;
    tuple discardTuple;
    int col;

    assert(inpamP->depth >= outpamP->depth);
        /* Entry condition.  If this weren't true, we could not simply
           treat an input tuple as an output tuple.
        */

    copyTuples   = pnm_allocpamrow(outpamP);
    discardTuple = pnm_allocpamtuple(inpamP);
    pnm_createBlackTuple(outpamP, &blackTuple);

    MALLOCARRAY_NOFAIL(inputPointers,  inpamP->width);
    MALLOCARRAY_NOFAIL(outputPointers, outpamP->width);

    /* Put in left padding */
    for (col = leftcol; col < 0 && col-leftcol < outpamP->width; ++col)
        outputPointers[col-leftcol] = blackTuple;

    /* Put in extracted columns */
    for (col = MAX(leftcol, 0);
         col <= MIN(rightcol, inpamP->width-1);
         ++col) {
        int const outcol = col - leftcol;

        inputPointers[col] = outputPointers[outcol] = copyTuples[outcol];
    }

    /* Put in right padding */
    for (col = MIN(rightcol, inpamP->width-1) + 1; col <= rightcol; ++col) {
        if (col - leftcol >= 0) {
            outputPointers[col-leftcol] = blackTuple;
        }
    }

    /* Direct input pixels that are getting cut off to the discard tuple */

    for (col = 0; col < MIN(leftcol, inpamP->width); ++col)
        inputPointers[col] = discardTuple;

    for (col = MAX(0, rightcol + 1); col < inpamP->width; ++col)
        inputPointers[col] = discardTuple;

    MALLOCVAR_NOFAIL(rowCutterP);

    rowCutterP->inputWidth     = inpamP->width;
    rowCutterP->outputWidth    = outpamP->width;
    rowCutterP->inputPointers  = inputPointers;
    rowCutterP->outputPointers = outputPointers;
    rowCutterP->copyTuples     = copyTuples;
    rowCutterP->discardTuple   = discardTuple;
    rowCutterP->blackTuple     = blackTuple;

    *rowCutterPP = rowCutterP;
}



static void
destroyRowCutter(struct rowCutter * const rowCutterP) {

    pnm_freepamrow(rowCutterP->copyTuples);
    pnm_freepamtuple(rowCutterP->blackTuple);
    pnm_freepamtuple(rowCutterP->discardTuple);
    free(rowCutterP->inputPointers);
    free(rowCutterP->outputPointers);

    free(rowCutterP);
}



static void
extractRowsGen(const struct pam * const inpamP,
               const struct pam * const outpamP,
               int                const leftcol,
               int                const rightcol,
               int                const toprow,
               int                const bottomrow) {

    struct rowCutter * rowCutterP;
    int row;

    /* Write out top padding */
    if (0 - toprow > 0)
        writeBlackRows(outpamP, 0 - toprow);

    createRowCutter(inpamP, outpamP, leftcol, rightcol, &rowCutterP);

    /* Read input and write out rows extracted from it */
    for (row = 0; row < inpamP->height; ++row) {
        if (row >= toprow && row <= bottomrow){
            pnm_readpamrow(inpamP, rowCutterP->inputPointers);
            pnm_writepamrow(outpamP, rowCutterP->outputPointers);
        } else  /* row < toprow || row > bottomrow */
            pnm_readpamrow(inpamP, NULL);

        /* Note that we may be tempted just to quit after reaching the bottom
           of the extracted image, but that would cause a broken pipe problem
           for the process that's feeding us the image.
        */
    }

    destroyRowCutter(rowCutterP);

    /* Write out bottom padding */
    if ((bottomrow - (inpamP->height-1)) > 0)
        writeBlackRows(outpamP, bottomrow - (inpamP->height-1));
}



static void
makeBlackPBMRow(unsigned char * const bitrow,
                unsigned int    const cols) {

    unsigned int const colByteCnt = pbm_packed_bytes(cols);

    unsigned int i;

    for (i = 0; i < colByteCnt; ++i)
        bitrow[i] = PBM_BLACK * 0xff;

    if (PBM_BLACK != 0 && cols % 8 > 0)
        bitrow[colByteCnt-1] <<= (8 - cols % 8);
}



static void
extractRowsPBM(const struct pam * const inpamP,
               const struct pam * const outpamP,
               int                const leftcol,
               int                const rightcol,
               int                const toprow,
               int                const bottomrow) {

    unsigned char * bitrow;
    int             readOffset, writeOffset;
    int             row;
    unsigned int    totalWidth;

    assert(leftcol <= rightcol);
    assert(toprow <= bottomrow);

    if (leftcol > 0) {
        totalWidth = MAX(rightcol+1, inpamP->width) + 7;
        if (totalWidth > INT_MAX - 10)
            /* Prevent overflows in pbm_allocrow_packed() */
            pm_error("Specified right edge is too far "
                     "from the right end of input image");

        readOffset  = 0;
        writeOffset = leftcol;
    } else {
        totalWidth = -leftcol + MAX(rightcol+1, inpamP->width);
        if (totalWidth > INT_MAX - 10)
            pm_error("Specified left/right edge is too far "
                     "from the left/right end of input image");

        readOffset = -leftcol;
        writeOffset = 0;
    }

    bitrow = pbm_allocrow_packed(totalWidth);

    if (toprow < 0 || leftcol < 0 || rightcol >= inpamP->width){
        makeBlackPBMRow(bitrow, totalWidth);
        if (toprow < 0) {
            int row;
            for (row=0; row < 0 - toprow; ++row)
                pbm_writepbmrow_packed(outpamP->file, bitrow,
                                       outpamP->width, 0);
        }
    }

    for (row = 0; row < inpamP->height; ++row){
        if (row >= toprow && row <= bottomrow) {
            pbm_readpbmrow_bitoffset(inpamP->file, bitrow, inpamP->width,
                                     inpamP->format, readOffset);

            pbm_writepbmrow_bitoffset(outpamP->file, bitrow, outpamP->width,
                                      0, writeOffset);

            if (rightcol >= inpamP->width)
                /* repair right padding */
                bitrow[writeOffset/8 + pbm_packed_bytes(outpamP->width) - 1] =
                    0xff * PBM_BLACK;
        } else
            pnm_readpamrow(inpamP, NULL);    /* read and discard */
    }

    if (bottomrow - (inpamP->height-1) > 0) {
        int row;
        makeBlackPBMRow(bitrow, outpamP->width);
        for (row = 0; row < bottomrow - (inpamP->height-1); ++row)
            pbm_writepbmrow_packed(outpamP->file, bitrow, outpamP->width, 0);
    }
    pbm_freerow_packed(bitrow);
}



static void
cutOneImage(FILE *             const ifP,
            struct CmdlineInfo const cmdline,
            FILE *             const ofP) {

    int leftcol, rightcol, toprow, bottomrow;
        /* Could be out of bounds, even negative */
    struct pam inpam;   /* Input PAM image */
    struct pam outpam;  /* Output PAM image */

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    computeCutBounds(inpam.width, inpam.height,
                     cmdline.leftLoc, cmdline.rghtLoc,
                     cmdline.topLoc, cmdline.botLoc,
                     cmdline.widthSpec, cmdline.width,
                     cmdline.heightSpec, cmdline.height,
                     &leftcol, &rightcol, &toprow, &bottomrow);

    rejectOutOfBounds(inpam.width, inpam.height, leftcol, rightcol,
                      toprow, bottomrow, cmdline.pad);

    if (cmdline.verbose) {
        pm_message("Image goes from Row 0, Column 0 through Row %u, Column %u",
                   inpam.height-1, inpam.width-1);
        pm_message("Cutting from Row %d, Column %d through Row %d Column %d",
                   toprow, leftcol, bottomrow, rightcol);
    }

    outpam = inpam;    /* Initial value -- most fields should be same */
    outpam.file   = ofP;
    outpam.width  = rightcol - leftcol + 1;
    outpam.height = bottomrow - toprow + 1;

    pnm_writepaminit(&outpam);

    if (PNM_FORMAT_TYPE(outpam.format) == PBM_TYPE)
        extractRowsPBM(&inpam, &outpam, leftcol, rightcol, toprow, bottomrow);
    else
        extractRowsGen(&inpam, &outpam, leftcol, rightcol, toprow, bottomrow);
}



int
main(int argc, const char *argv[]) {

    FILE * const ofP = stdout;

    struct CmdlineInfo cmdline;
    FILE * ifP;
    int eof;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    eof = FALSE;
    while (!eof) {
        cutOneImage(ifP, cmdline, ofP);
        pnm_nextimage(ifP, &eof);
    }

    pm_close(ifP);
    pm_close(ofP);

    return 0;
}
