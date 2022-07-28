/*=============================================================================
                                   pamcat
===============================================================================

  Concatenate images.

  By Bryan Henderson and Akira Urushibata.  Contributed to the public domain
  by its authors.

=============================================================================*/

#include <assert.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "bitarith.h"
#include "nstring.h"
#include "pam.h"
#include "pbm.h"

#define LEFTBITS pm_byteLeftBits
#define RIGHTBITS pm_byteRightBits

enum PadColorMethod {PAD_BLACK, PAD_WHITE, PAD_AUTO};
  /* The method of determining the color of padding when images are not the
     same height or width.  Always white (maxval samples) always black (zero
     samples) or determined from what looks like background for the image in
     question.
  */


enum PlanePadMethod {PLANEPAD_ZERO, PLANEPAD_EXTEND};
  /* The method for adding additional planes when some images have fewer
     planes than others.  The additional plane is either all zeroes or
     equal to the highest plane in the original image.
  */

enum Orientation {TOPBOTTOM, LEFTRIGHT};
  /* Direction of concatenation */

enum Justification {JUST_CENTER, JUST_MIN, JUST_MAX};
  /* Justification of images in concatenation */

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char **       inputFileName;
    unsigned int        fileCt;
    enum PadColorMethod padColorMethod;
    enum PlanePadMethod planePadMethod;
    enum Orientation    orientation;
    enum Justification  justification;
    unsigned int        verbose;
};



static void
parseCommandLine(int argc, const char ** const argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3() on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int leftright, topbottom;
    unsigned int black, white, extendplane;
    unsigned int jtop, jbottom, jleft, jright, jcenter;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "leftright",   OPT_FLAG,   NULL, &leftright,         0);
    OPTENT3(0, "lr",          OPT_FLAG,   NULL, &leftright,         0);
    OPTENT3(0, "topbottom",   OPT_FLAG,   NULL, &topbottom,         0);
    OPTENT3(0, "tb",          OPT_FLAG,   NULL, &topbottom,         0);
    OPTENT3(0, "black",       OPT_FLAG,   NULL, &black,             0);
    OPTENT3(0, "white",       OPT_FLAG,   NULL, &white,             0);
    OPTENT3(0, "jtop",        OPT_FLAG,   NULL, &jtop,              0);
    OPTENT3(0, "jbottom",     OPT_FLAG,   NULL, &jbottom,           0);
    OPTENT3(0, "jleft",       OPT_FLAG,   NULL, &jleft,             0);
    OPTENT3(0, "jright",      OPT_FLAG,   NULL, &jright,            0);
    OPTENT3(0, "jcenter",     OPT_FLAG,   NULL, &jcenter,           0);
    OPTENT3(0, "extendplane", OPT_FLAG,   NULL, &extendplane,       0);
    OPTENT3(0, "verbose",     OPT_FLAG,   NULL, &cmdlineP->verbose, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    free(option_def);

    if (leftright + topbottom > 1)
        pm_error("You may specify only one of -topbottom (-tb) and "
                 "-leftright (-lr)");
    else if (leftright)
        cmdlineP->orientation = LEFTRIGHT;
    else if (topbottom)
        cmdlineP->orientation = TOPBOTTOM;
    else
        pm_error("You must specify either -leftright or -topbottom");

    if (black + white > 1)
        pm_error("You may specify only one of -black and -white");
    else if (black)
        cmdlineP->padColorMethod = PAD_BLACK;
    else if (white)
        cmdlineP->padColorMethod = PAD_WHITE;
    else
        cmdlineP->padColorMethod = PAD_AUTO;

    cmdlineP->planePadMethod = extendplane ? PLANEPAD_EXTEND : PLANEPAD_ZERO;

    if (jtop + jbottom + jleft + jright + jcenter > 1)
        pm_error("You may specify only one of -jtop, -jbottom, "
                 "-jleft, and -jright");
    else {
        switch (cmdlineP->orientation) {
        case LEFTRIGHT:
            if (jleft)
                pm_error("-jleft is invalid with -leftright");
            if (jright)
                pm_error("-jright is invalid with -leftright");
            if (jtop)
                cmdlineP->justification = JUST_MIN;
            else if (jbottom)
                cmdlineP->justification = JUST_MAX;
            else if (jcenter)
                cmdlineP->justification = JUST_CENTER;
            else
                cmdlineP->justification = JUST_CENTER;
            break;
        case TOPBOTTOM:
            if (jtop)
                pm_error("-jtop is invalid with -topbottom");
            if (jbottom)
                pm_error("-jbottom is invalid with -topbottom");
            if (jleft)
                cmdlineP->justification = JUST_MIN;
            else if (jright)
                cmdlineP->justification = JUST_MAX;
            else if (jcenter)
                cmdlineP->justification = JUST_CENTER;
            else
                cmdlineP->justification = JUST_CENTER;
            break;
        }
    }

    if (argc-1 < 1) {
        MALLOCARRAY_NOFAIL(cmdlineP->inputFileName, 1);
        cmdlineP->inputFileName[0] = "-";
        cmdlineP->fileCt = 1;
    } else {
        unsigned int i;
        unsigned int stdinCt;
            /* Number of input files user specified as Standard Input */

        MALLOCARRAY_NOFAIL(cmdlineP->inputFileName, argc-1);

        for (i = 0, stdinCt = 0; i < argc-1; ++i) {
            cmdlineP->inputFileName[i] = argv[1+i];
            if (streq(argv[1+i], "-"))
                ++stdinCt;
        }
        cmdlineP->fileCt = argc-1;
        if (stdinCt > 1)
            pm_error("At most one input image can come from Standard Input.  "
                     "You specified %u", stdinCt);
    }
}



static void
computeOutputParms(unsigned int       const fileCt,
                   enum Orientation   const orientation,
                   const struct pam * const inpam,  /* array */
                   bool               const verbose,
                   struct pam *       const outpamP) {

    double newCols, newRows, newDepth;
    const char * newTupletype;
    sample newMaxval;
    bool allPbm;
        /* All the input images are raw PBM, so far as we've seen */
    bool tupleTypeVaries;
        /* We've seen multiple tuple types among the input images */
    unsigned int i;

    for (i = 0, newCols = 0, newRows = 0, newMaxval = 0, allPbm = true,
             newTupletype = NULL, tupleTypeVaries = false;
         i < fileCt;
         ++i) {

        const struct pam * const inpamP = &inpam[i];

        if (inpamP->format != RPBM_FORMAT)
            allPbm = false;

        newMaxval = MAX(newMaxval, inpamP->maxval);
        newDepth  = MAX(newDepth,  inpamP->depth);

        switch (orientation) {
        case LEFTRIGHT:
            newCols += inpamP->width;
            newRows = MAX(newRows, inpamP->height);
            break;
        case TOPBOTTOM:
            newRows += inpamP->height;
            newCols = MAX(newCols, inpamP->width);
            break;
        }
        if (newTupletype) {
            if (!streq(inpamP->tuple_type, newTupletype))
                tupleTypeVaries = true;
        } else
            newTupletype = inpamP->tuple_type;
    }
    assert(newCols   > 0);
    assert(newRows   > 0);
    assert(newMaxval > 0);

    if (newCols > INT_MAX)
       pm_error("Output width too large: %.0f.", newCols);
    if (newRows > INT_MAX)
       pm_error("Output height too large: %.0f.", newRows);

    outpamP->size = sizeof(*outpamP);
    outpamP->len  = PAM_STRUCT_SIZE(raster_pos);

    /* Note that while 'double' is not in general a precise numerical type,
       in the case of a sum of integers which is less than INT_MAX, it
       is exact, because double's precision is greater than int's.
    */
    outpamP->height           = (unsigned int)newRows;
    outpamP->width            = (unsigned int)newCols;
    outpamP->depth            = newDepth;
    outpamP->allocation_depth = newDepth;
    outpamP->maxval           = newMaxval;
    outpamP->format           = allPbm ? RPBM_FORMAT : PAM_FORMAT;
    STRSCPY(outpamP->tuple_type, tupleTypeVaries ? "" : newTupletype);
    outpamP->comment_p        = NULL;
    outpamP->plainformat      = false;

    if (verbose) {
        pm_message("Concatenating %u input images", fileCt);
        pm_message("Output width, height, depth: %u x %u x %u",
                   outpamP->width, outpamP->height, outpamP->depth);
        if (outpamP->format == RPBM_FORMAT)
            pm_message("Using PBM fast path and producing raw PBM output");
        else {
            pm_message("Output maxval (max of all inputs): %lu",
                       outpamP->maxval);
            if (strlen(outpamP->tuple_type) > 0)
                pm_message("Output tuple type (same as all inputs): '%s'",
                           outpamP->tuple_type);
            else
                pm_message("Output tuple type is null string because input "
                           "images have various tuple types");
        }
    }
}



static void
copyBitrow(const unsigned char * const source,
           unsigned char *       const destBitrow,
           unsigned int          const cols,
           unsigned int          const offset) {
/*----------------------------------------------------------------------------
  Copy from source to destBitrow, without shifting.  Preserve
  surrounding image data.
-----------------------------------------------------------------------------*/
    unsigned char * const dest = & destBitrow[ offset/8 ];
        /* Copy destination, with leading full bytes ignored. */
    unsigned int const rs = offset % 8;
        /* The "little offset", as measured from start of dest.  Source
           is already shifted by this value.
        */
    unsigned int const trs = (cols + rs) % 8;
        /* The number of partial bits in the final char. */
    unsigned int const colByteCnt = pbm_packed_bytes(cols + rs);
        /* # bytes to process, including partial ones on both ends. */
    unsigned int const last = colByteCnt - 1;

    unsigned char const origHead = dest[0];
    unsigned char const origEnd  = dest[last];

    unsigned int i;

    assert(colByteCnt >= 1);

    for (i = 0; i < colByteCnt; ++i)
        dest[i] = source[i];

    if (rs > 0)
        dest[0] = LEFTBITS(origHead, rs) | RIGHTBITS(dest[0], 8-rs);

    if (trs > 0)
        dest[last] = LEFTBITS(dest[last], trs) | RIGHTBITS(origEnd, 8-trs);
}



static void
padFillBitrow(unsigned char * const destBitrow,
              unsigned char   const padColor,
              unsigned int    const cols,
              unsigned int    const offset) {
/*----------------------------------------------------------------------------
   Fill destBitrow, starting at offset, with padColor.  padColor is a
   byte -- 0x00 or 0xff -- not a single bit.
-----------------------------------------------------------------------------*/
    unsigned char * const dest = &destBitrow[offset/8];
    unsigned int const rs = offset % 8;
    unsigned int const trs = (cols + rs) % 8;
    unsigned int const colByteCnt = pbm_packed_bytes(cols + rs);
    unsigned int const last = colByteCnt - 1;

    unsigned char const origHead = dest[0];
    unsigned char const origEnd  = dest[last];

    unsigned int i;

    assert(colByteCnt > 0);

    for (i = 0; i < colByteCnt; ++i)
        dest[i] = padColor;

    if (rs > 0)
        dest[0] = LEFTBITS(origHead, rs) | RIGHTBITS(dest[0], 8-rs);

    if (trs > 0)
        dest[last] = LEFTBITS(dest[last], trs) | RIGHTBITS(origEnd, 8-trs);
}



/* concatenateLeftRightPBM() and concatenateLeftRightGen()
   employ almost identical algorithms.
   The difference is in the data types and functions.

   Same for concatenateTopBottomPBM() and concatenateTopBottomGen().
*/


typedef struct {
    /* Information about one image */
    unsigned char * proberow;
        /* Top row of image, when background color is
           auto-determined.
        */
    unsigned int offset;
        /* start position of image, in bits, counting from left
           edge
        */
    unsigned char background;
        /* Background color.  0x00 means white; 0xff means black */
    unsigned int padtop;
        /* Top padding amount */
} ImgInfoPbm;



static void
getPbmImageInfo(const struct pam *  const inpam,  /* array */
                unsigned int        const fileCt,
                unsigned int        const newRows,
                enum Justification  const justification,
                enum PadColorMethod const padColorMethod,
                ImgInfoPbm **       const imgInfoP) {
/*----------------------------------------------------------------------------
   Read the first row of each image in inpam[] and return that and additional
   information about images as *imgInfoP.
-----------------------------------------------------------------------------*/
    ImgInfoPbm * imgInfo;  /* array, size 'fileCt' */
    unsigned int i;

    MALLOCARRAY_NOFAIL(imgInfo, fileCt);

    for (i = 0; i < fileCt; ++i) {
        switch (justification) {
        case JUST_MIN:
            imgInfo[i].padtop = 0;
            break;
        case JUST_MAX:
            imgInfo[i].padtop = newRows - inpam[i].height;
            break;
        case JUST_CENTER:
            imgInfo[i].padtop = (newRows - inpam[i].width) / 2;
            break;
        }

        imgInfo[i].offset =
            (i == 0) ? 0 : imgInfo[i-1].offset + inpam[i-1].width;

        if (inpam[i].height == newRows)  /* no padding */
            imgInfo[i].proberow = NULL;
        else {                   /* determine pad color for image i */
            switch (padColorMethod) {
            case PAD_AUTO: {
                bit bgBit;
                imgInfo[i].proberow =
                    pbm_allocrow_packed((unsigned int)inpam[i].width + 7);
                pbm_readpbmrow_bitoffset(
                    inpam[i].file, imgInfo[i].proberow,
                    inpam[i].width, inpam[i].format, imgInfo[i].offset % 8);

                bgBit = pbm_backgroundbitrow(
                    imgInfo[i].proberow, inpam[i].width,
                    imgInfo[i].offset % 8);

                imgInfo[i].background = bgBit == PBM_BLACK ? 0xff : 0x00;
            } break;
            case PAD_BLACK:
                imgInfo[i].proberow   = NULL;
                imgInfo[i].background = 0xff;
                break;
            case PAD_WHITE:
                imgInfo[i].proberow   = NULL;
                imgInfo[i].background = 0x00;
                break;
            }
        }
    }
    *imgInfoP = imgInfo;
}



static void
destroyPbmImgInfo(ImgInfoPbm *  const imgInfo,
                  unsigned int  const fileCt) {

    unsigned int i;

    for (i = 0; i < fileCt; ++i) {
        if (imgInfo[i].proberow)
            free(imgInfo[i].proberow);
    }
    free(imgInfo);
}



static void
concatenateLeftRightPbm(struct pam *        const outpamP,
                        const struct pam *  const inpam,  /* array */
                        unsigned int        const fileCt,
                        enum Justification  const justification,
                        enum PadColorMethod const padColorMethod) {

    unsigned char * const outrow = pbm_allocrow_packed(outpamP->width);
        /* We use just one outrow.  All padding and image data (with the
           exception of following imgInfo.proberow) goes directly into this
           packed PBM row.
        */

    ImgInfoPbm * imgInfo;
        /* malloc'ed array, one element per image.  Shadows inpam[] */
    unsigned int row;

    getPbmImageInfo(inpam, fileCt, outpamP->height,
                    justification, padColorMethod,
                    &imgInfo);

    outrow[pbm_packed_bytes(outpamP->width)-1] = 0x00;

    for (row = 0; row < outpamP->width; ++row) {
        unsigned int i;

        for (i = 0; i < fileCt; ++i) {

            if ((row == 0 && imgInfo[i].padtop > 0) ||
                row == imgInfo[i].padtop + inpam[i].height) {

                /* This row begins a run of padding, either above or below
                   file 'i', so set 'outrow' to padding.
                */
                padFillBitrow(outrow, imgInfo[i].background, inpam[i].width,
                              imgInfo[i].offset);
            }

            if (row == imgInfo[i].padtop && imgInfo[i].proberow != NULL) {
                /* Top row has been read to proberow[] to determine
                   background.  Copy it to outrow[].
                */
                copyBitrow(imgInfo[i].proberow, outrow,
                           inpam[i].width, imgInfo[i].offset);
            } else if (row >= imgInfo[i].padtop &&
                       row < imgInfo[i].padtop + inpam[i].height) {
                pbm_readpbmrow_bitoffset(
                    inpam[i].file, outrow, inpam[i].width, inpam[i].format,
                    imgInfo[i].offset);
            } else {
                /* It's a row of padding, so outrow[] is already set
                   appropriately.
                */
            }
        }
        pbm_writepbmrow_packed(outpamP->file, outrow, outpamP->width, 0);
    }

    destroyPbmImgInfo(imgInfo, fileCt);

    pbm_freerow_packed(outrow);
}



static void
concatenateTopBottomPbm(const struct pam *  const outpamP,
                        const struct pam *  const inpam,  /* array */
                        unsigned int        const fileCt,
                        enum Justification  const justification,
                        enum PadColorMethod const padColorMethod) {

    unsigned char * const outrow = pbm_allocrow_packed(outpamP->width);
        /* Like the left-right PBM case, all padding and image data
           goes directly into outrow.  There is no proberow.
        */
    unsigned char background, backgroundPrev;
        /* 0x00 means white; 0xff means black */
    unsigned int  padleft;
    bool          backChange;
        /* Background color is different from that of the previous
           input image.
        */

    unsigned int i;
    unsigned int row, startRow;

    outrow[pbm_packed_bytes(outpamP->width)-1] = 0x00;

    switch (padColorMethod){
    case PAD_AUTO:   /* do nothing */    break;
    case PAD_BLACK:  background = 0xff;  break;
    case PAD_WHITE:  background = 0x00;  break;
    }

    for (i = 0; i < fileCt; ++i) {
        if (inpam[i].width == outpamP->width) {
            /* No padding */
            startRow   = 0;
            backChange = FALSE;
            padleft    = 0;
            outrow[pbm_packed_bytes(outpamP->width)-1] = 0x00;
        } else {
            /* Determine amount of padding and color */
            switch (justification) {
            case JUST_MIN:
                padleft = 0;
                break;
            case JUST_MAX:
                padleft = outpamP->width - inpam[i].width;
                break;
            case JUST_CENTER:
                padleft = (outpamP->width - inpam[i].width) / 2;
                break;
            }

            switch (padColorMethod) {
            case PAD_AUTO: {
                bit bgBit;

                startRow = 1;

                pbm_readpbmrow_bitoffset(
                    inpam[i].file, outrow, inpam[i].width, inpam[i].format,
                    padleft);

                bgBit = pbm_backgroundbitrow(outrow, inpam[i].width, padleft);
                background = bgBit == PBM_BLACK ? 0xff : 0x00;

                backChange = (i == 0 || background != backgroundPrev);
            } break;
            case PAD_WHITE:
            case PAD_BLACK:
                startRow = 0;
                backChange = (i == 0);
                break;
            }

            if (backChange || (i > 0 && inpam[i-1].width > inpam[i].width)) {
                unsigned int const padright =
                    outpamP->width - padleft - inpam[i].width;

                if (padleft > 0)
                    padFillBitrow(outrow, background, padleft, 0);

                if (padright > 0)
                    padFillBitrow(outrow, background, padright,
                                  padleft + inpam[i].width);

            }
        }

        if (startRow == 1)
            /* Top row already read for auto background color
               determination.  Write it out.
            */
            pbm_writepbmrow_packed(outpamP->file, outrow, outpamP->width, 0);

        for (row = startRow; row < inpam[i].height; ++row) {
            pbm_readpbmrow_bitoffset(inpam[i].file, outrow, inpam[i].width,
                                     inpam[i].format, padleft);
            pbm_writepbmrow_packed(outpamP->file, outrow, outpamP->width, 0);
        }

        backgroundPrev = background;
    }
    pbm_freerow_packed(outrow);
}



static void
padPlanesRow(enum PlanePadMethod const planePadMethod,
             const struct pam *  const inpamP,
             tuple *             const outrow,
             const struct pam *  const outpamP) {
/*----------------------------------------------------------------------------
   Add additional planes to *outrow as needed to pad out to the depth
   indicated in *outpamP from the depth indicated in *inpamP.

   'planePadMethod' tells what to use for the new planes.
-----------------------------------------------------------------------------*/
    unsigned int plane;

    for (plane = inpamP->depth; plane < outpamP->depth; ++plane) {
        unsigned int col;

        for (col = 0; col < inpamP->width; ++col) {
            switch (planePadMethod) {
            case PLANEPAD_ZERO:
                outrow[col][plane] = 0;
                break;
            case PLANEPAD_EXTEND:
                assert(plane >= 1);
                    /* Because depth is always at least 1 and we started at
                       inpam[i].depth:
                    */
                outrow[col][plane] = outrow[col][plane-1];
                break;
            }
        }
    }
}



typedef struct {
/*----------------------------------------------------------------------------
   Parameters and state for placing a row of a particular input image in
   the output in a left-right concatenation.
-----------------------------------------------------------------------------*/
    tuple *      cachedRow;
        /* Contents of the current row of the image, with depth and maxval
           adjusted for output, in malloc'ed space belonging to this object.
           Input file is positioned past this row.  Null if data not present
           and input file is positioned to the current row.
        */
    tuple *      out;
        /* Point in output row buffer where the row from this image goes */
    tuple        background;
    unsigned int padtop;
        /* Number of rows of padding that go above this image in the output */
} LrImgCtl;

/* TODO: free LrImgCtl, including allocated stuff within */


static void
createLrImgCtlArray(const struct pam *  const inpam,  /* array */
                    unsigned int        const fileCt,
                    tuple *             const newTuplerow,
                    const struct pam *  const outpamP,
                    enum Justification  const justification,
                    enum PadColorMethod const padColorMethod,
                    enum PlanePadMethod const planePadMethod,
                    LrImgCtl **         const imgCtlP) {

    LrImgCtl * imgCtl;  /* array */
    unsigned int i;

    MALLOCARRAY_NOFAIL(imgCtl, fileCt);

    for (i = 0; i < fileCt; ++i) {
        LrImgCtl *         const thisEntryP = &imgCtl[i];
        const struct pam * const inpamP     = &inpam[i];

        switch (justification) {  /* Determine top padding */
            case JUST_MIN:
                thisEntryP->padtop = 0;
                break;
            case JUST_MAX:
                thisEntryP->padtop = outpamP->height - inpamP->height;
                break;
            case JUST_CENTER:
                thisEntryP->padtop = (outpamP->height - inpamP->height) / 2;
                break;
        }

        thisEntryP->out =
            (i == 0 ? &newTuplerow[0] : imgCtl[i-1].out + inpam[i-1].width);

        if (inpamP->height == outpamP->height)  /* no vertical padding */
            thisEntryP->cachedRow = NULL;
        else {
            /* Determine pad color */
            switch (padColorMethod){
            case PAD_AUTO:
                thisEntryP->cachedRow = pnm_allocpamrow(&inpam[i]);
                pnm_readpamrow(inpamP, thisEntryP->cachedRow);
                padPlanesRow(planePadMethod, &inpam[i], thisEntryP->cachedRow,
                             outpamP);
                pnm_scaletuplerow(&inpam[i], thisEntryP->cachedRow,
                                  thisEntryP->cachedRow, outpamP->maxval);
                {
                    struct pam cachedRowPam;
                    cachedRowPam = *outpamP;
                    cachedRowPam.width = inpamP->width;
                    thisEntryP->background = pnm_backgroundtuplerow(
                        &cachedRowPam, thisEntryP->cachedRow);
                }
                break;
            case PAD_BLACK:
                thisEntryP->cachedRow = NULL;
                pnm_createBlackTuple(outpamP, &thisEntryP->background);
                break;
            case PAD_WHITE:
                thisEntryP->cachedRow = NULL;
                pnm_createWhiteTuple(outpamP, &thisEntryP->background);
                break;
            }
        }
    }
    *imgCtlP = imgCtl;
}



static void
destroyLrImgCtlArray(LrImgCtl *   const imgCtl,  /* array */
                     unsigned int const fileCt) {

    unsigned int i;

    for (i = 0; i < fileCt; ++i) {
        LrImgCtl * const thisEntryP = &imgCtl[i];

        pnm_freepamtuple(thisEntryP->background);
        pnm_freepamrow(thisEntryP->cachedRow);
    }

    free(imgCtl);
}



static void
concatenateLeftRightGen(const struct pam *  const outpamP,
                        const struct pam *  const inpam,  /* array */
                        unsigned int        const fileCt,
                        enum Justification  const justification,
                        enum PadColorMethod const padColorMethod,
                        enum PlanePadMethod const planePadMethod) {

    tuple * const outrow = pnm_allocpamrow(outpamP);

    LrImgCtl *   imgCtl;
    unsigned int row;

    createLrImgCtlArray(inpam, fileCt, outrow, outpamP,
                        justification, padColorMethod, planePadMethod,
                        &imgCtl);

    for (row = 0; row < outpamP->height; ++row) {
        unsigned int i;

        for (i = 0; i < fileCt; ++i) {
            LrImgCtl *   const thisEntryP   = &imgCtl[i];
            const struct pam * const inpamP = &inpam[i];

            if ((row == 0 && thisEntryP->padtop > 0) ||
                row == thisEntryP->padtop + inpamP->height) {
                /* This row begins a run of padding, either above or below
                   image 'i', so set its part of outrow[] to padding.
                */
                unsigned int col;
                for (col = 0; col < inpamP->width; ++col) {
                    pnm_assigntuple(outpamP, thisEntryP->out[col],
                                    thisEntryP->background);
                }
            }
            if (row == thisEntryP->padtop && thisEntryP->cachedRow) {
                /* We're at the top row of file 'i', and that row
                   has already been read to cachedRow[] to determine
                   background.  Copy it to image i's part of outrow[].
                */
                unsigned int col;
                for (col = 0; col < inpamP->width; ++col) {
                    pnm_assigntuple(outpamP, thisEntryP->out[col],
                                    thisEntryP->cachedRow[col]);
                }
                free(thisEntryP->cachedRow);
                thisEntryP->cachedRow = NULL;
            } else if (row >= thisEntryP->padtop &&
                       row < thisEntryP->padtop + inpamP->height) {
                pnm_readpamrow(&inpam[i], thisEntryP->out);
                padPlanesRow(planePadMethod, &inpam[i], thisEntryP->out,
                             outpamP);
                pnm_scaletuplerow(&inpam[i], thisEntryP->out,
                                  thisEntryP->out, outpamP->maxval);
            } else {
                /* It's a row of padding, so image i's part of outrow[] is
                   already set appropriately.
                */
            }
        }
        /* Note that imgCtl[N].out[] is an alias to part of outrow[], so
           outrow[] has been set.
        */
        pnm_writepamrow(outpamP, outrow);
    }
    destroyLrImgCtlArray(imgCtl, fileCt);

    pnm_freepamrow(outrow);
}



static unsigned int
leftPadAmount(const struct pam * const outpamP,
              const struct pam * const inpamP,
              enum Justification const justification) {

    switch (justification) {
    case JUST_MIN:    return 0;
    case JUST_MAX:    return outpamP->width - inpamP->width;
    case JUST_CENTER: return (outpamP->width - inpamP->width) / 2;
    }
    assert(false);
}



static void
setHorizPadding(tuple *            const newTuplerow,
                const struct pam * const outpamP,
                bool               const backChanged,
                const struct pam * const inpam,  /* array */
                unsigned int       const imageSeq,
                unsigned int       const padLeft,
                tuple              const background) {
/*----------------------------------------------------------------------------
   Set the left and right padding for an output row in a top-bottom
   concatenation.

   'newTuplerow' is where we set the padding (and also assumed to hold the
   contents of the previous output row).  *outpamP describes it.

   'backChanged' means the background color is different for the current row
   than for the previous one.

   inpam[] is the array of descriptors for all the input images.  'imageSeq'
   is the index into this array for the current image.

   'background' is the background color to set.
-----------------------------------------------------------------------------*/
    if (backChanged ||
        (imageSeq > 0 && inpam[imageSeq-1].width > inpam[imageSeq].width)) {
        unsigned int col;

        for (col = 0; col < padLeft; ++col)
            pnm_assigntuple(outpamP, newTuplerow[col], background);
        for (col = padLeft + inpam[imageSeq].width;
             col < outpamP->width;
             ++col) {
            pnm_assigntuple(outpamP, newTuplerow[col], background);
        }
    } else {
        /* No need to pad because newTuplerow[] already contains the
           correct padding from the previous row.
        */
    }
}



static void
readFirstTBRowAndDetermineBackground(const struct pam *  const inpamP,
                                     const struct pam *  const outpamP,
                                     tuple *             const out,
                                     enum PlanePadMethod const planePadMethod,
                                     tuple *             const backgroundP) {
/*----------------------------------------------------------------------------
   Read the first row of an input image into 'out', adjusting it to conform
   to the output depth and maxval described by *outpamP.

   The image is positioned to the first row at entry.

   From this row, determine the background color for the input image and
   return it as *backgroundP (a newly malloced tuple).
-----------------------------------------------------------------------------*/
    struct pam partialOutpam;
        /* Descriptor for the input image with depth and maxval adjusted to
           that of the output image.
        */

    pnm_readpamrow(inpamP, out);

    padPlanesRow(planePadMethod, inpamP, out, outpamP);

    pnm_scaletuplerow(inpamP, out, out, outpamP->maxval);

    {
        partialOutpam = *outpamP;
        partialOutpam.width = inpamP->width;

        *backgroundP = pnm_backgroundtuplerow(&partialOutpam, out);
    }
}



static void
concatenateTopBottomGen(const struct pam *  const outpamP,
                        const struct pam *  const inpam,  /* array */
                        unsigned int        const fileCt,
                        enum Justification  const justification,
                        enum PadColorMethod const padColorMethod,
                        enum PlanePadMethod const planePadMethod) {

    tuple * const newTuplerow = pnm_allocpamrow(outpamP);
    tuple * out;
        /* The location in newTuplerow[] that the row from the current
           input image goes.
        */
    unsigned int i;
    tuple background;
    tuple backgroundPrev;

    switch (padColorMethod) {
    case PAD_AUTO:
        /* Backgournd is different for each input image */
        backgroundPrev = pnm_allocpamtuple(outpamP);
            /* Dummy value; just need something to free */
        break;
    case PAD_BLACK:
        pnm_createBlackTuple(outpamP, &background);
        break;
    case PAD_WHITE:
        pnm_createWhiteTuple(outpamP, &background);
        break;
    }

    for (i = 0; i < fileCt; ++i) {
        const struct pam * const inpamP = &inpam[i];

        unsigned int row;
        unsigned int startRow;
        bool backChanged;
            /* The background color is different from that of the previous
               input image.
            */

        if (inpamP->width == outpamP->width) {
            /* no padding */
            startRow = 0;
            backChanged = false;
            out = &newTuplerow[0];
        } else {
            unsigned int const padLeft =
                leftPadAmount(outpamP, inpamP, justification);

            if (padColorMethod == PAD_AUTO) {
                out = &newTuplerow[padLeft];
                readFirstTBRowAndDetermineBackground(
                    inpamP, outpamP, out, planePadMethod, &background);

                backChanged =
                    i == 0 ||
                    pnm_tupleequal(outpamP, background, backgroundPrev);
                pnm_freepamtuple(backgroundPrev);
                backgroundPrev = background;

                startRow = 1;
            } else {
                /* Background color is constant: black or white */
                startRow = 0;
                out = &newTuplerow[padLeft];
                backChanged = (i == 0);
            }

            setHorizPadding(newTuplerow, outpamP, backChanged, inpam, i,
                            padLeft, background);
        }

        if (startRow == 1)
            /* Top row already read for auto background
               color determination.  Write it out. */
            pnm_writepamrow(outpamP, newTuplerow);

        for (row = startRow; row < inpamP->height; ++row) {
            pnm_readpamrow(inpamP, out);

            pnm_writepamrow(outpamP, newTuplerow);
        }
    }
    pnm_freepamtuple(background);
    pnm_freepamrow(newTuplerow);
}



int
main(int           argc,
     const char ** argv) {

    struct CmdlineInfo cmdline;
    struct pam * inpam;  /* malloc'ed array */
    struct pam outpam;
    unsigned int i;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    MALLOCARRAY_NOFAIL(inpam, cmdline.fileCt);

    for (i = 0; i < cmdline.fileCt; ++i) {
        FILE * const ifP = pm_openr(cmdline.inputFileName[i]);
        pnm_readpaminit(ifP, &inpam[i], PAM_STRUCT_SIZE(tuple_type));
    }

    outpam.file = stdout;

    computeOutputParms(cmdline.fileCt, cmdline.orientation, inpam,
                       cmdline.verbose, &outpam);

    pnm_writepaminit(&outpam);

    if (outpam.format == RPBM_FORMAT) {
        switch (cmdline.orientation) {
        case LEFTRIGHT:
            concatenateLeftRightPbm(&outpam, inpam, cmdline.fileCt,
                                    cmdline.justification,
                                    cmdline.padColorMethod);
            break;
        case TOPBOTTOM:
            concatenateTopBottomPbm(&outpam, inpam, cmdline.fileCt,
                                    cmdline.justification,
                                    cmdline.padColorMethod);
            break;
        }
    } else {
        switch (cmdline.orientation) {
        case LEFTRIGHT:
            concatenateLeftRightGen(&outpam, inpam, cmdline.fileCt,
                                    cmdline.justification,
                                    cmdline.padColorMethod,
                                    cmdline.planePadMethod);
            break;
        case TOPBOTTOM:
            concatenateTopBottomGen(&outpam, inpam, cmdline.fileCt,
                                    cmdline.justification,
                                    cmdline.padColorMethod,
                                    cmdline.planePadMethod);
            break;
        }
    }
    for (i = 0; i < cmdline.fileCt; ++i)
        pm_close(inpam[i].file);
    free(cmdline.inputFileName);
    free(inpam);
    pm_close(stdout);

    return 0;
}



