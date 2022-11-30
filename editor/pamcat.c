/*=============================================================================
                                   pamcat
===============================================================================

  Concatenate images.

  By Bryan Henderson and Akira Urushibata.  Contributed to the public domain
  by its authors.

=============================================================================*/

#include <stdio.h>
#include <limits.h>
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


enum Orientation {TOPBOTTOM, LEFTRIGHT};
  /* Direction of concatenation */

enum Justification {JUST_CENTER, JUST_MIN, JUST_MAX};
  /* Justification of images in concatenation */

/* FOPEN_MAX is usually defined in stdio.h, PATH_MAX in limits.h
   Given below are typical values.  Adjust as necessary.
 */

#ifndef FOPEN_MAX
  #define FOPEN_MAX 16
#endif

#ifndef PATH_MAX
  #define PATH_MAX 255
#endif


static const char **
copyOfStringList(const char ** const list,
                 unsigned int  const size) {

    const char ** retval;
    unsigned int i;

    MALLOCARRAY_NOFAIL(retval, size);

    for (i = 0; i < size; ++i)
        retval[i] = pm_strdup(list[i]);

    return retval;
}



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char **       inputFileName;
    unsigned int        inputFileCt;
    const char *        listfile;  /* NULL if not specified */
    enum PadColorMethod padColorMethod;
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
    unsigned int black, white;
    unsigned int jtop, jbottom, jleft, jright, jcenter;
    unsigned int listfileSpec;

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
    OPTENT3(0, "listfile",    OPT_STRING, &cmdlineP->listfile,
            &listfileSpec,      0);
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

    if (listfileSpec) {
        if (argc-1 > 0)
          pm_error ("You can not specify files on the command line and "
                    "also -listfile.");
    } else {
        cmdlineP->listfile = NULL;

        if (argc-1 < 1) {
            MALLOCARRAY_NOFAIL(cmdlineP->inputFileName, 1);
            cmdlineP->inputFileName[0] = "-";
            cmdlineP->inputFileCt = 1;
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
            cmdlineP->inputFileCt = argc-1;
            if (stdinCt > 1)
                pm_error("At most one input image can come from "
                         "Standard Input.  You specified %u", stdinCt);
        }
    }
}



static void
freeCmdLine(struct CmdlineInfo const cmdline) {

    if (!cmdline.listfile)
        free(cmdline.inputFileName);
}



static void
createInFileListFmFile(const char  *        const listFileNm,
                       bool                 const verbose,
                       const char ***       const inputFileNmP,
                       unsigned int *       const inputFileCtP) {

    FILE * const lfP = pm_openr(listFileNm);

    const char ** inputFileNm;
    unsigned int inputFileCt;
    unsigned int emptyLineCt;
    unsigned int stdinCt;
    int eof;

    MALLOCARRAY_NOFAIL(inputFileNm, FOPEN_MAX);

    for (inputFileCt = emptyLineCt = stdinCt = eof = 0; !eof; ) {

        size_t lineLen;
        char * buf;
        size_t bufferSz;

        buf = NULL;  /* initial value */
        bufferSz = 0;  /* initial value */

        pm_getline(lfP, &buf, &bufferSz, &eof, &lineLen);

        if (!eof) {
            if (lineLen == 0)
                ++emptyLineCt;
            else if (lineLen > PATH_MAX)
                pm_error("Path/file name in list file is too long "
                         "(%u bytes).  Maximum is %u bytes",
                         (unsigned)lineLen, PATH_MAX);
            else /* 0 < lineLen < PATH_MAX */ {
                if (inputFileCt >= FOPEN_MAX)
                    pm_error("Too many files in list file.  Maximum is %u",
                             FOPEN_MAX);
                else {
                    inputFileNm[inputFileCt] = buf;
                    ++inputFileCt;
                    if (streq(buf, "-"))
                        ++stdinCt;
                }
            }
        }
    }

    pm_close(lfP);

    if (stdinCt > 1)
        pm_error("At most one input image can come from Standard Input.  "
                 "You specified %u", stdinCt);

    if (inputFileCt == 0)
        pm_error("No files specified in list file.");

    if (verbose) {
        pm_message("%u files specified and %u blank lines in list file",
                   inputFileCt, emptyLineCt);
    }

    *inputFileCtP = inputFileCt;
    *inputFileNmP = inputFileNm;
}



static void
createInFileList(struct CmdlineInfo const cmdline,
                 bool               const verbose,
                 const char ***     const inputFileNmP,
                 unsigned int *     const inputFileCtP) {

    if (cmdline.listfile)
        createInFileListFmFile(cmdline.listfile, verbose,
                               inputFileNmP, inputFileCtP);
    else {
        *inputFileCtP = cmdline.inputFileCt;
        *inputFileNmP = copyOfStringList(cmdline.inputFileName,
                                         cmdline.inputFileCt);
    }
}



static void
freeInFileList(const char ** const inputFileNm,
               unsigned int  const inputFileCt) {

    unsigned int i;

    for (i = 0; i < inputFileCt; ++i)
        pm_strfree(inputFileNm[i]);

    free(inputFileNm);
}



static const char *
tupletypeX(bool         const allVisual,
           unsigned int const colorDepth,
           sample       const maxMaxval,
           bool         const haveOpacity) {

    const char * retval;

    if (allVisual) {
        switch (colorDepth) {
        case 1:
            if (maxMaxval == 1)
                retval = haveOpacity ? "BLACKANDWHITE_ALPHA" : "BLACKANDWHITE";
            else
                retval = haveOpacity ? "GRAYSCALE_ALPHA"     : "GRAYSCALE";
            break;
        case 3:
            retval = haveOpacity ? "RGB_ALPHA"           : "RGB";
            break;
        default:
            assert(false);
        }
    } else
        retval = "";

    return retval;
}



typedef struct {
    /* This describes a transformation from one tuple type to another,
       e.g. from BLACKANDWHITE to GRAY_ALPHA.

       For transformations bewteen the defined ones for visual images,
       only the "up" transformations are covered.
    */
    bool mustPromoteColor;
        /* Plane 0, which is the black/white or grayscale plane and also
           the red plane must be copied as the red, green, and blue planes
           (0, 1, and 2).
        */
    bool mustPromoteOpacity;
        /* Plane 1, which is the opacity plane for black and white or
           grayscale tuples, must be copied as the RGB opacity plane (3).
        */
    bool mustCreateOpacity;
        /* The opacity plane value must be set to opaque */

    bool mustPadZero;
        /* Where the target tuple type is deeper than the source tuple
           type, all higher numbered planes must be cleared to zero.

           This is mutually exclusive with the rest of the musts.
        */

} TtTransform;



static TtTransform
ttXformForImg(const struct pam * const inpamP,
              const struct pam * const outpamP) {
/*----------------------------------------------------------------------------
  The transform required to transform tuples of the kind described by *inpamP
  to tuples of the kind described by *outpamP (e.g. from grayscale to RGB,
  which involves replicating one plane into three).

  We assume *outpamP tuples are of a type that is at least as expressive as
  *inpamP tuples.  So e.g. outpamP->tuple_type cannot be "GRAYSCALE" if
  inpamP->tuple_type is "RGB".
-----------------------------------------------------------------------------*/
    TtTransform retval;

    if (inpamP->visual && outpamP->visual) {
        retval.mustPromoteColor   =
            (outpamP->color_depth > inpamP->color_depth);
        retval.mustPromoteOpacity =
            (outpamP->color_depth > inpamP->color_depth &&
             (outpamP->have_opacity && inpamP->have_opacity));
        retval.mustCreateOpacity  =
            (outpamP->have_opacity && !inpamP->have_opacity);
        retval.mustPadZero = false;
    } else {
        retval.mustPromoteColor   = false;
        retval.mustPromoteOpacity = false;
        retval.mustCreateOpacity  = false;
        retval.mustPadZero        = true;
    }
    return retval;
}



static void
reportPlans(unsigned int       const fileCt,
            const struct pam * const outpamP) {

    pm_message("Concatenating %u input images", fileCt);

    pm_message("Output width, height, depth: %u x %u x %u",
               outpamP->width, outpamP->height, outpamP->depth);

    if (outpamP->format == RPBM_FORMAT)
        pm_message("Using PBM fast path and producing raw PBM output");
    else if (outpamP->format == PBM_FORMAT)
        pm_message("Output format: Plain PBM");
    else {
        pm_message("Output maxval (max of all inputs): %lu", outpamP->maxval);

        switch (outpamP->format) {
        case PGM_FORMAT:
            pm_message("Output format: Plain PGM");
            break;
        case RPGM_FORMAT:
            pm_message("Output format: Raw PGM");
            break;
        case PPM_FORMAT:
            pm_message("Output format: Plain PPM");
            break;
        case RPPM_FORMAT:
            pm_message("Output format: Raw PPM");
            break;
        case PAM_FORMAT:
            pm_message("Output format: PAM");

            if (strlen(outpamP->tuple_type) > 0)
                pm_message("Output tuple type: '%s'", outpamP->tuple_type);
            else
                pm_message("Output tuple type is null string because "
                           "input images have various non-visual tuple types");
            break;
        }
    }
}



static void
computeOutputParms(unsigned int       const fileCt,
                   enum Orientation   const orientation,
                   const struct pam * const inpam,  /* array */
                   bool               const verbose,
                   struct pam *       const outpamP) {

    double newCols, newRows;
    unsigned int maxDepth;
    sample maxMaxval;
    int newFormat;
    const char * firstTupletype;
    bool allSameTt;
    bool allVisual;
    unsigned int maxColorDepth;
    bool haveOpacity;
    unsigned int fileSeq;

    for (fileSeq = 0, newCols = 0, newRows = 0, maxDepth = 0, maxMaxval = 0,
             newFormat = 0,
             allVisual = true, maxColorDepth = 0, haveOpacity = false,
             firstTupletype = NULL, allSameTt = true;
         fileSeq < fileCt;
         ++fileSeq) {

        const struct pam * const inpamP = &inpam[fileSeq];

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

        if (!firstTupletype)
            firstTupletype = inpamP->tuple_type;
        if (inpamP->tuple_type != firstTupletype)
            allSameTt = false;

        if (inpamP->visual) {
            maxColorDepth = MAX(maxColorDepth, inpamP->color_depth);

            if (inpamP->have_opacity)
                haveOpacity = true;
        } else
            allVisual = false;

        maxDepth      = MAX(maxDepth,      inpamP->depth);
        maxMaxval     = MAX(maxMaxval,     inpamP->maxval);

        if (PAM_FORMAT_TYPE(inpamP->format) > PAM_FORMAT_TYPE(newFormat))
            newFormat = inpamP->format;
    }
    assert(newCols       > 0);
    assert(newRows       > 0);
    assert(maxMaxval     > 0);
    assert(newFormat     > 0);

    if (newCols > INT_MAX)
       pm_error("Output width too large: %.0f.", newCols);
    if (newRows > INT_MAX)
       pm_error("Output height too large: %.0f.", newRows);

    outpamP->size = sizeof(*outpamP);
    outpamP->len  = PAM_STRUCT_SIZE(tuple_type);

    /* Note that while 'double' is not in general a precise numerical type,
       in the case of a sum of integers which is less than INT_MAX, it
       is exact, because double's precision is greater than int's.
    */
    outpamP->height           = (unsigned int)newRows;
    outpamP->width            = (unsigned int)newCols;
    if (allVisual)
        outpamP->depth        = MAX(maxDepth,
                                    maxColorDepth + (haveOpacity ? 1 : 0));
    else
        outpamP->depth        = maxDepth;
    outpamP->allocation_depth = 0;  /* This means same as depth */
    outpamP->maxval           = maxMaxval;
    outpamP->format           = newFormat;
    if (allSameTt)
        STRSCPY(outpamP->tuple_type, firstTupletype);
    else
        STRSCPY(outpamP->tuple_type,
                tupletypeX(allVisual, maxColorDepth, maxMaxval, haveOpacity));
    outpamP->comment_p        = NULL;
    outpamP->plainformat      = false;

    if (verbose)
        reportPlans(fileCt, outpamP);
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



/* concatenateLeftRightPbm() and concatenateLeftRightGen()
   employ almost identical algorithms.
   The difference is in the data types and functions.

   Same for concatenateTopBottomPbm() and concatenateTopBottomGen().
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
} LrImgCtlPbm;



static void
createLrImgCtlPbm(const struct pam *  const inpam,  /* array */
                  unsigned int        const fileCt,
                  unsigned int        const outHeight,
                  enum Justification  const justification,
                  enum PadColorMethod const padColorMethod,
                  LrImgCtlPbm **      const imgCtlP) {
/*----------------------------------------------------------------------------
   Read the first row of each image in inpam[] and return that and additional
   information about images as *imgCtlP.
-----------------------------------------------------------------------------*/
    LrImgCtlPbm * imgCtl;  /* array, size 'fileCt' */
    unsigned int fileSeq;

    MALLOCARRAY_NOFAIL(imgCtl, fileCt);

    for (fileSeq = 0; fileSeq < fileCt; ++fileSeq) {
        LrImgCtlPbm *      const imgCtlP = &imgCtl[fileSeq];
        const struct pam * const inpamP  = &inpam[fileSeq];

        switch (justification) {
        case JUST_MIN:
            imgCtlP->padtop = 0;
            break;
        case JUST_MAX:
            imgCtlP->padtop = outHeight - inpam[fileSeq].height;
            break;
        case JUST_CENTER:
            imgCtlP->padtop = (outHeight - inpamP->height) / 2;
            break;
        }

        imgCtlP->offset =
            (fileSeq == 0) ?
                0 : imgCtl[fileSeq-1].offset + inpam[fileSeq-1].width;

        if (inpamP->height == outHeight)  /* no padding */
            imgCtlP->proberow = NULL;
        else {                   /* determine pad color for image i */
            switch (padColorMethod) {
            case PAD_AUTO: {
                bit bgBit;
                imgCtlP->proberow =
                    pbm_allocrow_packed((unsigned int)inpamP->width + 7);
                pbm_readpbmrow_bitoffset(
                    inpamP->file, imgCtlP->proberow,
                    inpamP->width, inpamP->format, imgCtlP->offset % 8);

                bgBit = pbm_backgroundbitrow(
                    imgCtlP->proberow, inpamP->width,
                    imgCtlP->offset % 8);

                imgCtlP->background = bgBit == PBM_BLACK ? 0xff : 0x00;
            } break;
            case PAD_BLACK:
                imgCtlP->proberow   = NULL;
                imgCtlP->background = 0xff;
                break;
            case PAD_WHITE:
                imgCtlP->proberow   = NULL;
                imgCtlP->background = 0x00;
                break;
            }
        }
    }
    *imgCtlP = imgCtl;
}



static void
destroyPbmImgCtl(LrImgCtlPbm * const imgCtl,  /* array */
                 unsigned int  const fileCt) {

    unsigned int i;

    for (i = 0; i < fileCt; ++i) {
        if (imgCtl[i].proberow)
            free(imgCtl[i].proberow);
    }
    free(imgCtl);
}



static void
concatenateLeftRightPbm(struct pam *        const outpamP,
                        const struct pam *  const inpam,  /* array */
                        unsigned int        const fileCt,
                        enum Justification  const justification,
                        enum PadColorMethod const padColorMethod) {

    unsigned char * const outrow = pbm_allocrow_packed(outpamP->width);
        /* We use just one outrow.  All padding and image data (with the
           exception of following imgCtl.proberow) goes directly into this
           packed PBM row.
        */

    LrImgCtlPbm * imgCtl;
        /* malloc'ed array, one element per image.  Shadows inpam[] */
    unsigned int row;

    createLrImgCtlPbm(inpam, fileCt, outpamP->height,
                      justification, padColorMethod,
                      &imgCtl);

    outrow[pbm_packed_bytes(outpamP->width)-1] = 0x00;

    for (row = 0; row < outpamP->height; ++row) {
        unsigned int fileSeq;

        for (fileSeq = 0; fileSeq < fileCt; ++fileSeq) {
            const LrImgCtlPbm * const imgCtlP = &imgCtl[fileSeq];
            const struct pam *  const inpamP  = &inpam[fileSeq];

            if ((row == 0 && imgCtlP->padtop > 0) ||
                row == imgCtlP->padtop + inpamP->height) {

                /* This row begins a run of padding, either above or below
                   file 'i', so set 'outrow' to padding.
                */
                padFillBitrow(outrow, imgCtlP->background, inpamP->width,
                              imgCtlP->offset);
            }

            if (row == imgCtlP->padtop && imgCtlP->proberow != NULL) {
                /* Top row has been read to proberow[] to determine
                   background.  Copy it to outrow[].
                */
                copyBitrow(imgCtlP->proberow, outrow,
                           inpamP->width, imgCtlP->offset);
            } else if (row >= imgCtlP->padtop &&
                       row < imgCtlP->padtop + inpamP->height) {
                pbm_readpbmrow_bitoffset(
                    inpamP->file, outrow, inpamP->width, inpamP->format,
                    imgCtlP->offset);
            } else {
                /* It's a row of padding, so outrow[] is already set
                   appropriately.
                */
            }
        }
        pbm_writepbmrow_packed(outpamP->file, outrow, outpamP->width, 0);
    }

    destroyPbmImgCtl(imgCtl, fileCt);

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

    unsigned int fileSeq;
    unsigned int row, startRow;

    outrow[pbm_packed_bytes(outpamP->width)-1] = 0x00;

    switch (padColorMethod){
    case PAD_AUTO:   /* do nothing */    break;
    case PAD_BLACK:  background = 0xff;  break;
    case PAD_WHITE:  background = 0x00;  break;
    }

    for (fileSeq = 0; fileSeq < fileCt; ++fileSeq) {
        const struct pam * const inpamP = &inpam[fileSeq];

        if (inpamP->width == outpamP->width) {
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
                padleft = outpamP->width - inpamP->width;
                break;
            case JUST_CENTER:
                padleft = (outpamP->width - inpamP->width) / 2;
                break;
            }

            switch (padColorMethod) {
            case PAD_AUTO: {
                bit bgBit;

                startRow = 1;

                pbm_readpbmrow_bitoffset(
                    inpamP->file, outrow, inpamP->width, inpamP->format,
                    padleft);

                bgBit = pbm_backgroundbitrow(outrow, inpamP->width, padleft);
                background = bgBit == PBM_BLACK ? 0xff : 0x00;

                backChange = (fileSeq == 0 || background != backgroundPrev);
            } break;
            case PAD_WHITE:
            case PAD_BLACK:
                startRow = 0;
                backChange = (fileSeq == 0);
                break;
            }

            if (backChange ||
                (fileSeq > 0 && inpam[fileSeq-1].width > inpamP->width)) {
                unsigned int const padright =
                    outpamP->width - padleft - inpamP->width;

                if (padleft > 0)
                    padFillBitrow(outrow, background, padleft, 0);

                if (padright > 0)
                    padFillBitrow(outrow, background, padright,
                                  padleft + inpamP->width);

            }
        }

        if (startRow == 1)
            /* Top row already read for auto background color
               determination.  Write it out.
            */
            pbm_writepbmrow_packed(outpamP->file, outrow, outpamP->width, 0);

        for (row = startRow; row < inpamP->height; ++row) {
            pbm_readpbmrow_bitoffset(inpamP->file, outrow, inpamP->width,
                                     inpamP->format, padleft);
            pbm_writepbmrow_packed(outpamP->file, outrow, outpamP->width, 0);
        }

        backgroundPrev = background;
    }
    pbm_freerow_packed(outrow);
}



static void
padPlanesRow(const struct pam *  const inpamP,
             tuple *             const outrow,
             const struct pam *  const outpamP) {
/*----------------------------------------------------------------------------
  Rearrange the planes of *outrow as needed to transform them into tuples
  as described by *outpamP from tuples as described by *inpamP.
-----------------------------------------------------------------------------*/
    TtTransform const ttTransform = ttXformForImg(inpamP, outpamP);

    assert(inpamP->allocation_depth >= outpamP->depth);

    if (ttTransform.mustPromoteOpacity) {
        unsigned int col;

        assert(outpamP->depth >= PAM_TRN_PLANE);

        for (col = 0; col < inpamP->width; ++col) {
            outrow[col][outpamP->opacity_plane] =
                outrow[col][inpamP->opacity_plane];
        }
    }
    if (ttTransform.mustPromoteColor) {
        unsigned int col;

        assert(outpamP->depth >= PAM_GRN_PLANE);
        assert(outpamP->depth >= PAM_BLU_PLANE);

        for (col = 0; col < inpamP->width; ++col) {
            assert(PAM_RED_PLANE == 0);
            outrow[col][PAM_GRN_PLANE] = outrow[col][0];
            outrow[col][PAM_BLU_PLANE] = outrow[col][0];
        }
    }

    if (ttTransform.mustCreateOpacity) {
        unsigned int col;

        for (col = 0; col < inpamP->width; ++col)
            outrow[col][outpamP->opacity_plane] = outpamP->maxval;
    }

    if (ttTransform.mustPadZero) {
        unsigned int plane;

        for (plane = inpamP->depth; plane < outpamP->depth; ++plane) {
            unsigned int col;

            for (col = 0; col < inpamP->width; ++col)
                outrow[col][plane] = 0;
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



static void
createLrImgCtlArray(const struct pam *  const inpam,  /* array */
                    unsigned int        const fileCt,
                    tuple *             const newTuplerow,
                    const struct pam *  const outpamP,
                    enum Justification  const justification,
                    enum PadColorMethod const padColorMethod,
                    LrImgCtl **         const imgCtlP) {

    LrImgCtl * imgCtl;  /* array */
    unsigned int fileSeq;

    MALLOCARRAY_NOFAIL(imgCtl, fileCt);

    for (fileSeq = 0; fileSeq < fileCt; ++fileSeq) {
        LrImgCtl *         const thisEntryP = &imgCtl[fileSeq];
        const struct pam * const inpamP     = &inpam[fileSeq];

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
            (fileSeq == 0 ?
             &newTuplerow[0] : imgCtl[fileSeq-1].out + inpam[fileSeq-1].width);

        if (inpamP->height == outpamP->height) { /* no vertical padding */
            thisEntryP->cachedRow  = NULL;
            pnm_createBlackTuple(outpamP, &thisEntryP->background);
                /* Meaningless because no padding */
        } else {
            /* Determine pad color */
            switch (padColorMethod){
            case PAD_AUTO:
                thisEntryP->cachedRow = pnm_allocpamrow(inpamP);
                pnm_readpamrow(inpamP, thisEntryP->cachedRow);
                pnm_scaletuplerow(inpamP, thisEntryP->cachedRow,
                                  thisEntryP->cachedRow, outpamP->maxval);
                padPlanesRow(inpamP, thisEntryP->cachedRow, outpamP);
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
        if (outpamP->visual) {
            /* Any opacity sample in background color tuple is meaningless at
               this point; make it opaque.
            */
            if (outpamP->have_opacity) {
                thisEntryP->background[outpamP->opacity_plane] =
                    outpamP->maxval;
            }
        }

    }
    *imgCtlP = imgCtl;
}



static void
destroyLrImgCtlArray(LrImgCtl *   const imgCtl,  /* array */
                     unsigned int const fileCt) {

    unsigned int fileSeq;

    for (fileSeq = 0; fileSeq < fileCt; ++fileSeq) {
        LrImgCtl * const thisEntryP = &imgCtl[fileSeq];

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
                        enum PadColorMethod const padColorMethod) {

    tuple * const outrow = pnm_allocpamrow(outpamP);

    LrImgCtl *   imgCtl;
    unsigned int row;

    createLrImgCtlArray(inpam, fileCt, outrow, outpamP,
                        justification, padColorMethod,
                        &imgCtl);

    for (row = 0; row < outpamP->height; ++row) {
        unsigned int fileSeq;

        for (fileSeq = 0; fileSeq < fileCt; ++fileSeq) {
            LrImgCtl *   const thisEntryP   = &imgCtl[fileSeq];
            const struct pam * const inpamP = &inpam[fileSeq];

            if ((row == 0 && thisEntryP->padtop > 0) ||
                row == thisEntryP->padtop + inpamP->height) {
                /* This row begins a run of padding, either above or below
                   image 'fileSeq', so set its part of outrow[] to padding.
                */
                unsigned int col;
                for (col = 0; col < inpamP->width; ++col) {
                    pnm_assigntuple(outpamP, thisEntryP->out[col],
                                    thisEntryP->background);
                }
            }
            if (row == thisEntryP->padtop && thisEntryP->cachedRow) {
                /* We're at the top row of image 'fileSeq', and that row
                   has already been read to cachedRow[] to determine
                   background.  Copy it to image fileseq's part of outrow[].
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
                pnm_readpamrow(inpamP, thisEntryP->out);
                pnm_scaletuplerow(inpamP, thisEntryP->out,
                                  thisEntryP->out, outpamP->maxval);
                padPlanesRow(inpamP, thisEntryP->out, outpamP);
            } else {
                /* It's a row of padding, so image filesSeq's part of outrow[]
                   is already set appropriately.
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



static tuple
initialBackgroundColor(const struct pam *  const outpamP,
                       enum PadColorMethod const padColorMethod) {

    tuple retval;

    switch (padColorMethod) {
    case PAD_AUTO:
        /* Background is different for each input image */
        retval = pnm_allocpamtuple(outpamP);
            /* Dummy value; just need something to free */
        break;
    case PAD_BLACK:
        pnm_createBlackTuple(outpamP, &retval);
        break;
    case PAD_WHITE:
        pnm_createWhiteTuple(outpamP, &retval);
        break;
    }

    if (outpamP->visual) {
        /* Any opacity sample in background color tuple is meaningless at this
           point; make it opaque.
        */
        if (outpamP->have_opacity)
            retval[outpamP->opacity_plane] = outpamP->maxval;
    }

    return retval;
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
   from that of the previous row.

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
                                     tuple *             const backgroundP) {
/*----------------------------------------------------------------------------
   Read the first row of an input image into 'out', adjusting it to conform
   to the output depth and maxval described by *outpamP.

   The image is positioned to the first row at entry.

   From this row, determine the background color for the input image and
   return it as *backgroundP (a newly malloced tuple).
-----------------------------------------------------------------------------*/
    pnm_readpamrow(inpamP, out);

    pnm_scaletuplerow(inpamP, out, out, outpamP->maxval);

    padPlanesRow(inpamP, out, outpamP);

    {
        struct pam partialOutpam;
            /* Descriptor for the input image with depth and maxval adjusted to
               that of the output image.
            */
        tuple background;

        partialOutpam = *outpamP;
        partialOutpam.width = inpamP->width;

        background = pnm_backgroundtuplerow(&partialOutpam, out);

        if (outpamP->visual) {
            /* Make the background opaque. */
            if (outpamP->have_opacity)
                background[outpamP->opacity_plane] = outpamP->maxval;
        }

        *backgroundP = background;
    }
}



static void
concatenateTopBottomGen(const struct pam *  const outpamP,
                        const struct pam *  const inpam,  /* array */
                        unsigned int        const fileCt,
                        enum Justification  const justification,
                        enum PadColorMethod const padColorMethod) {

    tuple * const newTuplerow = pnm_allocpamrow(outpamP);
    tuple * out;
        /* The location in newTuplerow[] that the row from the current
           input image goes.
        */
    unsigned int fileSeq;
    tuple background;
    tuple backgroundPrev;

    background = initialBackgroundColor(outpamP, padColorMethod);

    for (fileSeq = 0; fileSeq < fileCt; ++fileSeq) {
        const struct pam * const inpamP = &inpam[fileSeq];

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
                backgroundPrev = background;
                readFirstTBRowAndDetermineBackground(
                    inpamP, outpamP, out, &background);

                backChanged =
                    fileSeq == 0 ||
                        pnm_tupleequal(outpamP, background, backgroundPrev);
                pnm_freepamtuple(backgroundPrev);

                startRow = 1;
            } else {
                /* Background color is constant: black or white */
                startRow = 0;
                out = &newTuplerow[padLeft];
                backChanged = (fileSeq == 0);
            }

            setHorizPadding(newTuplerow, outpamP, backChanged, inpam, fileSeq,
                            padLeft, background);
        }

        if (startRow == 1)
            /* Top row was already read for auto background color
               determination.  Write it out.
            */
            pnm_writepamrow(outpamP, newTuplerow);

        for (row = startRow; row < inpamP->height; ++row) {
            pnm_readpamrow(inpamP, out);

            pnm_scaletuplerow(inpamP, out, out, outpamP->maxval);

            padPlanesRow(inpamP, out, outpamP);

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
    const char ** inputFileNm;
    unsigned int inputFileCt;
    struct pam * inpam;  /* malloc'ed array */
    struct pam outpam;
    unsigned int i;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    createInFileList(cmdline, !!cmdline.verbose, &inputFileNm, &inputFileCt);

    MALLOCARRAY_NOFAIL(inpam, inputFileCt);

    for (i = 0; i < inputFileCt; ++i) {
        FILE *  ifP;
        ifP = pm_openr(inputFileNm[i]);
        inpam[i].comment_p = NULL;  /* Don't want to see the comments */
        pnm_readpaminit(ifP, &inpam[i], PAM_STRUCT_SIZE(opacity_plane));
    }

    computeOutputParms(inputFileCt, cmdline.orientation, inpam,
                       cmdline.verbose, &outpam);

    outpam.file = stdout;

    for (i = 0; i < inputFileCt; ++i)
        pnm_setminallocationdepth(&inpam[i], outpam.depth);

    pnm_writepaminit(&outpam);

    if (outpam.format == RPBM_FORMAT) {
        switch (cmdline.orientation) {
        case LEFTRIGHT:
            concatenateLeftRightPbm(&outpam, inpam, inputFileCt,
                                    cmdline.justification,
                                    cmdline.padColorMethod);
            break;
        case TOPBOTTOM:
            concatenateTopBottomPbm(&outpam, inpam, inputFileCt,
                                    cmdline.justification,
                                    cmdline.padColorMethod);
            break;
        }
    } else {
        switch (cmdline.orientation) {
        case LEFTRIGHT:
            concatenateLeftRightGen(&outpam, inpam, inputFileCt,
                                    cmdline.justification,
                                    cmdline.padColorMethod);
            break;
        case TOPBOTTOM:
            concatenateTopBottomGen(&outpam, inpam, inputFileCt,
                                    cmdline.justification,
                                    cmdline.padColorMethod);
            break;
        }
    }
    for (i = 0; i < inputFileCt; ++i)
        pm_close(inpam[i].file);
    free(inpam);
    freeInFileList(inputFileNm, inputFileCt);
    freeCmdLine(cmdline);
    pm_close(stdout);

    return 0;
}



