/*=============================================================================
                               pnmcolormap
===============================================================================
  Create a colormap file (a PPM image containing one pixel of each of a set
  of colors).  Base the set of colors on an input image.

  For PGM input, do the equivalent for grayscale and produce a PGM graymap.

  By Bryan Henderson, San Jose, CA 2001.12.17

  Derived from ppmquant, originally by Jef Poskanzer.

  Copyright (C) 1989, 1991 by Jef Poskanzer.
  Copyright (C) 2001 by Bryan Henderson.

  Permission to use, copy, modify, and distribute this software and its
  documentation for any purpose and without fee is hereby granted, provided
  that the above copyright notice appear in all copies and that both that
  copyright notice and this permission notice appear in supporting
  documentation.  This software is provided "as is" without express or
  implied warranty.
=============================================================================*/
#include <assert.h>
#include <math.h>

#include "pm_config.h"
#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pam.h"
#include "pammap.h"

enum MethodForLargest {LARGE_NORM, LARGE_LUM};

enum MethodForRep {REP_CENTER_BOX, REP_AVERAGE_COLORS, REP_AVERAGE_PIXELS};

enum MethodForSplit {SPLIT_MAX_PIXELS, SPLIT_MAX_COLORS, SPLIT_MAX_SPREAD};

struct Box {
/*----------------------------------------------------------------------------
   A box contains an extent of a color frequency table, i.e. the colors
   with some consecutive index values in the color frequency table.
-----------------------------------------------------------------------------*/
    unsigned int serialNum;
        /* Unique identifier of this box; sequence number of creation. */
    unsigned int startIndex;
        /* First index in the extent */
    unsigned int colorCt;
        /* Size of the extent (Number of colors in it -- at least 1) */
    unsigned int sum;
        /* Number of pixels of all colors in the extent */
    unsigned int maxdim;
        /* Which dimension has the largest spread.  RGB plane number. */
        /* Meaningless if box contains only 1 color */
    sample       spread;
        /* spread in dimension 'maxdim' */
        /* Meaningless if box contains only 1 color */
};

struct BoxVector {
    tupletable2 colorFreqTable;
        /* The colors and their frequencies (number of pixels in the image of
           that color), ordered into consecutive boxes, as defined by 'box'.
        */
    unsigned int colorDepth;
        /* Number of planes in the tuples of 'colorFreqTable' */
    struct Box * box;  /* malloc'ed array */
        /* An array of boxes that contain consecutive extents of
           'colorFreqTable'.  The list covers the entire table.
        */
    unsigned int boxCt;
        /* Number of boxes in the above list */
    unsigned int capacity;
        /* Number of boxes the array is capable of containing */
};

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileNm;  /* Name of input file */
    unsigned int allcolors;  /* boolean: select all colors from the input */
    unsigned int newcolors;
        /* Number of colors argument; meaningless if allcolors true */
    enum MethodForLargest methodForLargest;
        /* -spreadintensity/-spreadluminosity options */
    enum MethodForRep methodForRep;
        /* -center/-meancolor/-meanpixel options */
    enum MethodForSplit methodForSplit;
        /* -splitpixelct/-splitcolorct/-splitspread options */
    unsigned int sort;
    unsigned int square;
    unsigned int verbose;
    unsigned int debug;
};



static void
parseCommandLine (int argc, const char ** argv,
                  struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry *option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int spreadbrightness, spreadluminosity;
    unsigned int center, meancolor, meanpixel;
    unsigned int splitpixelct, splitcolorct, splitspread;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "spreadbrightness", OPT_FLAG,
            NULL,                       &spreadbrightness, 0);
    OPTENT3(0,   "spreadluminosity", OPT_FLAG,
            NULL,                       &spreadluminosity, 0);
    OPTENT3(0,   "center",           OPT_FLAG,
            NULL,                       &center,           0);
    OPTENT3(0,   "meancolor",        OPT_FLAG,
            NULL,                       &meancolor,        0);
    OPTENT3(0,   "meanpixel",        OPT_FLAG,
            NULL,                       &meanpixel,        0);
    OPTENT3(0,   "splitpixelct",     OPT_FLAG,
            NULL,                       &splitpixelct,     0);
    OPTENT3(0,   "splitcolorct",     OPT_FLAG,
            NULL,                       &splitcolorct,     0);
    OPTENT3(0,   "splitspread",      OPT_FLAG,
            NULL,                       &splitspread,      0);
    OPTENT3(0, "sort",               OPT_FLAG,   NULL,
            &cmdlineP->sort,                               0);
    OPTENT3(0, "square",             OPT_FLAG,   NULL,
            &cmdlineP->square,                             0);
    OPTENT3(0, "verbose",            OPT_FLAG,   NULL,
            &cmdlineP->verbose,                            0);
    OPTENT3(0, "debug",              OPT_FLAG,   NULL,
            &cmdlineP->debug,                              0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3( &argc, (char **)argv, opt, sizeof(opt), 0 );
        /* Uses and sets argc, argv, and some of *cmdline_p and others. */


    if (spreadbrightness && spreadluminosity)
        pm_error("You cannot specify both -spreadbrightness and "
                 "spreadluminosity.");
    if (spreadluminosity)
        cmdlineP->methodForLargest = LARGE_LUM;
    else
        cmdlineP->methodForLargest = LARGE_NORM;

    if (center + meancolor + meanpixel > 1)
        pm_error("You can specify only one of -center, -meancolor, and "
                 "-meanpixel.");
    if (meancolor)
        cmdlineP->methodForRep = REP_AVERAGE_COLORS;
    else if (meanpixel)
        cmdlineP->methodForRep = REP_AVERAGE_PIXELS;
    else
        cmdlineP->methodForRep = REP_CENTER_BOX;

    if (splitpixelct)
        cmdlineP->methodForSplit = SPLIT_MAX_PIXELS;
    else if (splitcolorct)
        cmdlineP->methodForSplit = SPLIT_MAX_COLORS;
    else if (splitspread)
        cmdlineP->methodForSplit = SPLIT_MAX_SPREAD;
    else
        cmdlineP->methodForSplit = SPLIT_MAX_PIXELS;

    if (argc-1 > 2)
        pm_error("Program takes at most two arguments: number of colors "
                 "and input file specification.  "
                 "You specified %d arguments.", argc-1);
    else {
        if (argc-1 < 2)
            cmdlineP->inputFileNm = "-";
        else
            cmdlineP->inputFileNm = argv[2];

        if (argc-1 < 1)
            pm_error("You must specify the number of colors in the "
                     "output as an argument.");
        else {
            if (strcmp(argv[1], "all") == 0)
                cmdlineP->allcolors = TRUE;
            else {
                char * tail;
                long int const newcolors = strtol(argv[1], &tail, 10);
                if (*tail != '\0')
                    pm_error("The number of colors argument '%s' is not "
                             "a number or 'all'", argv[1]);
                else if (newcolors < 1)
                    pm_error("The number of colors must be positive");
                else if (newcolors == 1)
                    pm_error("The number of colors must be greater than 1.");
                else {
                    cmdlineP->newcolors = newcolors;
                    cmdlineP->allcolors = FALSE;
                }
            }
        }
    }
}



#ifndef LITERAL_FN_DEF_MATCH
static qsort_comparison_fn compareColor;
#endif

static struct {
/*----------------------------------------------------------------------------
  This is a parameter to compareColor().  We use this global variable
  so that compareColor() can be called by qsort(), to compare two
  tuples.  qsort() doesn't pass any arguments except the two tuples.
-----------------------------------------------------------------------------*/
    unsigned int comparePlane;
        /* The number of the plane to compare between the tuples */
    unsigned int colorDepth;
        /* Depth (number of planes) of the tuples */
} compareColorParm;

static int
compareColor(const void * const arg1,
             const void * const arg2) {

    const struct tupleint * const * const comparandPP  = arg1;
    const struct tupleint * const * const comparatorPP = arg2;

    sample const comparandSample  =
        (*comparandPP) ->tuple[compareColorParm.comparePlane];
    sample const comparatorSample =
        (*comparatorPP)->tuple[compareColorParm.comparePlane];

    int retval;

    if (comparandSample < comparatorSample)
        retval = -1;
    else if (comparandSample > comparatorSample)
        retval = +1;
    else {
        /* In the plane that matters, samples are equal, but we're going to
           try to differentiate the colors anyway so as to make qsort put the
           colors in a deterministic order so the boxes are deterministic.
        */
        unsigned int plane;
        int bestDiffSoFar;  /* -1, 0, or 1, like our return value */
        for (plane = 0, bestDiffSoFar = 0;
             plane < compareColorParm.colorDepth && bestDiffSoFar == 0;
             ++plane) {

            sample const comparandSample  =
                (*comparandPP) ->tuple[compareColorParm.comparePlane];
            sample const comparatorSample =
                (*comparatorPP)->tuple[compareColorParm.comparePlane];

            if (comparandSample < comparatorSample)
                bestDiffSoFar = -1;
            else if (comparandSample > comparatorSample)
                bestDiffSoFar = +1;
        }
        retval = bestDiffSoFar;
    }
    return retval;
}



#ifndef LITERAL_FN_DEF_MATCH
static qsort_comparison_fn sumcompare;
#endif

static int
sumcompare(const void * const arg1,
           const void * const arg2) {

    struct Box * const comparandP  = (struct Box *)arg1;
    struct Box * const comparatorP = (struct Box *)arg2;

    return
        comparatorP->sum < comparandP->sum ? -1 :
        comparatorP->sum > comparandP->sum ? +1 :
        comparatorP->serialNum < comparandP->serialNum ? -1 :
        comparatorP->serialNum > comparandP->serialNum ? +1 :
        0;
}



#ifndef LITERAL_FN_DEF_MATCH
static qsort_comparison_fn colcompare;
#endif

static int
colcompare(const void * const arg1,
           const void * const arg2) {

    struct Box * const comparandP  = (struct Box *)arg1;
    struct Box * const comparatorP = (struct Box *)arg2;

    return
        comparatorP->colorCt < comparandP->colorCt ? -1 :
        comparatorP->colorCt > comparandP->colorCt ? 1 :
        comparatorP->serialNum < comparandP->serialNum ? -1 :
        comparatorP->serialNum > comparandP->serialNum ? +1 :
        0;
}



#ifndef LITERAL_FN_DEF_MATCH
static qsort_comparison_fn spreadcompare;
#endif

static int
spreadcompare(const void * const arg1,
              const void * const arg2) {

    struct Box * const comparandP  = (struct Box *)arg1;
    struct Box * const comparatorP = (struct Box *)arg2;

    return
        comparatorP->spread < comparandP->spread ? -1 :
        comparatorP->spread > comparandP->spread ? 1 :
        comparatorP->serialNum < comparandP->serialNum ? -1 :
        comparatorP->serialNum > comparandP->serialNum ? +1 :
        0;
}



static void
sortBoxes(struct BoxVector *  const boxVectorP,
          enum MethodForSplit const methodForSplit) {

    qsort_comparison_fn * comparisonFn;

    switch (methodForSplit){
    case SPLIT_MAX_PIXELS: comparisonFn = &sumcompare;    break;
    case SPLIT_MAX_COLORS: comparisonFn = &colcompare;    break;
    case SPLIT_MAX_SPREAD: comparisonFn = &spreadcompare; break;
    }

    qsort((char*) &boxVectorP->box[0], boxVectorP->boxCt, sizeof(struct Box),
          comparisonFn);
}



/*
** Here is the fun part, the median-cut colormap generator.  This is based
** on Paul Heckbert's paper "Color Image Quantization for Frame Buffer
** Display", SIGGRAPH '82 Proceedings, page 297.
*/

static void
findBoxBoundaries(tupletable2  const colorFreqTable,
                  unsigned int const depth,
                  unsigned int const boxStart,
                  unsigned int const boxSize,
                  sample             minval[],
                  sample             maxval[]) {
/*----------------------------------------------------------------------------
  Go through the box finding the minimum and maximum of each component - the
  boundaries of the box.
-----------------------------------------------------------------------------*/
    unsigned int plane;
    unsigned int i;

    for (plane = 0; plane < depth; ++plane) {
        minval[plane] = colorFreqTable.table[boxStart]->tuple[plane];
        maxval[plane] = minval[plane];
    }

    for (i = 1; i < boxSize; ++i) {
        unsigned int plane;
        for (plane = 0; plane < depth; ++plane) {
            sample const v = colorFreqTable.table[boxStart + i]->tuple[plane];
            if (v < minval[plane]) minval[plane] = v;
            if (v > maxval[plane]) maxval[plane] = v;
        }
    }
}



static void
findPlaneWithLargestSpreadByNorm(sample         const minval[],
                                 sample         const maxval[],
                                 unsigned int   const depth,
                                 unsigned int * const planeP,
                                 sample *       const spreadP) {

    unsigned int planeWithLargest;
    sample       largestSpreadSoFar;
    unsigned int plane;

    for (plane = 0, largestSpreadSoFar = 0; plane < depth; ++plane) {

        sample const spread = maxval[plane]-minval[plane];
        if (spread > largestSpreadSoFar) {
            largestSpreadSoFar = spread;
            planeWithLargest   = plane;
        }
    }
    *planeP  = planeWithLargest;
    *spreadP = largestSpreadSoFar;
}



static void
findPlaneWithLargestSpreadByLuminosity(sample         const minval[],
                                       sample         const maxval[],
                                       unsigned int   const depth,
                                       unsigned int * const planeP,
                                       sample *       const spreadP) {
/*----------------------------------------------------------------------------
   This subroutine presumes that the tuple type is either
   BLACKANDWHITE, GRAYSCALE, or RGB (which implies pamP->depth is 1 or 3).
   To save time, we don't actually check it.
-----------------------------------------------------------------------------*/
    if (depth == 1){
        *planeP  = 0;
        *spreadP = 0;
    } else {
        /* An RGB tuple */
        unsigned int planeWithLargest;
        sample       largestSpreadSoFar;
        unsigned int plane;

        assert(depth >= 3);

        for (plane = 0, largestSpreadSoFar = 0; plane < 3; ++plane) {
            double const spread =
                pnm_lumin_factor[plane] * (maxval[plane]-minval[plane]);
            if (spread > largestSpreadSoFar) {
                largestSpreadSoFar = spread;
                planeWithLargest   = plane;
            }
        }
        *planeP  = planeWithLargest;
        *spreadP = largestSpreadSoFar;
    }
}



static void
computeBoxSpread(const struct Box *    const boxP,
                 tupletable2           const colorFreqTable,
                 unsigned int          const depth,
                 enum MethodForLargest const methodForLargest,
                 unsigned int *        const planeWithLargestP,
                 sample *              const spreadP
                 ) {
/*----------------------------------------------------------------------------
  Find the spread in the dimension in which it is greatest.

  Return as *planeWithLargestP the number of that plane and as *spreadP the
  spread in that plane.
-----------------------------------------------------------------------------*/
    sample * minval;  /* malloc'ed array */
    sample * maxval;  /* malloc'ed array */

    MALLOCARRAY_NOFAIL(minval, depth);
    MALLOCARRAY_NOFAIL(maxval, depth);

    findBoxBoundaries(colorFreqTable, depth, boxP->startIndex, boxP->colorCt,
                      minval, maxval);

    switch (methodForLargest) {
    case LARGE_NORM:
        findPlaneWithLargestSpreadByNorm(minval, maxval, depth,
                                         planeWithLargestP, spreadP);
        break;
    case LARGE_LUM:
        findPlaneWithLargestSpreadByLuminosity(minval, maxval, depth,
                                               planeWithLargestP, spreadP);
        break;
    }
    free(minval); free(maxval);
}



static unsigned int
freqTotal(tupletable2 const freqTable) {

    unsigned int i;
    unsigned int sum;

    for (i = 0, sum = 0; i < freqTable.size; ++i)
        sum += freqTable.table[i]->value;

    return sum;
}



static struct BoxVector
newBoxVector(tupletable2           const colorFreqTable,
             unsigned int          const capacity,
             unsigned int          const depth,
             enum MethodForLargest const methodForLargest) {

    unsigned int const colorCt = colorFreqTable.size;
    unsigned int const sum     = freqTotal(colorFreqTable);

    struct BoxVector boxVector;

    boxVector.colorFreqTable = colorFreqTable;
    boxVector.colorDepth     = depth;

    MALLOCARRAY(boxVector.box, capacity);

    if (!boxVector.box)
        pm_error("out of memory allocating box vector table");

    /* Set up the initial box. */
    boxVector.box[0].startIndex = 0;
    boxVector.box[0].colorCt    = colorCt;
    boxVector.box[0].sum        = sum;

    computeBoxSpread(&boxVector.box[0], colorFreqTable, depth,
                     methodForLargest,
                     &boxVector.box[0].maxdim,
                     &boxVector.box[0].spread);

    boxVector.boxCt    = 1;
    boxVector.capacity = capacity;

    return boxVector;
}



static void
destroyBoxVector(struct BoxVector const boxVector) {

    free(boxVector.box);
}



static void
centerBox(int          const boxStart,
          int          const boxSize,
          tupletable2  const colorFreqTable,
          unsigned int const depth,
          tuple        const newTuple) {

    unsigned int plane;

    for (plane = 0; plane < depth; ++plane) {
        int minval, maxval;
        unsigned int i;

        minval = maxval = colorFreqTable.table[boxStart]->tuple[plane];

        for (i = 1; i < boxSize; ++i) {
            int const v = colorFreqTable.table[boxStart + i]->tuple[plane];
            minval = MIN( minval, v);
            maxval = MAX( maxval, v);
        }
        newTuple[plane] = (minval + maxval) / 2;
    }
}



static tupletable2
newColorMap(unsigned int const colorCt,
            unsigned int const depth) {

    tupletable2 colormap;
    unsigned int i;
    struct pam pam;

    pam.depth = depth;

    colormap.table = pnm_alloctupletable(&pam, colorCt);

    for (i = 0; i < colorCt; ++i) {
        unsigned int plane;
        for (plane = 0; plane < depth; ++plane)
            colormap.table[i]->tuple[plane] = 0;
    }
    colormap.size = colorCt;

    return colormap;
}



static void
averageColors(int          const boxStart,
              int          const boxSize,
              tupletable2  const colorFreqTable,
              unsigned int const depth,
              tuple        const newTuple) {

    unsigned int plane;

    for (plane = 0; plane < depth; ++plane) {
        sample sum;
        int i;

        sum = 0;

        for (i = 0; i < boxSize; ++i)
            sum += colorFreqTable.table[boxStart+i]->tuple[plane];

        newTuple[plane] = ROUNDDIV(sum, boxSize);
    }
}



static void
averagePixels(int          const boxStart,
              int          const boxSize,
              tupletable2  const colorFreqTable,
              unsigned int const depth,
              tuple        const newTuple) {

    unsigned int n;
        /* Number of tuples represented by the box */
    unsigned int plane;
    unsigned int i;

    /* Count the tuples in question */
    n = 0;  /* initial value */
    for (i = 0; i < boxSize; ++i)
        n += colorFreqTable.table[boxStart + i]->value;


    for (plane = 0; plane < depth; ++plane) {
        sample sum;
        int i;

        sum = 0;

        for (i = 0; i < boxSize; ++i)
            sum += colorFreqTable.table[boxStart+i]->tuple[plane]
                * colorFreqTable.table[boxStart+i]->value;

        newTuple[plane] = ROUNDDIV(sum, n);
    }
}



static tupletable2
colormapFromBv(unsigned int      const colorCt,
               struct BoxVector  const boxVector,
               enum MethodForRep const methodForRep) {
    /*
    ** Ok, we've got enough boxes.  Now choose a representative color for
    ** each box.  There are a number of possible ways to make this choice.
    ** One would be to choose the center of the box; this ignores any structure
    ** within the boxes.  Another method would be to average all the colors in
    ** the box - this is the method specified in Heckbert's paper.  A third
    ** method is to average all the pixels in the box.
    */
    tupletable2 colormap;
    unsigned int boxIdx;

    colormap = newColorMap(colorCt, boxVector.colorDepth);

    for (boxIdx = 0; boxIdx < boxVector.boxCt; ++boxIdx) {
        switch (methodForRep) {
        case REP_CENTER_BOX:
            centerBox(boxVector.box[boxIdx].startIndex,
                      boxVector.box[boxIdx].colorCt,
                      boxVector.colorFreqTable, boxVector.colorDepth,
                      colormap.table[boxIdx]->tuple);
            break;
        case REP_AVERAGE_COLORS:
            averageColors(boxVector.box[boxIdx].startIndex,
                          boxVector.box[boxIdx].colorCt,
                          boxVector.colorFreqTable, boxVector.colorDepth,
                          colormap.table[boxIdx]->tuple);
            break;
        case REP_AVERAGE_PIXELS:
            averagePixels(boxVector.box[boxIdx].startIndex,
                          boxVector.box[boxIdx].colorCt,
                          boxVector.colorFreqTable, boxVector.colorDepth,
                          colormap.table[boxIdx]->tuple);
            break;
        default:
            pm_error("Internal error: invalid value of methodForRep: %d",
                     methodForRep);
        }
    }
    return colormap;
}



static void
setBox(struct Box *          const boxP,
       unsigned int          const startIndex,
       unsigned int          const colorCt,
       unsigned int          const sum,
       struct BoxVector *    const boxVectorP,
       enum MethodForLargest const methodForLargest
    ) {

    boxP->startIndex = startIndex;
    boxP->colorCt    = colorCt;
    boxP->sum        = sum;

    computeBoxSpread(boxP, boxVectorP->colorFreqTable,
                     boxVectorP->colorDepth, methodForLargest,
                     &boxP->maxdim, &boxP->spread);
}



static void
makeNewBox(struct BoxVector *    const boxVectorP,
           unsigned int          const startIndex,
           unsigned int          const colorCt,
           unsigned int          const sum,
           enum MethodForLargest const methodForLargest) {

    struct Box * const boxP = &boxVectorP->box[boxVectorP->boxCt++];

    assert(boxVectorP->boxCt <= boxVectorP->capacity);

    boxP->serialNum = boxVectorP->boxCt;

    setBox(boxP, startIndex, colorCt, sum, boxVectorP, methodForLargest);
}



static void
splitBox(struct BoxVector *    const boxVectorP,
         unsigned int          const boxIdx,
         enum MethodForLargest const methodForLargest,
         enum MethodForSplit   const methodForSplit) {
/*----------------------------------------------------------------------------
   Split Box 'boxIdx' in the box vector 'boxVector' (so that 'boxVector'
   contains one more box than it did as input).  Split it so that each new box
   represents about half of the pixels in the image for the colors in the
   original box, but with distinct colors in each of the two new boxes.

   Assume the box contains at least two colors.
-----------------------------------------------------------------------------*/
    unsigned int const boxStart = boxVectorP->box[boxIdx].startIndex;
    unsigned int const boxSize  = boxVectorP->box[boxIdx].colorCt;
    unsigned int const sum      = boxVectorP->box[boxIdx].sum;

    unsigned int medianIndex;
    unsigned int lowerSum;
        /* Number of pixels whose value is "less than" the median */

    /* Set the gross global variable 'compareColorParm' as a
       parameter to compareColor(), which is called by qsort().
    */
    compareColorParm.comparePlane    = boxVectorP->box[boxIdx].maxdim;
    compareColorParm.colorDepth      = boxVectorP->colorDepth;
    qsort((char*) &boxVectorP->colorFreqTable.table[boxStart], boxSize,
          sizeof(boxVectorP->colorFreqTable.table[boxStart]),
          compareColor);

    {
        /* Find the median based on the counts, so that about half the pixels
           (not colors, pixels) are in each subdivision.
        */
        unsigned int i;

        lowerSum = boxVectorP->colorFreqTable.table[boxStart]->value;
            /* initial value */
        for (i = 1; i < boxSize - 1 && lowerSum < sum/2; ++i) {
            lowerSum += boxVectorP->colorFreqTable.table[boxStart + i]->value;
        }
        medianIndex = i;
    }
    /* Split the box, and sort to bring the biggest boxes to the top.  The old
       box becomes the lower half; we make a new box for the upper half.
    */
    setBox(&boxVectorP->box[boxIdx],
           boxStart, medianIndex,
           lowerSum,
           boxVectorP, methodForLargest);

    makeNewBox(boxVectorP,
               boxStart + medianIndex, boxSize - medianIndex,
               sum - lowerSum,
               methodForLargest);

    sortBoxes(boxVectorP, methodForSplit);
}



static void
reportBoxVector(struct BoxVector const boxVector) {

    unsigned int i;

    pm_message("All colors of image, sorted into %u boxes:", boxVector.boxCt);

    for (i = 0; i < boxVector.boxCt; ++i) {
        const struct Box * const boxP = &boxVector.box[i];

        unsigned int j;

        pm_message("Box %u, %u colors starting with index %u (%u pixels):",
                   i, boxP->colorCt, boxP->startIndex, boxP->sum);
        if (boxP->colorCt > 1)
            pm_message("Largest spread is %lu, in plane %u",
                       boxP->spread, boxP->maxdim);

        for (j = 0; j < boxP->colorCt; ++j) {
            unsigned int colorIdx = boxP->startIndex + j;

            assert(colorIdx < boxVector.colorFreqTable.size);

            tuple const color =
                boxVector.colorFreqTable.table[colorIdx]->tuple;

            pm_message("(%lu, %lu, %lu)",
                       color[PAM_RED_PLANE],
                       color[PAM_GRN_PLANE],
                       color[PAM_BLU_PLANE]);
        }
    }
}



static void
mediancut(tupletable2           const colorFreqTable,
          unsigned int          const depth,
          unsigned int          const newColorCt,
          enum MethodForLargest const methodForLargest,
          enum MethodForRep     const methodForRep,
          enum MethodForSplit   const methodForSplit,
          bool                  const wantBvReport,
          tupletable2 *         const colormapP) {
/*----------------------------------------------------------------------------
   Compute a set of only 'newColorCt' colors that best represent an
   image whose pixels are summarized by the histogram
   'colorFreqTable'.  Each tuple in that table has depth 'depth'.
   colorFreqTable.table[i] tells the number of pixels in the subject image
   that have a particular color.

   As a side effect, sort 'colorFreqTable'.
-----------------------------------------------------------------------------*/
    struct BoxVector boxVector;
    bool multicolorBoxesExist;
        /* There is at least one box that contains at least 2 colors; ergo,
           there is more splitting we can do.
        */

    boxVector = newBoxVector(colorFreqTable, newColorCt, depth,
                             methodForLargest);

    multicolorBoxesExist = (colorFreqTable.size > 1);

    /* Split boxes until we have enough. */
    while (boxVector.boxCt < newColorCt && multicolorBoxesExist) {
        unsigned int boxIdx;

        for (boxIdx = 0;
             boxIdx < boxVector.boxCt && boxVector.box[boxIdx].colorCt < 2;
             ++boxIdx);
            /* Find the first splittable box. */

        if (boxIdx >= boxVector.boxCt)
            multicolorBoxesExist = FALSE;
        else
            splitBox(&boxVector, boxIdx, methodForLargest, methodForSplit);
                /* Side effect: sorts the extent of 'colorfreqTable' that is
                   in the box
                */
    }

    if (wantBvReport)
        reportBoxVector(boxVector);

    *colormapP = colormapFromBv(newColorCt, boxVector, methodForRep);

    destroyBoxVector(boxVector);
}




static void
validateCompatibleImage(struct pam * const inpamP,
                        struct pam * const firstPamP,
                        unsigned int const imageSeq) {

    if (inpamP->depth != firstPamP->depth)
        pm_error("Image %u depth (%u) is not the same as Image 0 (%u)",
                 imageSeq, inpamP->depth, firstPamP->depth);
    if (inpamP->maxval != firstPamP->maxval)
        pm_error("Image %u maxval (%u) is not the same as Image 0 (%u)",
                 imageSeq,
                 (unsigned)inpamP->maxval, (unsigned)firstPamP->maxval);
    if (inpamP->format != firstPamP->format)
        pm_error("Image %u format (%d) is not the same as Image 0 (%d)",
                 imageSeq, inpamP->format, firstPamP->format);
    if (!streq(inpamP->tuple_type, firstPamP->tuple_type))
        pm_error("Image %u tuple type (%s) is not the same as Image 0 (%s)",
                 imageSeq, inpamP->tuple_type, firstPamP->tuple_type);
}



static void
addImageColorsToHash(struct pam *   const pamP,
                     tuplehash      const tuplehash,
                     unsigned int * const colorCountP) {

    tuple * tuplerow;
    unsigned int row;

    tuplerow = pnm_allocpamrow(pamP);

    for (row = 0; row < pamP->height; ++row) {
        unsigned int col;

        pnm_readpamrow(pamP, tuplerow);

        for (col = 0; col < pamP->width; ++col) {
            int firstOccurrence;

            pnm_addtuplefreqoccurrence(pamP, tuplerow[col], tuplehash,
                                       &firstOccurrence);

            if (firstOccurrence)
                ++(*colorCountP);
        }
    }
    pnm_freepamrow(tuplerow);
}



static void
computeHistogram(FILE *         const ifP,
                 int *          const formatP,
                 struct pam *   const freqPamP,
                 tupletable2 *  const colorFreqTableP) {
/*----------------------------------------------------------------------------
  Make a histogram of the colors in the image stream in the file '*ifP'.

  Return as *freqPamP a description of the tuple values in the histogram.
  Only the fields of *freqPamP that describe individual tuples are
  meaningful (depth, maxval, tuple type);

  As a fringe benefit, also return the format of the input file as
  *formatP.
----------------------------------------------------------------------------*/
    unsigned int imageSeq;
    struct pam firstPam;
    tuplehash tuplehash;
    unsigned int colorCount;
    int eof;

    pm_message("making histogram...");

    tuplehash = pnm_createtuplehash();
    colorCount = 0;

    eof = FALSE;

    for (imageSeq = 0; !eof; ++imageSeq) {
        struct pam inpam;

        pm_message("Scanning image %u", imageSeq);

        pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

        if (imageSeq == 0)
            firstPam = inpam;
        else
            validateCompatibleImage(&inpam, &firstPam, imageSeq);

        addImageColorsToHash(&inpam, tuplehash, &colorCount);

        pm_message("%u colors so far", colorCount);

        pnm_nextimage(ifP, &eof);
    }
    colorFreqTableP->table =
        pnm_tuplehashtotable(&firstPam, tuplehash, colorCount);
    colorFreqTableP->size = colorCount;

    pnm_destroytuplehash(tuplehash);

    pm_message("%u colors found", colorFreqTableP->size);

    freqPamP->size   = sizeof(*freqPamP);
    freqPamP->len    = PAM_STRUCT_SIZE(tuple_type);
    freqPamP->maxval = firstPam.maxval;
    freqPamP->bytes_per_sample = pnm_bytespersample(freqPamP->maxval);
    freqPamP->depth  = firstPam.depth;
    STRSCPY(freqPamP->tuple_type, firstPam.tuple_type);

    *formatP = firstPam.format;
}



static void
computeColorMapFromInput(FILE *                const ifP,
                         bool                  const allColors,
                         unsigned int          const reqColors,
                         enum MethodForLargest const methodForLargest,
                         enum MethodForRep     const methodForRep,
                         enum MethodForSplit   const methodForSplit,
                         bool                  const wantBvReport,
                         int *                 const formatP,
                         struct pam *          const freqPamP,
                         tupletable2 *         const colormapP) {
/*----------------------------------------------------------------------------
   Produce a colormap containing the best colors to represent the
   image stream in file 'ifP'.  Figure it out using the median cut
   technique.

   The colormap will have 'reqcolors' or fewer colors in it, unless
   'allcolors' is true, in which case it will have all the colors that
   are in the input.

   The colormap has the same maxval as the input.

   Put the colormap in newly allocated storage as a tupletable2
   and return its address as *colormapP.  Return the number of colors in
   it as *colorsP and its maxval as *colormapMaxvalP.

   Return the characteristics of the input file as
   *formatP and *freqPamP.  (This information is not really
   relevant to our colormap mission; just a fringe benefit).
-----------------------------------------------------------------------------*/
    tupletable2 colorFreqTable;
        /* Table of all colors in the image, with the number of pixels of
           each color.
        */

    computeHistogram(ifP, formatP, freqPamP, &colorFreqTable);

    if (allColors) {
        *colormapP = colorFreqTable;
    } else {
        if (colorFreqTable.size <= reqColors) {
            pm_message("Image already has few enough colors (<=%u).  "
                       "Keeping same colors.", reqColors);
            *colormapP = colorFreqTable;
        } else {
            pm_message("choosing %u colors...", reqColors);
            mediancut(colorFreqTable, freqPamP->depth,
                      reqColors, methodForLargest, methodForRep,
                      methodForSplit, wantBvReport, colormapP);
            pnm_freetupletable2(freqPamP, colorFreqTable);
        }
    }
}



static void
sortColormap(tupletable2  const colormap,
             sample       const depth) {
/*----------------------------------------------------------------------------
   Sort the colormap in place, in order of ascending Plane 0 value,
   the Plane 1 value, etc.

   Use insertion sort.
-----------------------------------------------------------------------------*/
    int i;

    pm_message("Sorting %u colors...", colormap.size);

    for (i = 0; i < colormap.size; ++i) {
        int j;
        for (j = i+1; j < colormap.size; ++j) {
            unsigned int plane;
            bool iIsGreater, iIsLess;

            iIsGreater = FALSE; iIsLess = FALSE;
            for (plane = 0;
                 plane < depth && !iIsGreater && !iIsLess;
                 ++plane) {
                if (colormap.table[i]->tuple[plane] >
                    colormap.table[j]->tuple[plane])
                    iIsGreater = TRUE;
                else if (colormap.table[i]->tuple[plane] <
                         colormap.table[j]->tuple[plane])
                    iIsLess = TRUE;
            }
            if (iIsGreater) {
                for (plane = 0; plane < depth; ++plane) {
                    sample const temp = colormap.table[i]->tuple[plane];
                    colormap.table[i]->tuple[plane] =
                        colormap.table[j]->tuple[plane];
                    colormap.table[j]->tuple[plane] = temp;
                }
            }
        }
    }
}



static void
colormapToSquare(struct pam * const pamP,
                 tupletable2  const colormap,
                 tuple ***    const outputRasterP) {
    {
        unsigned int const intsqrt = (int)sqrt((float)colormap.size);
        if (SQR(intsqrt) == colormap.size)
            pamP->width = intsqrt;
        else
            pamP->width = intsqrt + 1;
    }
    {
        unsigned int const intQuotient = colormap.size / pamP->width;
        if (pamP->width * intQuotient == colormap.size)
            pamP->height = intQuotient;
        else
            pamP->height = intQuotient + 1;
    }
    {
        tuple ** outputRaster;
        unsigned int row;
        unsigned int colormapIndex;

        outputRaster = pnm_allocpamarray(pamP);

        colormapIndex = 0;  /* initial value */

        for (row = 0; row < pamP->height; ++row) {
            unsigned int col;
            for (col = 0; col < pamP->width; ++col) {
                unsigned int plane;
                for (plane = 0; plane < pamP->depth; ++plane) {
                    outputRaster[row][col][plane] =
                        colormap.table[colormapIndex]->tuple[plane];
                }
                if (colormapIndex < colormap.size-1)
                    ++colormapIndex;
            }
        }
        *outputRasterP = outputRaster;
    }
}



static void
colormapToSingleRow(struct pam * const pamP,
                    tupletable2  const colormap,
                    tuple ***    const outputRasterP) {

    tuple ** outputRaster;
    unsigned int col;

    pamP->width = colormap.size;
    pamP->height = 1;

    outputRaster = pnm_allocpamarray(pamP);

    for (col = 0; col < pamP->width; ++col) {
        int plane;
        for (plane = 0; plane < pamP->depth; ++plane)
            outputRaster[0][col][plane] = colormap.table[col]->tuple[plane];
    }
    *outputRasterP = outputRaster;
}



static void
colormapToImage(int                const format,
                const struct pam * const colormapPamP,
                tupletable2        const colormap,
                bool               const sort,
                bool               const square,
                struct pam *       const outpamP,
                tuple ***          const outputRasterP) {
/*----------------------------------------------------------------------------
   Create a tuple array and pam structure for an image which includes
   one pixel of each of the colors in the colormap 'colormap'.

   May rearrange the contents of 'colormap'.
-----------------------------------------------------------------------------*/
    outpamP->size             = sizeof(*outpamP);
    outpamP->len              = PAM_STRUCT_SIZE(tuple_type);
    outpamP->format           = format,
    outpamP->plainformat      = FALSE;
    outpamP->depth            = colormapPamP->depth;
    outpamP->maxval           = colormapPamP->maxval;
    outpamP->bytes_per_sample = pnm_bytespersample(outpamP->maxval);
    STRSCPY(outpamP->tuple_type, colormapPamP->tuple_type);

    if (sort)
        sortColormap(colormap, outpamP->depth);

    if (square)
        colormapToSquare(outpamP, colormap, outputRasterP);
    else
        colormapToSingleRow(outpamP, colormap, outputRasterP);
}



int
main(int argc, const char * argv[] ) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    int format;
    struct pam colormapPam;
    struct pam outpam;
    tuple ** colormapRaster;
    tupletable2 colormap;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileNm);

    computeColorMapFromInput(ifP,
                             cmdline.allcolors, cmdline.newcolors,
                             cmdline.methodForLargest,
                             cmdline.methodForRep,
                             cmdline.methodForSplit,
                             !!cmdline.debug,
                             &format, &colormapPam, &colormap);

    pm_close(ifP);

    colormapToImage(format, &colormapPam, colormap,
                    cmdline.sort, cmdline.square, &outpam, &colormapRaster);

    if (cmdline.verbose)
        pm_message("Generating %u x %u image", outpam.width, outpam.height);

    outpam.file = stdout;

    pnm_writepam(&outpam, colormapRaster);

    pnm_freetupletable2(&colormapPam, colormap);

    pnm_freepamarray(colormapRaster, &outpam);

    pm_close(stdout);

    return 0;
}



