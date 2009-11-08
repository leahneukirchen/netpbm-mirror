/* pnmconvol.c - general MxN convolution on a Netpbm image
**
** Major rewriting by Mike Burns
** Copyright (C) 1994, 1995 by Mike Burns (burns@chem.psu.edu)
**
** Copyright (C) 1989, 1991 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

/* A change history is at the bottom */

#include "pm_c_util.h"
#include "pnm.h"
#include "shhopt.h"
#include "mallocvar.h"


struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char *inputFilespec;  /* '-' if stdin */
    const char *kernelFilespec;
    unsigned int nooffset;
};

static void
parseCommandLine(int argc, char ** argv,
                 struct cmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.  

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry *option_def;
        /* Instructions to optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "nooffset",     OPT_FLAG,   NULL,                  
            &cmdlineP->nooffset,       0 );

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    optParseOptions3( &argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 < 1)
        pm_error("Need at least one argument: file specification of the "
                 "convolution kernel image.");

    cmdlineP->kernelFilespec = argv[1];

    if (argc-1 >= 2)
        cmdlineP->inputFilespec = argv[2];
    else
        cmdlineP->inputFilespec = "-";

    if (argc-1 > 2)
        pm_error("Too many arguments.  Only acceptable arguments are: "
                 "convolution file name and input file name");
}


struct convKernel {
    unsigned int const cols;
    unsigned int const rows;
    float ** weight[3];
};

convKernelDestroy(struct convKernel * const convKernelP) {

    unsigned int plane;

    for (plane = 0; plane < 3; ++plane)
        pm_freearray(convKernelP->weight[plane]);

    free(convKernelP);
}

struct convolveType {
    void (*convolver)(struct pam *       const inpamP,
                      struct pam *       const outpamP,
                      const convKernel * const convKernelP);
};



static void
computeKernel(struct pam *         const cpamP
              tuple * const *      const ctuples, 
              bool                 const offsetPgm,
              struct convKernel ** const convKernelP) {
/*----------------------------------------------------------------------------
   Compute the convolution matrix in normalized form from the PGM
   form.  Each element of the output matrix is the actual weight we give an
   input pixel -- i.e. the thing by which we multiple a value from the
   input image.

   'offsetPgm' means the PGM convolution matrix is defined in offset form so
   that it can represent negative values.  E.g. with maxval 100, 50 means
   0, 100 means 50, and 0 means -50.  If 'offsetPgm' is false, 0 means 0
   and there are no negative weights.
-----------------------------------------------------------------------------*/
    double const scale = (offsetPgm ? 2.0 : 1.0) / cpamP->maxval;
    double const offset = offsetPgm ? - 1.0 : 0.0;

    struct convKernel * convKernelP;
    float sum[3];
    unsigned int plane;
    unsigned int row;

    MALLOCVAR_NOFAIL(convKernelP);
    
    convKernelP->weight[PAM_RED_PLANE] =
        pm_allocarray(cpamP->width, cpamP->height,
                      sizeof(convKernelP->weight[PAM_RED_PLANE][0][0]));
    
    for (plane = 0; plane < 3; ++plane)
        sum[plane] = 0.0; /* initial value */
    
    for (row = 0; row < cpamP->height; ++row) {
        unsigned int col;
        for (col = 0; col < cpamP->width; ++col) {
            unsigned int plane;
            for (plane = 0; plane < cpamP->depth; ++plane) {
                sum += convKernelP->weight[plane][row][col] =
                    ctuples[row][col][plane] * scale + offset);
        }
    }

    switch (PNM_FORMAT_TYPE(cpamP->format)) {
    case PPM_TYPE: {
        unsigned int plane;
        bool biased, negative;
        for (plane = 0, biased = false, negative = false;
             plane < cpamP->depth;
             ++plane) {
            if (sum[plane] < 0.9 || sum[plane] > 1.1)
                biased = true;
            if (sum[plane] < 0.0)
                negative = true;
        }
    
        if (biased) {
            pm_message("WARNING - this convolution matrix is biased.  " 
                       "red, green, and blue average weights: %f, %f, %f "
                       "(unbiased would be 1).",
                       sum[PAM_RED_PLANE],
                       sum[PAM_GRN_PLANE],
                       sum[PAM_BLU_PLANE]);

            if (negative)
                pm_message("Maybe you want the -nooffset option?");
        }
    } break;

    default:
        if (sum[0] < 0.9 || sum[0] > 1.1)
            pm_message("WARNING - this convolution matrix is biased.  "
                       "average weight = %f (unbiased would be 1)",
                       sum[0]);
        break;
    }
}



static tuple **
allocRowbuf(struct pam * const pamP,
            unsigned int const height) {

    tuple ** rowbuf;

    MALLOCARRAY(rowbuf, height);

    if (rowbuf == NULL)
        pm_error("Failed to allocate %u-row buffer");
    else {
        unsigned int row;
    
        for (row = 0; row < height; ++row)
            rowbuf[i] = pnm_allocpamrow(pamP);
    }

    return rowbuf;
}



static void
freeRowbuf(tuple **     const rowbuf,
           unsigned int const height) {

    unsigned int row;

    for (row = 0; row < height; ++row)
        pnm_freepamrow(rowbuf[row]);

    free(rowbuf);
}



static void
readAndScaleRow(struct pam * const inpamP,
                tuple *      const inrow,
                sample       const newMaxval,
                unsigned int const newDepth) {

    pnm_readpamrow(inpamP, inrow);

    if (newMaxval != inpamP->maxval)
        pnm_scaletuplerow(inpamP, inrow, inrow, newMaxval);

    if (newDepth == 3 && inpamP->depth == 1)
        pnm_makerowrgb(inpamP, inrow);
}



static void
readInitialRowbuf(struct pam *        const inpamP,
                  struct convKernel * const convKernelP,
                  tuple **            const rowbuf,
                  sample              const outputMaxval,
                  unsigned int        const outputDepth) {
/*----------------------------------------------------------------------------
  Read in one convolution kernel's worth of image, less one row,
  into the row buffer rowbuf[], starting at the beginning of the buffer.
  
  Scale the contents to maxval 'outputMaxval'.
-----------------------------------------------------------------------------*/
    unsigned int row;

    for (row = 0; row < convKernelP->rows - 1; ++row)
        readAndScaleRow(inpamP, rowbuf[row], outputMaxval, outputDepth);
}



static void
writeUnconvolvedTop(struct pam *        const outpamP,
                    struct convKernel * const convKernelP,
                    tuple **            const rowbuf) {
/*----------------------------------------------------------------------------
   Write out the top part that we can't convolve because the convolution
   kernel runs off the top of the image.

   Assume those rows are in the window rowbuf[], with the top row of the
   image as the first row in rowbuf[].
-----------------------------------------------------------------------------*/
    unsigned int row;

    for (row = 0; row < convKernelP->rows/2; ++row)
        pnm_writepamrow(outpamP, rowbuf[row]);
}



static void
writeUnconvolvedBottom(struct pam *        const outpamP,
                       struct convKernel * const convKernelP,
                       tuple **            const circMap) {
/*----------------------------------------------------------------------------
  Write out the bottom part that we can't convolve because the convolution
  kernel runs off the bottom of the image.

  Assume the end of the image is in the row buffer, mapped by 'circMap'
  such that the top of the window is circMap[0].
-----------------------------------------------------------------------------*/
    unsigned int row;

    for (row = convKernelP->rows / 2 + 1; row < convKernelP->rows; ++row)
        pnm_writepamrow(outpamP, circMap[row]);
}



static void
setupCircMap(tuple **     const circMap,
             tuple **     const rowbuf,
             unsigned int const topRowbufRow) {
/*----------------------------------------------------------------------------
  Set up circMap[] to reflect the case that index 'topRowbufRow' of rowbuf[]
  is for the topmost row in the window.
-----------------------------------------------------------------------------*/
    unsigned int row;
    unsigned int i;

    i = 0;

    for (row = topRowbufRow; row < convKernelP->rows; ++i, ++row)
        circMap[i] = rowbuf[row];

    for (row = 0; row < topRowbufRow; ++row, ++i)
        circMap[i] = rowbuf[row];
}



static void
convolveGeneralRowPlane(struct pam *        const pamP,
                        tuple **            const circMap,
                        struct convKernel * const convKernelP,
                        unsigned int        const plane,
                        tuple *             const outputrow) {

    unsigned int const crowso2 = convKernelP->rows / 2;
    unsigned int const ccolso2 = convKernelP->cols / 2;

    unsigned int col;
    
    for (col = 0; col < pamP->width; ++col) {
        if (col < ccolso2 || col >= pamP->width - ccolso2)
            outputrow[col][plane] = circMap[crowso2][col][plane];
        else {
            unsigned int const leftcol = col - ccolso2;
            unsigned int crow;
            float sum;
            sum = 0.0;
            for (crow = 0; crow < crows; ++crow) {
                tuple ** const leftrptr = &circMap[crow][leftcol];
                unsigned int ccol;
                for (ccol = 0; ccol < convKernelP->cols; ++ccol)
                    sum += leftrptr[ccol][plane] *
                        convKernelP->weight[plane][crow][ccol];
            }
            outputrow[col][plane] = MIN(pamP->maxval, MAX(0, sum + 0.5));
        }
    }
}



static void
convolveGeneral(struct pam *        const inpamP,
                struct pam *        const outpamP,
                struct convKernel * const convKernelP) {
/*----------------------------------------------------------------------------
   Do the convolution without taking advantage of any useful redundancy in the
   convolution matrix.
-----------------------------------------------------------------------------*/
    tuple ** rowbuf;
        /* A vertical window of the input image.  It holds as many rows as the
           convolution kernel covers -- the rows we're currently using to
           create output rows.  It is a circular buffer.
        */
    tuple ** circMap;
        /* A map from image row number within window to element of rowbuf[].
           E.g. if rowbuf[] if 5 rows high and rowbuf[2] contains the
           topmost row, then circMap[0] == 2, circMap[1] = 3,
           circMap[4] = 1.  You could calculate the same thing with a mod
           function, but that is sometimes more expensive.
        */
    tuple * outputrow;
        /* The convolved row to be output */
    unsigned int row;

    rowbuf = allocRowbuf(pamP, convKernelP->rows);
    MALLOCARRAY_NOFAIL(circMap, convKernelP->rows);
    outputrow = pnm_allocpamrow(outpamP);

    pnm_writepaminit(outpamP);

    assert(convKernelP->rows > 0);

    readInitialRowbuf(inpamP, convKernelP, rowbuf, outpamP->maxval);

    writeUnconvolvedTop(outpamP, convKernelP, rowbuf);

    /* Now the rest of the image - read in the row at the bottom of the
       window, then convolve and write out the row in the middle of the
       window.
    */
    for (row = convKernelP->rows - 1; row < inpamP->height; ++row) {
        unsigned int const rowbufRow = row % convKernelP->rows;

        setupCircMap(circMap, rowbuf, (row + 1) % convKernelP->rows);

        readAndScaleRow(inpamP, rowbuf[rowbufRow],
                        outpamP->maxval, outpamP->depth);

        for (plane = 0; plane < outpamP->depth; ++plane)
            convolveGeneralRowPlane(outpamP, circMap, convKernelP, plane,
                                    outputrow);

        pnm_writepamrow(outpamP, outputrow);
    }
    writeUnconvolvedBottom(outpamP, convKernelP, circMap);

    freeRowbuf(rowbuf, convKernelP->rows);
}



static sample **
allocSum(unsigned int const depth,
         unsigned int const size) {

    sample ** sum;

    MALLOCARRAY(sum, depth);

    if (!sum)
        pm_error("Could not allocate memory for %u planes of sums", depth);
    else {
        unsigned int plane;

        for (plane = 0; plane < depth; ++plane) {
            MALLOCARRAY(sum[plane], size);
            
            if (!sum[plane])
                pm_error("Could not allocate memory for %u sums", size);
        }
    }
    return sum;
}



static void
freeSum(sample **    const sum,
        unsigned int const depth) {

    unsigned int plane;

    for (plane = 0; plane < depth; ++plane)
        free(sum[plane]);

    free(sum);
}



static void
convolveAndComputeColumnSums(struct pam *        const inpamP,
                             struct pam *        const outpamP,
                             tuple **            const rows,
                             struct convKernel * const convKernelP,
                             unsigned int        conts plane,
                             tuple *             const outputrow,
                             sample **           const convColumnSum) {

    unsigned int const ccolso2 = convKernelP->cols / 2;
    float        const weight  = convKernelP->weight[plane][0][0];

    unsigned int col;
    sample matrixSum;

    for (col = 0; col < inpamP->width; ++col)
        convColumnSum[col] = 0;      /* Initial value */

    for (col = 0, matrixSum = 0; col < inpamP->width; ++col) {
        if (col < ccolso2 || col >= inpamP->width - ccolso2)
            outputrow[col] = circMap[crowso2][col];
        else if (col == ccolso2) {
            unsigned int const leftcol = col - ccolso2;
            unsigned int crow;
            unsigned int ccol;
            for (crow = 0; crow < convKernelP->rows; ++crow) {
                tuple ** const temprptr = &circMap[crow][leftcol];
                unsigned int ccol;
                for (ccol = 0; ccol < convKernelP->cols; ++ccol)
                    convColumnSum[leftcol + ccol] += temprptr[ccol][plane];
            }
            for (ccol = 0; ccol < convKernelP->cols; ++ccol)
                matrixSum += convColumnSum[leftcol + ccol];
            outputrow[col][plane] =
                MIN(outpamP->maxval, MAX(0, matrixSum * weight + 0.5));
        } else {
            /* Column numbers to subtract or add to isum */
            unsigned int const subcol = col - ccolso2 - 1;
            unsigned int const addcol = col + ccolso2;  
            unsigned int crow;
            for (crow = 0; crow < convKernelP->rows; ++crow)
                convColumnSum[addcol] += circMap[crow][addcol][plane];
            matrixSum =
                matrixSum - convColumnSum[subcol] + convColumnSum[addcol];
            outputrow[col][plane] =
                MIN(outpamP->maxval, MAX(0, matrixSum * weight + 0.5));
        }
    }
}



static void
convolveAndComputeColumnSums(struct pam *        const inpamP,
                             struct pam *        const outpamP,
                             tuple **            const rows,
                             struct convKernel * const convKernelP,
                             tuple *             const outputrow,
                             sample **           const convColumnSum) {
/*----------------------------------------------------------------------------
  Convolve the rows in rows[] -- one convolution kernel's worth, where
  rows[0] is the top.  Put the result in outputrow[].

  Along the way, add up the sum of each column and return that as
  convColumnSum[].
-----------------------------------------------------------------------------*/
    unsigned int plane;

    for (plane = 0; plane < outpamP->depth; ++plane) 
        convolveAndComputeColumnSumsPlane(inpamP, outpamP,
                                          rows, convKernelP, plane,
                                          outputrow, convColumnSum[plane]);
}



static void
convolveMeanRowPlane(struct pam *        const pamP,
                     tuple **            const circMap,
                     struct convKernel * const convKernelP,
                     unsigned int        const plane,
                     tuple *             const outputrow,
                     sample *            const convColumnSum) {

    unsigned int const ccolso2 = convKernelP->cols / 2;
    float const weight = convKernelP->weight[plane][0][0];

    unsigned int col;
    sample gisum;

    gisum = 0;
    for (col = 0; col < cols; ++col) {
        if (col < ccolso2 || col >= cols - ccolso2)
            outputrow[col] = circMap[crowso2][col];
        else if (col == ccolso2) {
            unsigned int const leftcol = col - ccolso2;

            unsigned int ccol;

            for (ccol = 0; ccol < convKernelP->cols; ++ccol) {
                sample * const thisColumnSumP =
                    &convColumnSum[leftcol + ccol];
                *thisColumnSumP = *thisColumnSumP
                    - circMap[subrow][ccol][plane]
                    + circMap[addrow][ccol][plane];
                gisum += *thisColumnSumP;
            }
            outputrow[col][plane] =
                MIN(outpamP->maxval, MAX(0, gisum * weight + 0.5));
        } else {
            /* Column numbers to subtract or add to isum */
            unsigned int const subcol = col - ccolso2 - 1;
            unsigned int const addcol = col + ccolso2;  

            convColumnSum[addcol] = convColumnSum[addcol]
                - circMap[subrow][addcol][plane]
                + circMap[addrow][addcol][plane];
            gisum = gisum - convColumnSum[subcol] + convColumnSum[addcol];

            outputrow[col][plane] =
                MIN(outpamP->maxval, MAX(0, gisum * weight + 0.5));
        }
    }
}



static void
convolveMean(struct pam *        const inpamP,
             struct pam *        const outpamP,
             struct convKernel * const convKernelP) {
/*----------------------------------------------------------------------------
  Mean Convolution

  This is for the common case where you just want the target pixel replaced
  with the average value of its neighbors.  This can work much faster than the
  general case because you can reduce the number of floating point operations
  that are required since all the weights are the same.  You will only need to
  multiply by the weight once, not for every pixel in the convolution matrix.

  This algorithm works as follows: At a certain vertical position in the
  image, create sums for each column fragment of the convolution height all
  the way across the image.  Then add those sums across the convolution width
  to obtain the total sum over the convolution area and multiply that sum by
  the weight.  As you move left to right, to calculate the next output pixel,
  take the total sum you just generated, add in the value of the next column
  and subtract the value of the leftmost column.  Multiply that by the weight
  and that's it.  As you move down a row, calculate new column sums by using
  previous sum for that column and adding in pixel on current row and
  subtracting pixel in top row.

  We assume the convolution kernel is uniform -- same weights everywhere.

  We assume the output is PGM and the input is PGM or PBM.
-----------------------------------------------------------------------------*/
    unsigned int const windowHeight = convKernelP->rows + 1;
        /* The height of the window we keep in the row buffer.  The buffer
           contains the rows covered by the convolution kernel, plus the row
           immediately above that.  The latter is there because to compute
           the sliding mean, we need to subtract off the row that the
           convolution kernel just slid past.
        */

    tuple ** rowbuf;
        /* Same as in convolvePgmGeneral */
    tuple ** circMap;
        /* Same as in convolvePgmGeneral */
    tuple * outputrow;
        /* Same as in convolvePgmGeneral */
    unsigned int row;
        /* Row number of next row to read in from the file */
    sample matrixSum;
        /* Sum of all pixels in current convolution window */
    sample ** convColumnSum;  /* Malloc'd */
        /* convColumnSum[plane][col] is the sum of Plane 'plane' of all the
           pixels in the Column 'col' of the image within the current vertical
           convolution window.  I.e. if our convolution kernel is 5 rows high
           and we're now looking at Rows 10-15, convColumn[0][3] is the sum of
           Plane 0 of Column 3, Rows 10-15.
        */
    unsigned int col;

    rowbuf = allocRowbuf(pamP, windowHeight);
    MALLOCARRAY_NOFAIL(circMap, windowHeight);
    outputrow = pnm_allocpamrow(outpamP);

    convColumnSum = allocSum(outpamP->depth, outpamP->width);

    pnm_writepaminit(outpamP);

    readInitialRowbuf(inpamP, convKernelP, rowbuf, outpamP->maxval);

    writeUnconvolvedTop(outpamP, convKernelP, rowbuf);

    /* Add a row to the window to have enough to convolve */
    readAndScaleRow(inpamP, rowbuf[convKernelP->rows - 1],
                    outpamP->maxval, outpamP->depth);

    setupCircMap(circMap, rowbuf, 0);

    /* Convolve the first window the long way */
    convolveAndComputeColumnSums(inpamP, outpamP, circMap, convKernelP,
                                 outputrow, convColumnSum);

    /* Write that first convolved row */
    pnm_writepamrow(outpamP, outputrow);

    /* For all subsequent rows do it this way as the columnsums have been
       generated.  Now we can use them to reduce further calculations.
    */
    for (row = convKernelP->rows; row < inpamP->height; ++row) {
        unsigned int const subrow = 0;
            /* Row just above convolution window -- what we subtract from
               running sum
            */
        unsigned int const addrow = 1 + (convKernelP->rows - 1);
            /* Bottom row of convolution window: What we add to running sum */

        readAndScaleRow(inpamP, rowbuf[row % windowHeight],
                        outpamP->maxval, outpamP->depth);

        /* Remember the window is one row higher than the convolution
           kernel.  The top row in the window is not part of this convolution.
        */

        setupCircMap(circMap, rowbuf, (row + 1) % windowHeight);

        for (plane = 0; plane < outpamP->depth; ++plane)
            convolveMeanRowPlane(outpamP, circMap, convKernelP, plane,
                                 outputrow, convColumnSum[plane]);

        pnm_writepamrow(outpamP, outputrow);
    }
    writeUnconvolvedBottom(outpamP, convKernelP, circMap);

    freeColumnSum(convColumnSum, outpamP->depth);
    freeRowbuf(rowbuf, windowHeight);
}



static void
convolveHorizontalRowPlane(struct pam *        const outpamP,
                           tuple **            const circMap,
                           struct convKernel * const convKernelP,
                           unsigned int        const plane,
                           tuple *             const outputrow,
                           sample *            const sumCircMap) {

    unsigned int col;

    for (col = 0; col < cols; ++col) {
        if (col < ccolso2 || col >= cols - ccolso2)
            outputrow[col][0] = circMap[crowso2][col][0];
        else if (col == ccolso2) {
            unsigned int const leftcol = col - ccolso2;
            
            float matrixSum;
            unsigned int crow;

            for (crow = 0, matrixSum = 0.0; crow < crows; ++crow) {
                tuple ** const temprptr = circMap[crow] + leftcol;

                unsigned int ccol;
                
                sumCircMap[crow][leftcol] = 0L;
                for (ccol = 0; ccol < convKernelP->cols; ++ccol)
                    sumCircMap[crow][leftcol] += temprptr[ccol][0];
                matrixSum += sumCircMap[crow][leftcol] *
                    convKernelP->weight[crow][0][0];
            }
            outputrow[col][0] =
                MIN(outpamP->maxval, MAX(0, matrixSum + 0.5));
        } else {
            unsigned int const leftcol = col - ccolso2;
            unsigned int const subcol  = col - ccolso2 - 1;
            usnigned int const addcol  = col + ccolso2;

            float matrixSum;
            unsigned int crow;

            matrixSum = 0.0;

            for (crow = 0; crow < crows; ++crow) {
                sumCircMap[crow][leftcol] = sumCircMap[crow][subcol]
                    - circMap[crow][subcol][0]
                    + circMap[crow][addcol][0];
                matrixSum += sumCircMap[crow][leftcol] *
                    convKernelP->weight[crow][0][0];
            }
            outputrow[col][0] =
                MIN(outpamP->maxval, MAX(0, matrixSum + 0.5));
        }
    }
}



static void
convolveHorizontal(struct pam *        const inpamP,
                   struct pam *        const outpamP,
                   struct convKernel * const convKernelP) {
/*----------------------------------------------------------------------------
  Horizontal Convolution

  Similar idea to using columnsums of the Mean and Vertical convolution,
  but uses temporary sums of row values.  Need to multiply by weights crows
  number of times.  Each time a new line is started, must recalculate the
  initials rowsums for the newest row only.  Uses queue to still access
  previous row sums.
-----------------------------------------------------------------------------*/
    unsigned int const ccolso2 = convKernelP->cols / 2;
    unsigned int const crowso2 = convKernelP->rows / 2;
    unsigned int const windowHeight = convKernelP->rows + 1;
        /* Same as in convolvePgmMean */

    tuple ** rowbuf;
        /* Same as in convolvePgmGeneral */
    tuple ** circMap;
        /* Same as in convolvePgmGeneral */
    tuple * outputrow;
        /* Same as in convolvePgmGeneral */
    unsigned int row;
        /* Row number of next row to read in from the file */
    unsigned int plane;
    sample ** convRowSum;  /* Malloc'd */
    sample * sumCircMap;  /* Malloc'd */
    unsigned int plane;

    rowbuf = allocRowbuf(pamP, windowHeight);
    MALLOCARRAY_NOFAIL(circMap, windowHeight);
    outputrow = pnm_allocpamrow(outpamP);

    convRowSum = allocSum(outpamP->depth, windowHeight);
    MALLOCARRAY_NOFAIL(sumCircMap, windowHeight);

    pnm_writepaminit(outpamP);

    readInitialRowbuf(inpamP, convKernelP, rowbuf, outpamP->maxval);

    writeUnconvolvedTop(outpamP, convKernelP, rowbuf);

    /* Add a row to the window to have enough to convolve */
    readAndScaleRow(inpamP, rowbuf[convKernelP->rows - 1],
                    outpamP->maxval, outpamP->depth);

    setupCircMap(circMap, rowbuf, 0);

    for (plane = 0; plane < outpamP->depth; ++plane) {
        unsigned int crow;

        for (crow = 0; crow < crows; ++crow)
            sumCircMap[crow] = convRowSum[plane][crow];
 
        convolveHorizontalRowPlane(outpamP, circMap, convKernelP, plane,
                                   outputrow, sumCircMap);
    }
    pnm_writepamrow(outpamP, outputrow);

    {
        /* For all subsequent rows */

        unsigned int const addrow = crows - 1;
        unsigned int const windowHeight = crows + 1;

        for (row = convKernelP->rows ; row < rows; ++row) {
            unsigned int const toprow = (row + 2) % windowHeight;

            unsigned int col;

            readAndScaleRow(inpamP, rowbuf[row % windowHeight],
                            outpamP->maxval, outpamP->depth);
            
            i = 0;
            for (irow = toprow; irow < windowHeight; ++i, ++irow) {
                circMap[i] = rowbuf[irow];
                sumCircMap[i] = convRowSum[plane][irow];
            }
            for (irow = 0; irow < toprow; ++irow, ++i) {
                circMap[i] = rowbuf[irow];
                sumCircMap[i] = convRowSum[plane][irow];
            }
            
            for (col = 0; col < cols; ++col) {
                if (col < ccolso2 || col >= cols - ccolso2)
                    outputrow[col] = circMap[crowso2][col];
                else if (col == ccolso2) {
                    unsigned int const leftcol = col - ccolso2;

                    float matrixSum;

                    {
                        unsigned int ccol;
                        sumCircMap[addrow][leftcol] = 0L;
                        for (ccol = 0; ccol < convKernelP->cols; ++ccol)
                            sumCircMap[addrow][leftcol] += 
                                circMap[addrow][leftcol + ccol][0];
                    }
                    {
                        unsigned int crow;
                        for (crow = 0, matrixSum = 0.0; crow < crows; ++crow)
                            matrixSum += sumCircMap[crow][leftcol] *
                                convKernelP->weight[crow][0][0];
                    }
                    outputrow[col][0] =
                        MIN(outpamP->maxval, MAX(0, matrixSum + 0.5));
                } else {
                    unsigned int const leftcol = col - ccolso2;
                    unsigned int const subcol  = col - ccolso2 - 1;
                    unsigned int const addcol  = col + ccolso2;  

                    float matrixSum;
                    unsigned int crow;

                    sumCircMap[addrow][leftcol] = sumCircMap[addrow][subcol]
                        - circMap[addrow][subcol][0]
                        + circMap[addrow][addcol][0];

                    for (crow = 0, matrixSum = 0.0; crow < crows; ++crow)
                        matrixSum += sumCircMap[crow][leftcol] *
                            convKernelP->weight[crow][0][0];

                    outputrow[col][0] =
                        MIN(outpamP->maxval, MAX(0, matrixSum + 0.5));
                }
            }
            pnm_writepamrow(outpamP, outputrow);
        }
    }
    writeUnconvolvedBottom(outpamP, convKernelP, circMap);

    freeRowbuf(rowbuf, windowHeight);
}



static void
convolvePgmVertical(struct pam *        const inpamP,
                    struct pam *        const outpamP,
                    struct convKernel * const convKernelP) {

    /* Uses column sums as in mean convolution, above */

    unsigned int const ccolso2 = convKernelP->cols / 2;
    float const weight = convKernelP->weight[0][0][0];
    unsigned int const windowHeight = convKernelP->rows + 1;
        /* The height of the window we keep in the row buffer.  The buffer
           contains the rows covered by the convolution kernel, plus the row
           immediately above that.  The latter is there because to compute
           the sliding mean, we need to subtract off the row that the
           convolution kernel just slid past.
        */

    tuple ** rowbuf;
        /* Same as in convolvePgmGeneral */
    tuple ** circMap;
        /* Same as in convolvePgmGeneral */
    tuple * outputrow;
        /* Same as in convolvePgmGeneral */
    unsigned int row;
        /* Row number of next row to read in from the file */
    sample * convColumnSum;  /* Malloc'd */
        /* convColumnSum[col] is the sum of all the pixels in the Column 'col'
           of the image within the current vertical convolution window.
           I.e. if our convolution kernel is 5 rows high and we're now looking
           at Rows 10-15, convColumn[3] is the sum of Column 3, Rows 10-15.
        */
    sample matrixSum;
    unsigned int col;

    rowbuf = allocRowbuf(pamP, windowHeight);
    MALLOCARRAY_NOFAIL(circMap, windowHeight);
    outputrow = pnm_allocpamrow(outpamP);

    MALLOCARRAY_NOFAIL(convColumnSum, inpamP->width);

    pnm_writepaminit(outpamP);

    readInitialRowbuf(inpamP, convKernelP, rowbuf, outpamP->maxval);

    writeUnconvolvedTop(outpamP, convKernelP, rowbuf);

    setupCircMap(circMap, rowbuf, 0);

    /* Convolve the first window the long way */
    convolveAndComputeColumnSums(inpamP, outpamP, circMap, convKernelP,
                                 outputrow, convColumnSum);

    /* Write that first convolved row */
    pnm_writepamrow(outpamP, outputrow);

    for (row = convKernelP->rows; row < inpamP->height; ++row) {
        unsigned int const subrow = 0;
            /* Row just above convolution window -- what we subtract from
               running sum
            */
        unsigned int const addrow = 1 + (convKernelP->rows - 1);
            /* Bottom row of convolution window: What we add to running sum */

        unsigned int col;

        readAndScaleRow(inpamP, rowbuf[row % windowHeight],
                        outpamP->maxval, outpamP->depth);

        /* Remember the window is one row higher than the convolution
           kernel.  The top row in the window is not part of this convolution.
        */

        setupCircMap(circMap, rowbuf, (row + 1) % windowHeight);

        for (col = 0; col < cols; ++col) {
            if (col < ccolso2 || col >= cols - ccolso2)
                outputrow[col] = circMap[crowso2][col];
            else if (col == ccolso2) {
                unsigned int const leftcol = col - ccolso2;

                float matrixSum;
                unsigned int crow;
                unsigned int ccol;

                for (crow = 0; crow < convKernelP->rows; ++crow) {
                    unsigned int ccol;

                    for (ccol = 0; ccol < convKernelP->cols; ++ccol)
                        convColumnSum[leftcol + ccol] +=
                            circMap[crow][ccol][0];
                }
                for (ccol = 0, matrixSum = 0.0;
                     ccol < convKernelP->cols;
                     ++ccol) {
                    matrixSum += convColumnSum[leftcol + ccol] *
                        convKernelP->weight[0][ccol][0];
                }
                outputrow[col][0] =
                    MIN(outpamP->maxval, MAX(0, matrixSum + 0.5));
            } else {
                unsigned int const leftcol = col - ccolso2;
                unsigned int const addcol  = col + ccolso2;

                float matrixSum;
                unsigned int ccol;

                convColumnSum[addcol] = convColumnSum[addcol]
                    - circMap[subrow][addcol][0]
                    + circMap[addrow][addcol][0];

                for (ccol = 0, matrixSum = 0.0;
                     ccol < convKernelP->cols;
                     ++ccol) {
                    matrixSum += convColumnSum[leftcol + ccol] *
                        convKernelP->weight[0][ccol][0];
                }
                outputrow[col][0] =
                    MIN(outpamP->maxval, MAX(0, matrixSum + 0.5));
            }
        }
        pnm_writepamrow(outpamP, outputrow);
    }
    writeUnconvolvedBottom(outpamP, convKernelP, circMap);

    freeRowbuf(rowbuf, windowHeight);
}



/* PPM General Convolution Algorithm
**
** No redundancy in convolution matrix.  Just use brute force.
** See convolvePgmGeneral() for more details.
*/

static void
ppm_general_convolve(const float ** const rweights,
                     const float ** const gweights,
                     const float ** const bweights) {
    int ccol, col;
    xel** xelbuf;
    xel* outputrow;
    xelval r, g, b;
    int row, crow;
    float rsum, gsum, bsum;
    xel **rowptr, *temprptr;
    int toprow, temprow;
    int i, irow;
    int leftcol;
    long temprsum, tempgsum, tempbsum;

    /* Allocate space for one convolution-matrix's worth of rows, plus
    ** a row output buffer. */
    xelbuf = pnm_allocarray( cols, crows );
    outputrow = pnm_allocrow( cols );

    /* Allocate array of pointers to xelbuf */
    rowptr = (xel **) pnm_allocarray( 1, crows );

    pnm_writepnminit( stdout, cols, rows, maxval, newformat, 0 );

    /* Read in one convolution-matrix's worth of image, less one row. */
    for ( row = 0; row < crows - 1; ++row )
    {
    pnm_readpnmrow( ifp, xelbuf[row], cols, maxval, format );
    if ( PNM_FORMAT_TYPE(format) != newformat )
        pnm_promoteformatrow(
        xelbuf[row], cols, maxval, format, maxval, newformat );
    /* Write out just the part we're not going to convolve. */
    if ( row < crowso2 )
        pnm_writepnmrow( stdout, xelbuf[row], cols, maxval, newformat, 0 );
    }

    /* Now the rest of the image - read in the row at the end of
    ** xelbuf, and convolve and write out the row in the middle.
    */
    for ( ; row < rows; ++row )
    {
    toprow = row + 1;
    temprow = row % crows;
    pnm_readpnmrow( ifp, xelbuf[temprow], cols, maxval, format );
    if ( PNM_FORMAT_TYPE(format) != newformat )
        pnm_promoteformatrow(
        xelbuf[temprow], cols, maxval, format, maxval, newformat );

    /* Arrange rowptr to eliminate the use of mod function to determine
    ** which row of xelbuf is 0...crows.  Mod function can be very costly.
    */
    temprow = toprow % crows;
    i = 0;
    for (irow = temprow; irow < crows; ++i, ++irow)
        rowptr[i] = xelbuf[irow];
    for (irow = 0; irow < temprow; ++irow, ++i)
        rowptr[i] = xelbuf[irow];

    for ( col = 0; col < cols; ++col )
        {
        if ( col < ccolso2 || col >= cols - ccolso2 )
        outputrow[col] = rowptr[crowso2][col];
        else
        {
        leftcol = col - ccolso2;
        rsum = gsum = bsum = 0.0;
        for ( crow = 0; crow < crows; ++crow )
            {
            temprptr = rowptr[crow] + leftcol;
            for ( ccol = 0; ccol < ccols; ++ccol )
            {
            rsum += PPM_GETR( *(temprptr + ccol) )
                * rweights[crow][ccol];
            gsum += PPM_GETG( *(temprptr + ccol) )
                * gweights[crow][ccol];
            bsum += PPM_GETB( *(temprptr + ccol) )
                * bweights[crow][ccol];
            }
            }
            temprsum = rsum + 0.5;
            tempgsum = gsum + 0.5;
            tempbsum = bsum + 0.5;
            CHECK_RED;
            CHECK_GREEN;
            CHECK_BLUE;
            PPM_ASSIGN( outputrow[col], r, g, b );
        }
        }
    pnm_writepnmrow( stdout, outputrow, cols, maxval, newformat, 0 );
    }

    /* Now write out the remaining unconvolved rows in xelbuf. */
    for ( irow = crowso2 + 1; irow < crows; ++irow )
    pnm_writepnmrow(
            stdout, rowptr[irow], cols, maxval, newformat, 0 );

    }


/* PPM Mean Convolution
**
** Same as pgm_mean_convolve() but for PPM.
**
*/

static void
ppm_mean_convolve(const float ** const rweights,
                  const float ** const gweights,
                  const float ** const bweights) {
    /* All weights of a single color are the same so just grab any one
       of them.  
    */
    float const rmeanweight = rweights[0][0];
    float const gmeanweight = gweights[0][0];
    float const bmeanweight = bweights[0][0];

    int ccol, col;
    xel** xelbuf;
    xel* outputrow;
    xelval r, g, b;
    int row, crow;
    xel **rowptr, *temprptr;
    int leftcol;
    int i, irow;
    int toprow, temprow;
    int subrow, addrow;
    int subcol, addcol;
    long risum, gisum, bisum;
    long temprsum, tempgsum, tempbsum;
    int tempcol, crowsp1;
    long *rcolumnsum, *gcolumnsum, *bcolumnsum;



    /* Allocate space for one convolution-matrix's worth of rows, plus
    ** a row output buffer.  MEAN uses an extra row. */
    xelbuf = pnm_allocarray( cols, crows + 1 );
    outputrow = pnm_allocrow( cols );

    /* Allocate array of pointers to xelbuf. MEAN uses an extra row. */
    rowptr = (xel **) pnm_allocarray( 1, crows + 1);

    /* Allocate space for intermediate column sums */
    rcolumnsum = (long *) pm_allocrow( cols, sizeof(long) );
    gcolumnsum = (long *) pm_allocrow( cols, sizeof(long) );
    bcolumnsum = (long *) pm_allocrow( cols, sizeof(long) );
    for ( col = 0; col < cols; ++col )
    {
    rcolumnsum[col] = 0L;
    gcolumnsum[col] = 0L;
    bcolumnsum[col] = 0L;
    }

    pnm_writepnminit( stdout, cols, rows, maxval, newformat, 0 );

    /* Read in one convolution-matrix's worth of image, less one row. */
    for ( row = 0; row < crows - 1; ++row )
    {
    pnm_readpnmrow( ifp, xelbuf[row], cols, maxval, format );
    if ( PNM_FORMAT_TYPE(format) != newformat )
        pnm_promoteformatrow(
        xelbuf[row], cols, maxval, format, maxval, newformat );
    /* Write out just the part we're not going to convolve. */
    if ( row < crowso2 )
        pnm_writepnmrow( stdout, xelbuf[row], cols, maxval, newformat, 0 );
    }

    /* Do first real row only */
    subrow = crows;
    addrow = crows - 1;
    toprow = row + 1;
    temprow = row % crows;
    pnm_readpnmrow( ifp, xelbuf[temprow], cols, maxval, format );
    if ( PNM_FORMAT_TYPE(format) != newformat )
    pnm_promoteformatrow(
        xelbuf[temprow], cols, maxval, format, maxval, newformat );

    temprow = toprow % crows;
    i = 0;
    for (irow = temprow; irow < crows; ++i, ++irow)
    rowptr[i] = xelbuf[irow];
    for (irow = 0; irow < temprow; ++irow, ++i)
    rowptr[i] = xelbuf[irow];

    risum = 0L;
    gisum = 0L;
    bisum = 0L;
    for ( col = 0; col < cols; ++col )
    {
    if ( col < ccolso2 || col >= cols - ccolso2 )
        outputrow[col] = rowptr[crowso2][col];
    else if ( col == ccolso2 )
        {
        leftcol = col - ccolso2;
        for ( crow = 0; crow < crows; ++crow )
        {
        temprptr = rowptr[crow] + leftcol;
        for ( ccol = 0; ccol < ccols; ++ccol )
            {
            rcolumnsum[leftcol + ccol] += 
            PPM_GETR( *(temprptr + ccol) );
            gcolumnsum[leftcol + ccol] += 
            PPM_GETG( *(temprptr + ccol) );
            bcolumnsum[leftcol + ccol] += 
            PPM_GETB( *(temprptr + ccol) );
            }
        }
        for ( ccol = 0; ccol < ccols; ++ccol)
        {
        risum += rcolumnsum[leftcol + ccol];
        gisum += gcolumnsum[leftcol + ccol];
        bisum += bcolumnsum[leftcol + ccol];
        }
        temprsum = (float) risum * rmeanweight + 0.5;
        tempgsum = (float) gisum * gmeanweight + 0.5;
        tempbsum = (float) bisum * bmeanweight + 0.5;
        CHECK_RED;
        CHECK_GREEN;
        CHECK_BLUE;
        PPM_ASSIGN( outputrow[col], r, g, b );
        }
    else
        {
        /* Column numbers to subtract or add to isum */
        subcol = col - ccolso2 - 1;
        addcol = col + ccolso2;  
        for ( crow = 0; crow < crows; ++crow )
        {
        rcolumnsum[addcol] += PPM_GETR( rowptr[crow][addcol] );
        gcolumnsum[addcol] += PPM_GETG( rowptr[crow][addcol] );
        bcolumnsum[addcol] += PPM_GETB( rowptr[crow][addcol] );
        }
        risum = risum - rcolumnsum[subcol] + rcolumnsum[addcol];
        gisum = gisum - gcolumnsum[subcol] + gcolumnsum[addcol];
        bisum = bisum - bcolumnsum[subcol] + bcolumnsum[addcol];
        temprsum = (float) risum * rmeanweight + 0.5;
        tempgsum = (float) gisum * gmeanweight + 0.5;
        tempbsum = (float) bisum * bmeanweight + 0.5;
        CHECK_RED;
        CHECK_GREEN;
        CHECK_BLUE;
        PPM_ASSIGN( outputrow[col], r, g, b );
        }
    }
    pnm_writepnmrow( stdout, outputrow, cols, maxval, newformat, 0 );

    ++row;
    /* For all subsequent rows do it this way as the columnsums have been
    ** generated.  Now we can use them to reduce further calculations.
    */
    crowsp1 = crows + 1;
    for ( ; row < rows; ++row )
    {
    toprow = row + 1;
    temprow = row % (crows + 1);
    pnm_readpnmrow( ifp, xelbuf[temprow], cols, maxval, format );
    if ( PNM_FORMAT_TYPE(format) != newformat )
        pnm_promoteformatrow(
        xelbuf[temprow], cols, maxval, format, maxval, newformat );

    /* This rearrangement using crows+1 rowptrs and xelbufs will cause
    ** rowptr[0..crows-1] to always hold active xelbufs and for 
    ** rowptr[crows] to always hold the oldest (top most) xelbuf.
    */
    temprow = (toprow + 1) % crowsp1;
    i = 0;
    for (irow = temprow; irow < crowsp1; ++i, ++irow)
        rowptr[i] = xelbuf[irow];
    for (irow = 0; irow < temprow; ++irow, ++i)
        rowptr[i] = xelbuf[irow];

    risum = 0L;
    gisum = 0L;
    bisum = 0L;
    for ( col = 0; col < cols; ++col )
        {
        if ( col < ccolso2 || col >= cols - ccolso2 )
        outputrow[col] = rowptr[crowso2][col];
        else if ( col == ccolso2 )
        {
        leftcol = col - ccolso2;
        for ( ccol = 0; ccol < ccols; ++ccol )
            {
            tempcol = leftcol + ccol;
            rcolumnsum[tempcol] = rcolumnsum[tempcol]
            - PPM_GETR( rowptr[subrow][ccol] )
            + PPM_GETR( rowptr[addrow][ccol] );
            risum += rcolumnsum[tempcol];
            gcolumnsum[tempcol] = gcolumnsum[tempcol]
            - PPM_GETG( rowptr[subrow][ccol] )
            + PPM_GETG( rowptr[addrow][ccol] );
            gisum += gcolumnsum[tempcol];
            bcolumnsum[tempcol] = bcolumnsum[tempcol]
            - PPM_GETB( rowptr[subrow][ccol] )
            + PPM_GETB( rowptr[addrow][ccol] );
            bisum += bcolumnsum[tempcol];
            }
        temprsum = (float) risum * rmeanweight + 0.5;
        tempgsum = (float) gisum * gmeanweight + 0.5;
        tempbsum = (float) bisum * bmeanweight + 0.5;
        CHECK_RED;
        CHECK_GREEN;
        CHECK_BLUE;
        PPM_ASSIGN( outputrow[col], r, g, b );
        }
        else
        {
        /* Column numbers to subtract or add to isum */
        subcol = col - ccolso2 - 1;
        addcol = col + ccolso2;  
        rcolumnsum[addcol] = rcolumnsum[addcol]
            - PPM_GETR( rowptr[subrow][addcol] )
            + PPM_GETR( rowptr[addrow][addcol] );
        risum = risum - rcolumnsum[subcol] + rcolumnsum[addcol];
        gcolumnsum[addcol] = gcolumnsum[addcol]
            - PPM_GETG( rowptr[subrow][addcol] )
            + PPM_GETG( rowptr[addrow][addcol] );
        gisum = gisum - gcolumnsum[subcol] + gcolumnsum[addcol];
        bcolumnsum[addcol] = bcolumnsum[addcol]
            - PPM_GETB( rowptr[subrow][addcol] )
            + PPM_GETB( rowptr[addrow][addcol] );
        bisum = bisum - bcolumnsum[subcol] + bcolumnsum[addcol];
        temprsum = (float) risum * rmeanweight + 0.5;
        tempgsum = (float) gisum * gmeanweight + 0.5;
        tempbsum = (float) bisum * bmeanweight + 0.5;
        CHECK_RED;
        CHECK_GREEN;
        CHECK_BLUE;
        PPM_ASSIGN( outputrow[col], r, g, b );
        }
        }
    pnm_writepnmrow( stdout, outputrow, cols, maxval, newformat, 0 );
    }

    /* Now write out the remaining unconvolved rows in xelbuf. */
    for ( irow = crowso2 + 1; irow < crows; ++irow )
    pnm_writepnmrow(
            stdout, rowptr[irow], cols, maxval, newformat, 0 );

    }


/* PPM Horizontal Convolution
**
** Same as pgm_horizontal_convolve()
**
**/

static void
ppm_horizontal_convolve(const float ** const rweights,
                        const float ** const gweights,
                        const float ** const bweights) {
    int ccol, col;
    xel** xelbuf;
    xel* outputrow;
    xelval r, g, b;
    int row, crow;
    xel **rowptr, *temprptr;
    int leftcol;
    int i, irow;
    int temprow;
    int subcol, addcol;
    float rsum, gsum, bsum;
    int addrow, subrow;
    long **rrowsum, **rrowsumptr;
    long **growsum, **growsumptr;
    long **browsum, **browsumptr;
    int crowsp1;
    long temprsum, tempgsum, tempbsum;

    /* Allocate space for one convolution-matrix's worth of rows, plus
    ** a row output buffer. */
    xelbuf = pnm_allocarray( cols, crows + 1 );
    outputrow = pnm_allocrow( cols );

    /* Allocate array of pointers to xelbuf */
    rowptr = (xel **) pnm_allocarray( 1, crows + 1);

    /* Allocate intermediate row sums.  HORIZONTAL uses an extra row */
    rrowsum = (long **) pm_allocarray( cols, crows + 1, sizeof(long) );
    rrowsumptr = (long **) pnm_allocarray( 1, crows + 1);
    growsum = (long **) pm_allocarray( cols, crows + 1, sizeof(long) );
    growsumptr = (long **) pnm_allocarray( 1, crows + 1);
    browsum = (long **) pm_allocarray( cols, crows + 1, sizeof(long) );
    browsumptr = (long **) pnm_allocarray( 1, crows + 1);

    pnm_writepnminit( stdout, cols, rows, maxval, newformat, 0 );

    /* Read in one convolution-matrix's worth of image, less one row. */
    for ( row = 0; row < crows - 1; ++row )
    {
    pnm_readpnmrow( ifp, xelbuf[row], cols, maxval, format );
    if ( PNM_FORMAT_TYPE(format) != newformat )
        pnm_promoteformatrow(
        xelbuf[row], cols, maxval, format, maxval, newformat );
    /* Write out just the part we're not going to convolve. */
    if ( row < crowso2 )
        pnm_writepnmrow( stdout, xelbuf[row], cols, maxval, newformat, 0 );
    }

    /* First row only */
    temprow = row % crows;
    pnm_readpnmrow( ifp, xelbuf[temprow], cols, maxval, format );
    if ( PNM_FORMAT_TYPE(format) != newformat )
    pnm_promoteformatrow(
        xelbuf[temprow], cols, maxval, format, maxval, newformat );

    temprow = (row + 1) % crows;
    i = 0;
    for (irow = temprow; irow < crows; ++i, ++irow)
    rowptr[i] = xelbuf[irow];
    for (irow = 0; irow < temprow; ++irow, ++i)
    rowptr[i] = xelbuf[irow];

    for ( crow = 0; crow < crows; ++crow )
    {
    rrowsumptr[crow] = rrowsum[crow];
    growsumptr[crow] = growsum[crow];
    browsumptr[crow] = browsum[crow];
    }
 
    for ( col = 0; col < cols; ++col )
    {
    if ( col < ccolso2 || col >= cols - ccolso2 )
        outputrow[col] = rowptr[crowso2][col];
    else if ( col == ccolso2 )
        {
        leftcol = col - ccolso2;
        rsum = 0.0;
        gsum = 0.0;
        bsum = 0.0;
        for ( crow = 0; crow < crows; ++crow )
        {
        temprptr = rowptr[crow] + leftcol;
        rrowsumptr[crow][leftcol] = 0L;
        growsumptr[crow][leftcol] = 0L;
        browsumptr[crow][leftcol] = 0L;
        for ( ccol = 0; ccol < ccols; ++ccol )
            {
            rrowsumptr[crow][leftcol] += 
                PPM_GETR( *(temprptr + ccol) );
            growsumptr[crow][leftcol] += 
                PPM_GETG( *(temprptr + ccol) );
            browsumptr[crow][leftcol] += 
                PPM_GETB( *(temprptr + ccol) );
            }
        rsum += rrowsumptr[crow][leftcol] * rweights[crow][0];
        gsum += growsumptr[crow][leftcol] * gweights[crow][0];
        bsum += browsumptr[crow][leftcol] * bweights[crow][0];
        }
        temprsum = rsum + 0.5;
        tempgsum = gsum + 0.5;
        tempbsum = bsum + 0.5;
        CHECK_RED;
        CHECK_GREEN;
        CHECK_BLUE;
        PPM_ASSIGN( outputrow[col], r, g, b );
        }
    else
        {
        rsum = 0.0;
        gsum = 0.0;
        bsum = 0.0;
        leftcol = col - ccolso2;
        subcol = col - ccolso2 - 1;
        addcol = col + ccolso2;
        for ( crow = 0; crow < crows; ++crow )
        {
        rrowsumptr[crow][leftcol] = rrowsumptr[crow][subcol]
            - PPM_GETR( rowptr[crow][subcol] )
            + PPM_GETR( rowptr[crow][addcol] );
        rsum += rrowsumptr[crow][leftcol] * rweights[crow][0];
        growsumptr[crow][leftcol] = growsumptr[crow][subcol]
            - PPM_GETG( rowptr[crow][subcol] )
            + PPM_GETG( rowptr[crow][addcol] );
        gsum += growsumptr[crow][leftcol] * gweights[crow][0];
        browsumptr[crow][leftcol] = browsumptr[crow][subcol]
            - PPM_GETB( rowptr[crow][subcol] )
            + PPM_GETB( rowptr[crow][addcol] );
        bsum += browsumptr[crow][leftcol] * bweights[crow][0];
        }
        temprsum = rsum + 0.5;
        tempgsum = gsum + 0.5;
        tempbsum = bsum + 0.5;
        CHECK_RED;
        CHECK_GREEN;
        CHECK_BLUE;
        PPM_ASSIGN( outputrow[col], r, g, b );
        }
        }
    pnm_writepnmrow( stdout, outputrow, cols, maxval, newformat, 0 );


    /* For all subsequent rows */

    subrow = crows;
    addrow = crows - 1;
    crowsp1 = crows + 1;
    ++row;
    for ( ; row < rows; ++row )
    {
    temprow = row % crowsp1;
    pnm_readpnmrow( ifp, xelbuf[temprow], cols, maxval, format );
    if ( PNM_FORMAT_TYPE(format) != newformat )
        pnm_promoteformatrow(
        xelbuf[temprow], cols, maxval, format, maxval, newformat );

    temprow = (row + 2) % crowsp1;
    i = 0;
    for (irow = temprow; irow < crowsp1; ++i, ++irow)
        {
        rowptr[i] = xelbuf[irow];
        rrowsumptr[i] = rrowsum[irow];
        growsumptr[i] = growsum[irow];
        browsumptr[i] = browsum[irow];
        }
    for (irow = 0; irow < temprow; ++irow, ++i)
        {
        rowptr[i] = xelbuf[irow];
        rrowsumptr[i] = rrowsum[irow];
        growsumptr[i] = growsum[irow];
        browsumptr[i] = browsum[irow];
        }

    for ( col = 0; col < cols; ++col )
        {
        if ( col < ccolso2 || col >= cols - ccolso2 )
        outputrow[col] = rowptr[crowso2][col];
        else if ( col == ccolso2 )
        {
        rsum = 0.0;
        gsum = 0.0;
        bsum = 0.0;
        leftcol = col - ccolso2;
        rrowsumptr[addrow][leftcol] = 0L;
        growsumptr[addrow][leftcol] = 0L;
        browsumptr[addrow][leftcol] = 0L;
        for ( ccol = 0; ccol < ccols; ++ccol )
            {
            rrowsumptr[addrow][leftcol] += 
            PPM_GETR( rowptr[addrow][leftcol + ccol] );
            growsumptr[addrow][leftcol] += 
            PPM_GETG( rowptr[addrow][leftcol + ccol] );
            browsumptr[addrow][leftcol] += 
            PPM_GETB( rowptr[addrow][leftcol + ccol] );
            }
        for ( crow = 0; crow < crows; ++crow )
            {
            rsum += rrowsumptr[crow][leftcol] * rweights[crow][0];
            gsum += growsumptr[crow][leftcol] * gweights[crow][0];
            bsum += browsumptr[crow][leftcol] * bweights[crow][0];
            }
        temprsum = rsum + 0.5;
        tempgsum = gsum + 0.5;
        tempbsum = bsum + 0.5;
        CHECK_RED;
        CHECK_GREEN;
        CHECK_BLUE;
        PPM_ASSIGN( outputrow[col], r, g, b );
        }
        else
        {
        rsum = 0.0;
        gsum = 0.0;
        bsum = 0.0;
        leftcol = col - ccolso2;
        subcol = col - ccolso2 - 1;
        addcol = col + ccolso2;  
        rrowsumptr[addrow][leftcol] = rrowsumptr[addrow][subcol]
            - PPM_GETR( rowptr[addrow][subcol] )
            + PPM_GETR( rowptr[addrow][addcol] );
        growsumptr[addrow][leftcol] = growsumptr[addrow][subcol]
            - PPM_GETG( rowptr[addrow][subcol] )
            + PPM_GETG( rowptr[addrow][addcol] );
        browsumptr[addrow][leftcol] = browsumptr[addrow][subcol]
            - PPM_GETB( rowptr[addrow][subcol] )
            + PPM_GETB( rowptr[addrow][addcol] );
        for ( crow = 0; crow < crows; ++crow )
            {
            rsum += rrowsumptr[crow][leftcol] * rweights[crow][0];
            gsum += growsumptr[crow][leftcol] * gweights[crow][0];
            bsum += browsumptr[crow][leftcol] * bweights[crow][0];
            }
        temprsum = rsum + 0.5;
        tempgsum = gsum + 0.5;
        tempbsum = bsum + 0.5;
        CHECK_RED;
        CHECK_GREEN;
        CHECK_BLUE;
        PPM_ASSIGN( outputrow[col], r, g, b );
        }
        }
    pnm_writepnmrow( stdout, outputrow, cols, maxval, newformat, 0 );
    }

    /* Now write out the remaining unconvolved rows in xelbuf. */
    for ( irow = crowso2 + 1; irow < crows; ++irow )
    pnm_writepnmrow(
            stdout, rowptr[irow], cols, maxval, newformat, 0 );

    }


/* PPM Vertical Convolution
**
** Same as pgm_vertical_convolve()
**
*/

static void
ppm_vertical_convolve(const float ** const rweights,
                      const float ** const gweights,
                      const float ** const bweights) {
    int ccol, col;
    xel** xelbuf;
    xel* outputrow;
    xelval r, g, b;
    int row, crow;
    xel **rowptr, *temprptr;
    int i, irow;
    int toprow, temprow;
    int subrow, addrow;
    int tempcol;
    long *rcolumnsum, *gcolumnsum, *bcolumnsum;
    int crowsp1;
    int addcol;
    long temprsum, tempgsum, tempbsum;

    /* Allocate space for one convolution-matrix's worth of rows, plus
    ** a row output buffer. VERTICAL uses an extra row. */
    xelbuf = pnm_allocarray(cols, crows + 1);
    outputrow = pnm_allocrow(cols);

    /* Allocate array of pointers to xelbuf */
    rowptr = (xel **) pnm_allocarray(1, crows + 1);

    /* Allocate space for intermediate column sums */
    MALLOCARRAY_NOFAIL(rcolumnsum, cols);
    MALLOCARRAY_NOFAIL(gcolumnsum, cols);
    MALLOCARRAY_NOFAIL(bcolumnsum, cols);

    for (col = 0; col < cols; ++col) {
        rcolumnsum[col] = 0L;
        gcolumnsum[col] = 0L;
        bcolumnsum[col] = 0L;
    }

    pnm_writepnminit(stdout, cols, rows, maxval, newformat, 0);

    /* Read in one convolution-matrix's worth of image, less one row. */
    for (row = 0; row < crows - 1; ++row) {
        pnm_readpnmrow(ifp, xelbuf[row], cols, maxval, format);
        if (PNM_FORMAT_TYPE(format) != newformat)
            pnm_promoteformatrow(xelbuf[row], cols, maxval, format, 
                                 maxval, newformat);
        /* Write out just the part we're not going to convolve. */
        if (row < crowso2)
            pnm_writepnmrow(stdout, xelbuf[row], cols, maxval, newformat, 0);
    }

    /* Now the rest of the image - read in the row at the end of
    ** xelbuf, and convolve and write out the row in the middle.
    */
    /* For first row only */

    toprow = row + 1;
    temprow = row % crows;
    pnm_readpnmrow(ifp, xelbuf[temprow], cols, maxval, format);
    if (PNM_FORMAT_TYPE(format) != newformat)
        pnm_promoteformatrow(xelbuf[temprow], cols, maxval, format, maxval, 
                             newformat);

    /* Arrange rowptr to eliminate the use of mod function to determine
    ** which row of xelbuf is 0...crows.  Mod function can be very costly.
    */
    temprow = toprow % crows;
    i = 0;
    for (irow = temprow; irow < crows; ++i, ++irow)
        rowptr[i] = xelbuf[irow];
    for (irow = 0; irow < temprow; ++irow, ++i)
        rowptr[i] = xelbuf[irow];

    for (col = 0; col < cols; ++col) {
        if (col < ccolso2 || col >= cols - ccolso2)
            outputrow[col] = rowptr[crowso2][col];
        else if (col == ccolso2) {
            int const leftcol = col - ccolso2;
            float rsum, gsum, bsum;
            rsum = 0.0;
            gsum = 0.0;
            bsum = 0.0;
            for (crow = 0; crow < crows; ++crow) {
                temprptr = rowptr[crow] + leftcol;
                for (ccol = 0; ccol < ccols; ++ccol) {
                    rcolumnsum[leftcol + ccol] += 
                        PPM_GETR(*(temprptr + ccol));
                    gcolumnsum[leftcol + ccol] += 
                        PPM_GETG(*(temprptr + ccol));
                    bcolumnsum[leftcol + ccol] += 
                        PPM_GETB(*(temprptr + ccol));
                }
            }
            for (ccol = 0; ccol < ccols; ++ccol) {
                rsum += rcolumnsum[leftcol + ccol] * rweights[0][ccol];
                gsum += gcolumnsum[leftcol + ccol] * gweights[0][ccol];
                bsum += bcolumnsum[leftcol + ccol] * bweights[0][ccol];
            }
            temprsum = rsum + 0.5;
            tempgsum = gsum + 0.5;
            tempbsum = bsum + 0.5;
            CHECK_RED;
            CHECK_GREEN;
            CHECK_BLUE;
            PPM_ASSIGN(outputrow[col], r, g, b);
        } else {
            int const leftcol = col - ccolso2;
            float rsum, gsum, bsum;
            rsum = 0.0;
            gsum = 0.0;
            bsum = 0.0;
            addcol = col + ccolso2;  
            for (crow = 0; crow < crows; ++crow) {
                rcolumnsum[addcol] += PPM_GETR( rowptr[crow][addcol]);
                gcolumnsum[addcol] += PPM_GETG( rowptr[crow][addcol]);
                bcolumnsum[addcol] += PPM_GETB( rowptr[crow][addcol]);
            }
            for (ccol = 0; ccol < ccols; ++ccol) {
                rsum += rcolumnsum[leftcol + ccol] * rweights[0][ccol];
                gsum += gcolumnsum[leftcol + ccol] * gweights[0][ccol];
                bsum += bcolumnsum[leftcol + ccol] * bweights[0][ccol];
            }
            temprsum = rsum + 0.5;
            tempgsum = gsum + 0.5;
            tempbsum = bsum + 0.5;
            CHECK_RED;
            CHECK_GREEN;
            CHECK_BLUE;
            PPM_ASSIGN(outputrow[col], r, g, b);
        }
    }
    pnm_writepnmrow(stdout, outputrow, cols, maxval, newformat, 0);
    
    /* For all subsequent rows */
    subrow = crows;
    addrow = crows - 1;
    crowsp1 = crows + 1;
    ++row;
    for (; row < rows; ++row) {
        toprow = row + 1;
        temprow = row % (crows +1);
        pnm_readpnmrow(ifp, xelbuf[temprow], cols, maxval, format);
        if (PNM_FORMAT_TYPE(format) != newformat)
            pnm_promoteformatrow(xelbuf[temprow], cols, maxval, format, 
                                 maxval, newformat);

        /* Arrange rowptr to eliminate the use of mod function to determine
        ** which row of xelbuf is 0...crows.  Mod function can be very costly.
        */
        temprow = (toprow + 1) % crowsp1;
        i = 0;
        for (irow = temprow; irow < crowsp1; ++i, ++irow)
            rowptr[i] = xelbuf[irow];
        for (irow = 0; irow < temprow; ++irow, ++i)
            rowptr[i] = xelbuf[irow];

        for (col = 0; col < cols; ++col) {
            if (col < ccolso2 || col >= cols - ccolso2)
                outputrow[col] = rowptr[crowso2][col];
            else if (col == ccolso2) {
                int const leftcol = col - ccolso2;
                float rsum, gsum, bsum;
                rsum = 0.0;
                gsum = 0.0;
                bsum = 0.0;

                for (ccol = 0; ccol < ccols; ++ccol) {
                    tempcol = leftcol + ccol;
                    rcolumnsum[tempcol] = rcolumnsum[tempcol] 
                        - PPM_GETR(rowptr[subrow][ccol])
                        + PPM_GETR(rowptr[addrow][ccol]);
                    rsum = rsum + rcolumnsum[tempcol] * rweights[0][ccol];
                    gcolumnsum[tempcol] = gcolumnsum[tempcol] 
                        - PPM_GETG(rowptr[subrow][ccol])
                        + PPM_GETG(rowptr[addrow][ccol]);
                    gsum = gsum + gcolumnsum[tempcol] * gweights[0][ccol];
                    bcolumnsum[tempcol] = bcolumnsum[tempcol] 
                        - PPM_GETB(rowptr[subrow][ccol])
                        + PPM_GETB(rowptr[addrow][ccol]);
                    bsum = bsum + bcolumnsum[tempcol] * bweights[0][ccol];
                }
                temprsum = rsum + 0.5;
                tempgsum = gsum + 0.5;
                tempbsum = bsum + 0.5;
                CHECK_RED;
                CHECK_GREEN;
                CHECK_BLUE;
                PPM_ASSIGN(outputrow[col], r, g, b);
            } else {
                int const leftcol = col - ccolso2;
                float rsum, gsum, bsum;
                rsum = 0.0;
                gsum = 0.0;
                bsum = 0.0;
                addcol = col + ccolso2;
                rcolumnsum[addcol] = rcolumnsum[addcol]
                    - PPM_GETR(rowptr[subrow][addcol])
                    + PPM_GETR(rowptr[addrow][addcol]);
                gcolumnsum[addcol] = gcolumnsum[addcol]
                    - PPM_GETG(rowptr[subrow][addcol])
                    + PPM_GETG(rowptr[addrow][addcol]);
                bcolumnsum[addcol] = bcolumnsum[addcol]
                    - PPM_GETB(rowptr[subrow][addcol])
                    + PPM_GETB(rowptr[addrow][addcol]);
                for (ccol = 0; ccol < ccols; ++ccol) {
                    rsum += rcolumnsum[leftcol + ccol] * rweights[0][ccol];
                    gsum += gcolumnsum[leftcol + ccol] * gweights[0][ccol];
                    bsum += bcolumnsum[leftcol + ccol] * bweights[0][ccol];
                }
                temprsum = rsum + 0.5;
                tempgsum = gsum + 0.5;
                tempbsum = bsum + 0.5;
                CHECK_RED;
                CHECK_GREEN;
                CHECK_BLUE;
                PPM_ASSIGN(outputrow[col], r, g, b);
            }
        }
        pnm_writepnmrow(stdout, outputrow, cols, maxval, newformat, 0);
    }

    /* Now write out the remaining unconvolved rows in xelbuf. */
    for (irow = crowso2 + 1; irow < crows; ++irow)
        pnm_writepnmrow(stdout, rowptr[irow], cols, maxval, newformat, 0);

}



static void
determineConvolveType(xel * const *         const cxels,
                      struct convolveType * const typeP) {
/*----------------------------------------------------------------------------
   Determine which form of convolution is best.  The general form always
   works, but with some special case convolution matrices, faster forms
   of convolution are possible.

   We don't check for the case that one of the PPM colors can have 
   differing types.  We handle only cases where all PPMs are of the same
   special case.
-----------------------------------------------------------------------------*/
    int horizontal, vertical;
    int tempcxel, rtempcxel, gtempcxel, btempcxel;
    int crow, ccol;

    switch (PNM_FORMAT_TYPE(format)) {
    case PPM_TYPE:
        horizontal = TRUE;  /* initial assumption */
        crow = 0;
        while (horizontal && (crow < crows)) {
            ccol = 1;
            rtempcxel = PPM_GETR(cxels[crow][0]);
            gtempcxel = PPM_GETG(cxels[crow][0]);
            btempcxel = PPM_GETB(cxels[crow][0]);
            while (horizontal && (ccol < ccols)) {
                if ((PPM_GETR(cxels[crow][ccol]) != rtempcxel) ||
                    (PPM_GETG(cxels[crow][ccol]) != gtempcxel) ||
                    (PPM_GETB(cxels[crow][ccol]) != btempcxel)) 
                    horizontal = FALSE;
                ++ccol;
            }
            ++crow;
        }

        vertical = TRUE;   /* initial assumption */
        ccol = 0;
        while (vertical && (ccol < ccols)) {
            crow = 1;
            rtempcxel = PPM_GETR(cxels[0][ccol]);
            gtempcxel = PPM_GETG(cxels[0][ccol]);
            btempcxel = PPM_GETB(cxels[0][ccol]);
            while (vertical && (crow < crows)) {
                if ((PPM_GETR(cxels[crow][ccol]) != rtempcxel) |
                    (PPM_GETG(cxels[crow][ccol]) != gtempcxel) |
                    (PPM_GETB(cxels[crow][ccol]) != btempcxel))
                    vertical = FALSE;
                ++crow;
            }
            ++ccol;
        }
        break;
        
    default:
        horizontal = TRUE; /* initial assumption */
        crow = 0;
        while (horizontal && (crow < crows)) {
            ccol = 1;
            tempcxel = PNM_GET1(cxels[crow][0]);
            while (horizontal && (ccol < ccols)) {
                if (PNM_GET1(cxels[crow][ccol]) != tempcxel)
                    horizontal = FALSE;
                ++ccol;
            }
            ++crow;
        }
        
        vertical = TRUE;  /* initial assumption */
        ccol = 0;
        while (vertical && (ccol < ccols)) {
            crow = 1;
            tempcxel = PNM_GET1(cxels[0][ccol]);
            while (vertical && (crow < crows)) {
                if (PNM_GET1(cxels[crow][ccol]) != tempcxel)
                    vertical = FALSE;
                ++crow;
            }
            ++ccol;
        }
        break;
    }
    
    /* Which type do we have? */
    if (horizontal && vertical) {
        typeP->ppmConvolver = convolvePpmMean;
        typeP->pgmConvolver = convolvePgmMean;
    } else if (horizontal) {
        typeP->ppmConvolver = convolvePpmHorizontal;
        typeP->pgmConvolver = convolvePgmHorizontal;
    } else if (vertical) {
        typeP->ppmConvolver = convolvePpmVertical;
        typeP->pgmConvolver = convolvePgmVertical;
    } else {
        typeP->ppmConvolver = convolvePpmGeneral;
        typeP->pgmConvolver = convolvePgmGeneral;
    }
}



static void
convolveIt(struct pam *        const inpamP,
           struct pam *        const outpamP,
           struct convolveType const convolveType,
           struct convKernel * const convKernelP) {

    switch (PNM_FORMAT_TYPE(inpamP->format)) {
    case PPM_TYPE:
        convolveType.ppmConvolver(inpamP, outpamP, convKernelP);
        break;

    default:
        convolveType.pgmConvolver(inpamP, outpamP, convKernelP);
    }
}



int
main(int argc, char * argv[]) {

    struct cmdlineInfo cmdline;
    FILE * ifP;
    FILE * cifP;
    tuple ** ctuples;
    int cformat;
    xelval cmaxval;
    struct convolveType convolveType;
    struct convKernel * convKernelP;
    struct pam inpam;
    sturct pam cpam;

    pnm_init(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    cifP = pm_openr(cmdline.kernelFilespec);

    /* Read in the convolution matrix. */
    ctuples = pnm_readpam(cifP, &cpam, PAM_STRUCT_SIZE(tuple_type));
    pm_close(cifP);

    if (ccols % 2 != 1 || crows % 2 != 1)
        pm_error("the convolution matrix must have an odd number of "
                 "rows and columns" );

    ccolso2 = ccols / 2;
    crowso2 = crows / 2;

    ifP = pm_openr(cmdline.inputFilespec);

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(allocation_depth));
    if (inpam.cols < cpam.cols || inpam.rows < cpam.rows)
        pm_error("the image is smaller than the convolution matrix" );

    outpam = inpam;

    outpam.format = MAX(PNM_FORMAT_TYPE(cpam.format),
                        PNM_FORMAT_TYPE(inpam.format));
    if (PNM_FORMAT_TYPE(cpam.format) != outpam.format)
        pnm_promoteformat(cxels, ccols, crows, cmaxval, cformat, 
                          cmaxval, outpam.format);
    if (PNM_FORMAT_TYPE(inpam.format) != outpam.format) {
        switch (PNM_FORMAT_TYPE(outpam.format)) {
        case PPM_TYPE:
            if (PNM_FORMAT_TYPE(inpam.format) != outpam.format)
                pm_message("promoting to PPM");
            break;
        case PGM_TYPE:
            if (PNM_FORMAT_TYPE(inpam.format) != outpam.format)
                pm_message("promoting to PGM");
            break;
        }
    }

    pnm_setminallocationdepth(&inpam, MAX(inpam.depth, outpam.depth));

    computeKernel(cpam, ctuples, outpam.format, !cmdline.nooffset,
                  &convKernelP);

    /* Handle certain special cases when runtime can be improved. */

    determineConvolveType(ctuples, &convolveType);

    convolveIt(&inpam, &outpam, convolveType, convKernelP);

    convKernelDestroy(convKernelP);
    pm_close(stdout);
    pm_close(ifP);

    return 0;
}



/******************************************************************************
                            SOME CHANGE HISTORY
*******************************************************************************

 Version 2.0.1 Changes
 ---------------------
 Fixed four lines that were improperly allocated as sizeof( float ) when they
 should have been sizeof( long ).

 Version 2.0 Changes
 -------------------

 Version 2.0 was written by Mike Burns (derived from Jef Poskanzer's
 original) in January 1995.

 Reduce run time by general optimizations and handling special cases of
 convolution matrices.  Program automatically determines if convolution 
 matrix is one of the types it can make use of so no extra command line
 arguments are necessary.

 Examples of convolution matrices for the special cases are

    Mean       Horizontal    Vertical
    x x x        x x x        x y z
    x x x        y y y        x y z
    x x x        z z z        x y z

 I don't know if the horizontal and vertical ones are of much use, but
 after working on the mean convolution, it gave me ideas for the other two.

 Some other compiler dependent optimizations
 -------------------------------------------
 Created separate functions as code was getting too large to put keep both
 PGM and PPM cases in same function and also because SWITCH statement in
 inner loop can take progressively more time the larger the size of the 
 convolution matrix.  GCC is affected this way.

 Removed use of MOD (%) operator from innermost loop by modifying manner in
 which the current xelbuf[] is chosen.

 This is from the file pnmconvol.README, dated August 1995, extracted in
 April 2000, which was in the March 1994 Netpbm release:

 ----------------------------------------------------------------------------- 
 This is a faster version of the pnmconvol.c program that comes with netpbm.
 There are no changes to the command line arguments, so this program can be
 dropped in without affecting the way you currently run it.  An updated man
 page is also included.
 
 My original intention was to improve the running time of applying a
 neighborhood averaging convolution matrix to an image by using a different
 algorithm, but I also improved the run time of performing the general
 convolution by optimizing that code.  The general convolution runs in 1/4 to
 1/2 of the original time and neighborhood averaging runs in near constant
 time for the convolution masks I tested (3x3, 5x5, 7x7, 9x9).
 
 Sample times for two computers are below.  Times are in seconds as reported
 by /bin/time for a 512x512 pgm image.
 
 Matrix                  IBM RS6000      SUN IPC
 Size & Type                220
 
 3x3
 original pnmconvol         6.3            18.4
 new general case           3.1             6.0
 new average case           1.8             2.6
 
 5x5
 original pnmconvol        11.9            44.4
 new general case           5.6            11.9
 new average case           1.8             2.6
 
 7x7
 original pnmconvol        20.3            82.9
 new general case           9.4            20.7
 new average case           1.8             2.6
 
 9x9
 original pnmconvol        30.9           132.4
 new general case          14.4            31.8
 new average case           1.8             2.6
 
 
 Send all questions/comments/bugs to me at burns@chem.psu.edu.
 
 - Mike
 
 ----------------------------------------------------------------------------
 Mike Burns                                              System Administrator
 burns@chem.psu.edu                                   Department of Chemistry
 (814) 863-2123                             The Pennsylvania State University
 ----------------------------------------------------------------------------

*/
