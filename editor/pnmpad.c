/* pnmpad.c - add border to sides of a PNM
   ** AJCD 4/9/90
 */

#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pnm.h"

#define MAX_WIDTHHEIGHT ((INT_MAX)-10)
    /* The maximum width or height value we can handle without risking
       arithmetic overflow
    */

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileNm;  /* Names of input files */
    unsigned int xsize;
    unsigned int xsizeSpec;
    unsigned int ysize;
    unsigned int ysizeSpec;
    unsigned int left;
    unsigned int right;
    unsigned int top;
    unsigned int bottom;
    unsigned int leftSpec;
    unsigned int rightSpec;
    unsigned int topSpec;
    unsigned int bottomSpec;
    float xalign;
    float yalign;
    unsigned int mwidth;
    unsigned int mheight;
    unsigned int white;     /* >0: pad white; 0: pad black */
    unsigned int extend_edge;
    unsigned int reportonly;
    unsigned int verbose;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry *option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;
    unsigned int blackOpt;
    unsigned int xalignSpec, yalignSpec, mwidthSpec, mheightSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "xsize",     OPT_UINT,    &cmdlineP->xsize,
            &cmdlineP->xsizeSpec, 0);
    OPTENT3(0,   "width",     OPT_UINT,    &cmdlineP->xsize,
            &cmdlineP->xsizeSpec, 0);
    OPTENT3(0,   "ysize",     OPT_UINT,    &cmdlineP->ysize,
            &cmdlineP->ysizeSpec, 0);
    OPTENT3(0,   "height",    OPT_UINT,    &cmdlineP->ysize,
            &cmdlineP->ysizeSpec, 0);
    OPTENT3(0,   "left",      OPT_UINT,    &cmdlineP->left,
            &cmdlineP->leftSpec, 0);
    OPTENT3(0,   "right",     OPT_UINT,    &cmdlineP->right,
            &cmdlineP->rightSpec, 0);
    OPTENT3(0,   "top",       OPT_UINT,    &cmdlineP->top,
            &cmdlineP->topSpec, 0);
    OPTENT3(0,   "bottom",    OPT_UINT,    &cmdlineP->bottom,
            &cmdlineP->bottomSpec, 0);
    OPTENT3(0,   "xalign",    OPT_FLOAT,   &cmdlineP->xalign,
            &xalignSpec,           0);
    OPTENT3(0,   "halign",    OPT_FLOAT,   &cmdlineP->xalign,
            &xalignSpec,           0);
    OPTENT3(0,   "yalign",    OPT_FLOAT,   &cmdlineP->yalign,
            &yalignSpec,           0);
    OPTENT3(0,   "valign",    OPT_FLOAT,   &cmdlineP->yalign,
            &yalignSpec,           0);
    OPTENT3(0,   "mwidth",    OPT_UINT,    &cmdlineP->mwidth,
            &mwidthSpec,         0);
    OPTENT3(0,   "mheight",   OPT_UINT,    &cmdlineP->mheight,
            &mheightSpec,        0);
    OPTENT3(0,   "black",     OPT_FLAG,    NULL,
            &blackOpt,           0);
    OPTENT3(0,   "white",     OPT_FLAG,    NULL,
            &cmdlineP->white,    0);
    OPTENT3(0,   "extend-edge", OPT_FLAG,  NULL,
            &cmdlineP->extend_edge,  0);
    OPTENT3(0,   "reportonly", OPT_FLAG,   NULL,
            &cmdlineP->reportonly,   0);
    OPTENT3(0,   "verbose",   OPT_FLAG,    NULL,
            &cmdlineP->verbose,  0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof opt, 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (cmdlineP->extend_edge) {
        if (blackOpt || cmdlineP->white)
            pm_error("You cannot specify -extend-edge with -black or -white");
    }

    if (blackOpt && cmdlineP->white)
        pm_error("You cannot specify both -black and -white");

    if (cmdlineP->topSpec > 1)
       pm_error("You can specify -top only once");

    if (cmdlineP->bottomSpec > 1)
       pm_error("You can specify -bottom only once");

    if (cmdlineP->leftSpec > 1)
       pm_error("You can specify -left only once");

    if (cmdlineP->rightSpec > 1)
       pm_error("You can specify -right only once");

    if (cmdlineP->xsizeSpec > 1)
       pm_error("You can specify -width only once");

    if (cmdlineP->ysizeSpec > 1)
       pm_error("You can specify -height only once");

    if (xalignSpec && (cmdlineP->leftSpec || cmdlineP->rightSpec))
        pm_error("You cannot specify both -halign and -left or -right");

    if (yalignSpec && (cmdlineP->topSpec || cmdlineP->bottomSpec))
        pm_error("You cannot specify both -valign and -top or -bottom");

    if (xalignSpec && (!cmdlineP->xsizeSpec && !mwidthSpec) )
        pm_error("-halign is meaningless without -width or -mwidth");

    if (yalignSpec && (!cmdlineP->ysizeSpec && !mheightSpec) )
        pm_error("-valign is meaningless without -height or -mheight");

    if (xalignSpec) {
        if (cmdlineP->xalign < 0)
            pm_error("You have specified a negative -halign value (%f)",
                     cmdlineP->xalign);
        if (cmdlineP->xalign > 1)
            pm_error("You have specified a -halign value (%f) greater than 1",
                     cmdlineP->xalign);
    } else
        cmdlineP->xalign = 0.5;

    if (yalignSpec) {
        if (cmdlineP->yalign < 0)
            pm_error("You have specified a negative -halign value (%f)",
                     cmdlineP->yalign);
        if (cmdlineP->yalign > 1)
            pm_error("You have specified a -valign value (%f) greater than 1",
                     cmdlineP->yalign);
    } else
        cmdlineP->yalign = 0.5;

    if (!mwidthSpec)
        cmdlineP->mwidth = 1;

    if (!mheightSpec)
        cmdlineP->mheight = 1;

    /* get optional input filename */
    if (argc-1 > 1)
        pm_error("This program takes at most 1 parameter.  You specified %d",
                 argc-1);
    else if (argc-1 == 1)
        cmdlineP->inputFileNm = argv[1];
    else
        cmdlineP->inputFileNm = "-";
}



static void
parseCommandLineOld(int argc, const char ** argv,
                    struct CmdlineInfo * const cmdlineP) {

    /* This syntax was abandoned in February 2002. */
    pm_message("Warning: old style options are deprecated!");

    cmdlineP->xsize = cmdlineP->ysize = 0;
    cmdlineP->left = cmdlineP->right = cmdlineP->top = cmdlineP->bottom = 0;
    cmdlineP->xalign = cmdlineP->yalign = 0.5;
    cmdlineP->white = cmdlineP->verbose = FALSE;

    while (argc >= 2 && argv[1][0] == '-') {
        if (strcmp(argv[1]+1,"black") == 0) cmdlineP->white = FALSE;
        else if (strcmp(argv[1]+1,"white") == 0) cmdlineP->white = TRUE;
        else switch (argv[1][1]) {
        case 'l':
            if (atoi(argv[1]+2) < 0)
                pm_error("left border too small");
            else if (atoi(argv[1]+2) > MAX_WIDTHHEIGHT)
                pm_error("left border too large");
            else
                cmdlineP->left = atoi(argv[1]+2);
            break;
        case 'r':
            if (atoi(argv[1]+2) < 0)
                pm_error("right border too small");
            else if (atoi(argv[1]+2) > MAX_WIDTHHEIGHT)
                pm_error("right border too large");
            else
                cmdlineP->right = atoi(argv[1]+2);
            break;
        case 'b':
            if (atoi(argv[1]+2) < 0)
                pm_error("bottom border too small");
            else if (atoi(argv[1]+2) > MAX_WIDTHHEIGHT)
                pm_error("bottom border too large");
            else
                cmdlineP->bottom = atoi(argv[1]+2);
            break;
        case 't':
            if (atoi(argv[1]+2) < 0)
                pm_error("top border too small");
            else if (atoi(argv[1]+2) > MAX_WIDTHHEIGHT)
                pm_error("top border too large");
            else
                cmdlineP->top = atoi(argv[1]+2);
            break;
        default:
            pm_usage("[-white|-black] [-l#] [-r#] [-t#] [-b#] [pnmfile]");
        }
        argc--, argv++;
    }

    cmdlineP->xsizeSpec = (cmdlineP->xsize > 0);
    cmdlineP->ysizeSpec = (cmdlineP->ysize > 0);

    if (argc > 2)
        pm_usage("[-white|-black] [-l#] [-r#] [-t#] [-b#] [pnmfile]");

    if (argc == 2)
        cmdlineP->inputFileNm = argv[1];
    else
        cmdlineP->inputFileNm = "-";
}



static void
validateHorizontalSize(struct CmdlineInfo const cmdline,
                       unsigned int       const cols) {
/*----------------------------------------------------------------------------
   Abort the program if the padding parameters in 'cmdline', applied to
   an image width 'cols', would result in numbers too large for us to
   compute with easily.
-----------------------------------------------------------------------------*/
    unsigned int const lpad         = cmdline.leftSpec   ? cmdline.left   : 0;
    unsigned int const rpad         = cmdline.rightSpec  ? cmdline.right  : 0;
    unsigned int const mwidthMaxPad = cmdline.mwidth - 1;

    if (cmdline.xsizeSpec && cmdline.xsize > MAX_WIDTHHEIGHT)
        pm_error("The width value you specified is too large.");

    if (lpad > MAX_WIDTHHEIGHT)
        pm_error("The left padding value you specified is too large.");

    if (rpad > MAX_WIDTHHEIGHT)
        pm_error("The right padding value you specified is too large.");

    if ((double) cols +
        (double) lpad +
        (double) rpad +
        (double) mwidthMaxPad > MAX_WIDTHHEIGHT)
        pm_error("Given padding parameters make output width too large "
                 "for this program to compute");

    if (cmdline.xsizeSpec &&
        (double) cmdline.xsize + (double) mwidthMaxPad > MAX_WIDTHHEIGHT)
        pm_error("Given padding parameters make output width too large "
                 "for this program to compute");
}



static void
computePadSizeBeforeMult(unsigned int   const unpaddedSize,
                         bool           const sizeSpec,
                         unsigned int   const sizeReq,
                         bool           const begPadSpec,
                         unsigned int   const begPadReq,
                         bool           const endPadSpec,
                         unsigned int   const endPadReq,
                         double         const align,
                         unsigned int * const begPadP,
                         unsigned int * const endPadP) {
/*----------------------------------------------------------------------------
   Compute the padding on each end that would be required if user did not
   request any "multiple" padding; i.e. he didn't say request e.g. that the
   output width be a multiple of 10 pixels.
-----------------------------------------------------------------------------*/
    if (sizeSpec) {
        if (begPadSpec && endPadSpec) {
            if (begPadReq + unpaddedSize + endPadReq < unpaddedSize) {
                pm_error("Beginning adding (%u), and end "
                         "padding (%u) are insufficient to bring the "
                         "image size of %u up to %u.",
                         begPadReq, endPadReq, unpaddedSize, sizeReq);
            } else {
                *begPadP = begPadReq;
                *endPadP = endPadReq;
            }
        } else if (begPadSpec) {
            *begPadP = begPadReq;
            *endPadP = MAX(sizeReq, unpaddedSize + begPadReq) -
                (begPadReq + unpaddedSize);
        } else if (endPadSpec) {
            *endPadP = endPadReq;
            *begPadP = MAX(sizeReq, unpaddedSize + endPadReq) -
                (unpaddedSize + endPadReq);
        } else {
            if (sizeReq > unpaddedSize) {
                assert(align <= 1.0 && align >= 0.0);
                *begPadP = ROUNDU((sizeReq - unpaddedSize) * align);
                *endPadP = sizeReq - unpaddedSize - *begPadP;
                assert(*begPadP + unpaddedSize + *endPadP == sizeReq);
            } else {
                *begPadP = 0;
                *endPadP = 0;
            }
        }
    } else {
        *begPadP = begPadSpec ? begPadReq : 0;
        *endPadP = endPadSpec ? endPadReq : 0;
    }
}



static void
computePadSizesOneDim(unsigned int   const unpaddedSize,
                      bool           const sizeSpec,
                      unsigned int   const sizeReq,
                      bool           const begPadSpec,
                      unsigned int   const begPadReq,
                      bool           const endPadSpec,
                      unsigned int   const endPadReq,
                      double         const align,
                      unsigned int   const multiple,
                      unsigned int * const begPadP,
                      unsigned int * const endPadP) {
/*----------------------------------------------------------------------------
   Compute the number of pixels of padding needed before and after a row or
   column ("before" means on the left side of a row or the top side of a
   column).  Return them as *padBegP and *padEndP, respectively.

   'unpaddedSize' is the size (width/height) of the row or column before
   any padding.

   The rest of the inputs are the padding parameters, equivalent to the
   program's corresponding command line options.
-----------------------------------------------------------------------------*/
    unsigned int begPadBeforeMult, endPadBeforeMult;
        /* The padding we would apply if user did not request multiple
           padding (such as "make the output a multiple of 10 pixels")
        */

    computePadSizeBeforeMult(unpaddedSize, sizeSpec, sizeReq,
                             begPadSpec, begPadReq, endPadSpec, endPadReq,
                             align,
                             &begPadBeforeMult, &endPadBeforeMult);

    {
        unsigned int const sizeBeforeMpad =
            unpaddedSize + begPadBeforeMult + endPadBeforeMult;
        unsigned int const paddedSize =
            ROUNDUP(sizeBeforeMpad, multiple);
        unsigned int const morePadNeeded = paddedSize - sizeBeforeMpad;
        unsigned int const totalPadBeforeMult =
            begPadBeforeMult + endPadBeforeMult;
        double const begFrac =
            totalPadBeforeMult > 0 ?
            (double)begPadBeforeMult / totalPadBeforeMult :
            align;
        unsigned int const addlMsizeBegPad = ROUNDU(morePadNeeded * begFrac);
            /* # of pixels we have to add to the beginning to satisfy
               user's desire for the final size to be a multiple of something
            */
        unsigned int const addlMsizeEndPad = morePadNeeded - addlMsizeBegPad;
            /* Analogous to 'addlMsizeBegPad' */

        *begPadP = begPadBeforeMult + addlMsizeBegPad;
        *endPadP = endPadBeforeMult + addlMsizeEndPad;
    }
}



static void
computeHorizontalPadSizes(struct CmdlineInfo const cmdline,
                          unsigned int       const cols,
                          unsigned int *     const lpadP,
                          unsigned int *     const rpadP) {

    validateHorizontalSize(cmdline, cols);

    computePadSizesOneDim(cols,
                          cmdline.xsizeSpec > 0, cmdline.xsize,
                          cmdline.leftSpec > 0, cmdline.left,
                          cmdline.rightSpec > 0, cmdline.right,
                          cmdline.xalign,
                          cmdline.mwidth,
                          lpadP, rpadP);
}



static void
validateVerticalSize(struct CmdlineInfo const cmdline,
                     unsigned int       const rows) {
/*----------------------------------------------------------------------------
   Abort the program if the padding parameters in 'cmdline', applied to
   an image width 'cols', would result in numbers too large for us to
   compute with easily.
-----------------------------------------------------------------------------*/
    unsigned int const tpad          =
        cmdline.topSpec    ?  cmdline.top     : 0;
    unsigned int const bpad          =
        cmdline.bottomSpec ?  cmdline.bottom  : 0;
    unsigned int const mheightMaxPad = cmdline.mheight - 1;

    if (cmdline.ysizeSpec && cmdline.ysize > MAX_WIDTHHEIGHT)
        pm_error("The width value you specified is too large.");

    if (tpad > MAX_WIDTHHEIGHT)
        pm_error("The top padding value you specified is too large.");

    if (bpad > MAX_WIDTHHEIGHT)
        pm_error("The bottom padding value you specified is too large.");

    if ((double) rows +
        (double) tpad +
        (double) bpad +
        (double) mheightMaxPad > MAX_WIDTHHEIGHT)
        pm_error("Given padding parameters make output height too large "
            "for this program to compute");

    if (cmdline.ysizeSpec &&
        (double) cmdline.ysize && (double) mheightMaxPad > MAX_WIDTHHEIGHT)
        pm_error("Given padding parameters make output height too large "
            "for this program to compute");
}



static void
computeVerticalPadSizes(struct CmdlineInfo const cmdline,
                        int                const rows,
                        unsigned int *     const tpadP,
                        unsigned int *     const bpadP) {

    validateVerticalSize(cmdline, rows);

    computePadSizesOneDim(rows,
                          cmdline.ysizeSpec > 0, cmdline.ysize,
                          cmdline.topSpec > 0, cmdline.top,
                          cmdline.bottomSpec > 0, cmdline.bottom,
                          cmdline.yalign,
                          cmdline.mheight,
                          tpadP, bpadP);
}



static void
computePadSizes(struct CmdlineInfo const cmdline,
                int                const cols,
                int                const rows,
                unsigned int *     const lpadP,
                unsigned int *     const rpadP,
                unsigned int *     const tpadP,
                unsigned int *     const bpadP) {

    computeHorizontalPadSizes(cmdline, cols, lpadP, rpadP);

    computeVerticalPadSizes(cmdline, rows, tpadP, bpadP);

    if (cmdline.verbose)
        pm_message("Padding: left: %u; right: %u; top: %u; bottom: %u",
                   *lpadP, *rpadP, *tpadP, *bpadP);
}



static void
reportPadSizes(int          const inCols,
               int          const inRows,
               unsigned int const lpad,
               unsigned int const rpad,
               unsigned int const tpad,
               unsigned int const bpad) {

    unsigned int const outCols = inCols + lpad + rpad;
    unsigned int const outRows = inRows + tpad + bpad;

    printf("%u %u %u %u %u %u\n", lpad, rpad, tpad, bpad, outCols, outRows);

}



static unsigned char
bitPeek(const unsigned char * const bitrow,
        unsigned int          const position) {

    bool retval;

    unsigned int const charPosition = position / 8;
    unsigned int const bitPosition  = position % 8;

    retval =  (bitrow[charPosition] >> (7 - bitPosition)) & 0x01;

    return retval;
}



static void
extendLeftPbm(unsigned char * const bitrow,
              unsigned int    const lpad) {

    unsigned int const padCharCt  = lpad / 8;
    unsigned int const fractBitCt = lpad % 8;
    unsigned int const color      = bitPeek(bitrow, lpad);

    unsigned int colChar;

    for (colChar = 0; colChar < padCharCt; ++colChar)
        bitrow[colChar] = 0xff * color;

    if (fractBitCt > 0) {
        bitrow[colChar] = (unsigned char)
            (0xff * color) << (8-fractBitCt) |
            (bitrow[colChar] & (0xff >> fractBitCt));
    }
}



static void
extendRightPbm(unsigned char * const bitrow,
               unsigned int    const lcols,
               unsigned int    const rpad) {

    unsigned int const  rpad0 = (8 - lcols % 8) % 8;
    unsigned int const  rpad1 = rpad - rpad0;
    unsigned int const  rpad1CharCt =
        rpad1 > 0 ? pbm_packed_bytes(rpad1) : 0;
    unsigned int const  lastColChar = lcols / 8 - (rpad0 == 0 ? 1 : 0);
    unsigned char const fillColor = bitPeek(bitrow, lcols - 1);

    unsigned int colChar;

    if (rpad0 > 0) {
        if (fillColor == 0x1) {
            bitrow[lastColChar] =
                bitrow[lastColChar] | (bitrow[lastColChar] - 1);
        } else {
            pbm_cleanrowend_packed(bitrow, lcols);
            /* Function pbm_cleanrowend_packed() is employed an atypical way
               here.  It cleans bits beyond the active image content, but the
               position is not the row end.
            */
        }
    }

    for (colChar = 0; colChar < rpad1CharCt; ++colChar)
        bitrow[lastColChar + colChar +1] = 0xff * fillColor;

    if (fillColor == 0x1)
        pbm_cleanrowend_packed(bitrow, lcols + rpad);
}



static void
extendEdgePbm(FILE *       const ifP,
              unsigned int const cols,
              unsigned int const rows,
              int          const format,
              unsigned int const newcols,
              unsigned int const lpad,
              unsigned int const rpad,
              unsigned int const tpad,
              unsigned int const bpad,
              FILE *       const ofP) {
/*----------------------------------------------------------------------------
  Fast extend-edge routine for PBM
-----------------------------------------------------------------------------*/
    unsigned char * const newbitrow = pbm_allocrow_packed(newcols);

    unsigned int row;

    pbm_writepbminit(stdout, newcols, rows + tpad + bpad, 0);

    /* Write top row tpad + 1 times */
    if (rpad > 0)
        newbitrow[(cols + lpad) / 8] = 0x00;

    pbm_readpbmrow_bitoffset(ifP, newbitrow, cols, format, lpad);

    if (lpad > 0)
        extendLeftPbm(newbitrow, lpad);
    if (rpad > 0)
        extendRightPbm(newbitrow, lpad + cols, rpad);

    pbm_cleanrowend_packed(newbitrow, newcols);

    for (row = 0; row < tpad + 1; ++row)
        pbm_writepbmrow_packed(ofP, newbitrow, newcols, 0);

    /* Read rows, shift and write with left and right margins added */
    for (row = 1;  row < rows; ++row) {
        pbm_readpbmrow_bitoffset(ifP, newbitrow, cols, format, lpad);

        if (lpad > 0 &&
            (bitPeek(newbitrow, lpad - 1) != bitPeek(newbitrow, lpad)))
            extendLeftPbm(newbitrow, lpad);
        if (rpad > 0 &&
            (bitPeek(newbitrow, lpad + cols -1) !=
             bitPeek(newbitrow, lpad + cols)))
            extendRightPbm(newbitrow, lpad + cols, rpad);
        pbm_writepbmrow_packed(ofP, newbitrow, newcols, 0);
    }

    /* Write bottom margin */
    for (row = 0; row < bpad; ++row)
        pbm_writepbmrow_packed(ofP, newbitrow, newcols, 0);

    pnm_freerow(newbitrow);
}



static void
extendEdgeGeneral(FILE *       const ifP,
                  unsigned int const cols,
                  unsigned int const rows,
                  xelval       const maxval,
                  int          const format,
                  unsigned int const newcols,
                  unsigned int const lpad,
                  unsigned int const rpad,
                  unsigned int const tpad,
                  unsigned int const bpad,
                  FILE *       const ofP) {
/*----------------------------------------------------------------------------
  General extend-edge routine (logic works for PBM)
-----------------------------------------------------------------------------*/
    xel * const xelrow = pnm_allocrow(newcols);

    unsigned int row, col;

    pnm_writepnminit(ofP, newcols, rows + tpad + bpad, maxval, format, 0);

    pnm_readpnmrow(ifP, &xelrow[lpad], cols, maxval, format);

    for (col = 0; col < lpad; ++col)
        xelrow[col] = xelrow[lpad];
    for (col = lpad + cols; col < newcols; ++col)
        xelrow[col] = xelrow[lpad + cols - 1];

    for (row = 0; row < tpad + 1; ++row)
        pnm_writepnmrow(ofP, xelrow, newcols, maxval, format, 0);

    for (row = 1; row < rows; ++row) {
        unsigned int col;

        pnm_readpnmrow(ifP, &xelrow[lpad], cols, maxval, format);

        for (col = 0; col < lpad; ++col)
            xelrow[col] = xelrow[lpad];
        for (col = lpad + cols; col < newcols; ++col)
            xelrow[col] = xelrow[lpad + cols - 1];

        pnm_writepnmrow(ofP, xelrow, newcols, maxval, format, 0);
    }

    for (row = 0; row < bpad; ++row)
        pnm_writepnmrow(ofP, xelrow, newcols, maxval, format, 0);

    pnm_freerow(xelrow);
}



static void
padPbm(FILE *       const ifP,
       unsigned int const cols,
       unsigned int const rows,
       int          const format,
       unsigned int const newcols,
       unsigned int const lpad,
       unsigned int const rpad,
       unsigned int const tpad,
       unsigned int const bpad,
       bool         const colorWhite,
       FILE *       const ofP) {
/*----------------------------------------------------------------------------
  Fast padding routine for PBM
-----------------------------------------------------------------------------*/
    unsigned char * const bgrow  = pbm_allocrow_packed(newcols);
    unsigned char * const newrow = pbm_allocrow_packed(newcols);

    unsigned char const padChar =
        0xff * (colorWhite ? PBM_WHITE : PBM_BLACK);

    unsigned int const newColChars = pbm_packed_bytes(newcols);

    unsigned int row;
    unsigned int charCnt;

    /* Set up margin row, input-output buffer */
    for (charCnt = 0; charCnt < newColChars; ++charCnt)
        bgrow[charCnt] = newrow[charCnt] = padChar;

    if (newcols % 8 > 0) {
        bgrow[newColChars-1]  <<= 8 - newcols % 8;
        newrow[newColChars-1] <<= 8 - newcols % 8;
    }

    pbm_writepbminit(ofP, newcols, rows + tpad + bpad, 0);

    /* Write top margin */
    for (row = 0; row < tpad; ++row)
        pbm_writepbmrow_packed(ofP, bgrow, newcols, 0);

    /* Read rows, shift and write with left and right margins added */
    for (row = 0; row < rows; ++row) {
        pbm_readpbmrow_bitoffset(ifP, newrow, cols, format, lpad);
        pbm_writepbmrow_packed(ofP, newrow, newcols, 0);
    }

    pnm_freerow(newrow);

    /* Write bottom margin */
    for (row = 0; row < bpad; ++row)
        pbm_writepbmrow_packed(ofP, bgrow, newcols, 0);

    pnm_freerow(bgrow);
}



static void
padGeneral(FILE *       const ifP,
           unsigned int const cols,
           unsigned int const rows,
           xelval       const maxval,
           int          const format,
           unsigned int const newcols,
           unsigned int const lpad,
           unsigned int const rpad,
           unsigned int const tpad,
           unsigned int const bpad,
           bool         const colorWhite,
           FILE *       const ofP) {
/*----------------------------------------------------------------------------
  General padding routine (logic works for PBM)
-----------------------------------------------------------------------------*/
    xel * const bgrow      = pnm_allocrow(newcols);
    xel const   background = colorWhite ?
        pnm_whitexel(maxval, format) : pnm_blackxel(maxval, format);

    unsigned int row, col;

    for (col = 0; col < newcols; ++col)
        bgrow[col] = background;

    pnm_writepnminit(ofP, newcols, rows + tpad + bpad, maxval, format, 0);

    /* Write top padding */

    for (row = 0; row < tpad; ++row)
        pnm_writepnmrow(ofP, bgrow, newcols, maxval, format, 0);

    /* Write body of image */
    {
        xel * const xelrow = pnm_allocrow(newcols);

        /* initial value for 'xelrow': all background */
        for (col = 0; col < newcols; ++col)
            xelrow[col] = bgrow[col] = background;

        for (row = 0; row < rows; ++row) {
            pnm_readpnmrow(ifP, &xelrow[lpad], cols, maxval, format);
            pnm_writepnmrow(ofP, xelrow, newcols, maxval, format, 0);
        }
        pnm_freerow(xelrow);
    }

    /* Write bottom padding */
    for (row = 0; row < bpad; ++row)
        pnm_writepnmrow(ofP, bgrow, newcols, maxval, format, 0);

    pnm_freerow(bgrow);
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;
    FILE * ifP;

    xelval maxval;
    int rows, cols, newcols, format;
    bool depr_cmd; /* use deprecated commandline interface */
    unsigned int lpad, rpad, tpad, bpad;

    pm_proginit(&argc, argv);

    /* detect deprecated options */
    depr_cmd = FALSE;  /* initial assumption */
    if (argc > 1 && argv[1][0] == '-') {
        if (argv[1][1] == 't' || argv[1][1] == 'b'
            || argv[1][1] == 'l' || argv[1][1] == 'r') {
            if (argv[1][2] >= '0' && argv[1][2] <= '9')
                depr_cmd = TRUE;
        }
    }
    if (argc > 2 && argv[2][0] == '-') {
        if (argv[2][1] == 't' || argv[2][1] == 'b'
            || argv[2][1] == 'l' || argv[2][1] == 'r') {
            if (argv[2][2] >= '0' && argv[2][2] <= '9')
                depr_cmd = TRUE;
        }
    }

    if (depr_cmd)
        parseCommandLineOld(argc, argv, &cmdline);
    else
        parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileNm);

    pnm_readpnminit(ifP, &cols, &rows, &maxval, &format);

    if (cmdline.verbose) pm_message("image WxH = %dx%d", cols, rows);

    computePadSizes(cmdline, cols, rows, &lpad, &rpad, &tpad, &bpad);

    newcols = cols + lpad + rpad;

    if (cmdline.reportonly)
        reportPadSizes(cols, rows, lpad, rpad, tpad, bpad);
    else if (cmdline.extend_edge) {
        if (PNM_FORMAT_TYPE(format) == PBM_TYPE)
            extendEdgePbm(ifP, cols, rows, format,
                          newcols, lpad, rpad, tpad, bpad, stdout);
        else
            extendEdgeGeneral(ifP, cols, rows, maxval, format,
                              newcols, lpad, rpad, tpad, bpad, stdout);
    } else {
        if (PNM_FORMAT_TYPE(format) == PBM_TYPE)
            padPbm(ifP, cols, rows, format,
                   newcols, lpad, rpad, tpad, bpad, !!cmdline.white, stdout);
        else
            padGeneral(ifP, cols, rows, maxval, format,
                       newcols, lpad, rpad, tpad, bpad, !!cmdline.white,
                       stdout);
    }

    pm_close(ifP);

    return 0;
}



