/* ----------------------------------------------------------------------
 *
 * Map one quadrilateral to another
 * by Scott Pakin <scott+pbm@pakin.org>
 *
 * ----------------------------------------------------------------------
 *
 * Copyright (C) 2020 Scott Pakin <scott+pbm@pakin.org>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ----------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include "pm_c_util.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "pam.h"

#define MIN4(A, B, C, D) MIN(MIN(A, B), MIN(C, D))
#define MAX4(A, B, C, D) MAX(MAX(A, B), MAX(C, D))

/* A point on the image plane.  It may or may not lie within the
   bounds of the image itself. */
typedef struct Point {
    int x;
    int y;
} Point;

/* A quadrilateral on the image plane */
typedef struct Quad {
    Point ul;
    Point ur;
    Point lr;
    Point ll;
} Quad;

/* A user-specified mapping from one quadrilateral to another */
typedef struct QuadMap {
    Quad from;
    Quad to;
} QuadMap;

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFilespec;  /* "-" if stdin */
    QuadMap      qmap;           /* Source and target quadrilaterals */
    Quad         bbox;           /* Bounding box for the target image */
    const char * fillColor;      /* Fill color for unused coordinates */
};



static unsigned int
parseCoords(const char * const str,
            int *        const coords) {
/*----------------------------------------------------------------------------
  Parse a list of up to 16 integers.  The function returns the number
  of integers encountered.
-----------------------------------------------------------------------------*/

    const char * p;
    char * pnext;
    unsigned int i;

    for (i = 0, p = str; i < 16; ++i, p = pnext) {
        long int val;

        /* Skip punctuation, except "+" and "-", and white space. */
        while (*p != '\0' && *p != '+' && *p != '-' &&
               (isspace(*p) || ispunct(*p)))
            ++p;

        /* Parse the next integer. */
        errno = 0;  /* strtol() sets errno on error. */
        val = strtol(p, &pnext, 10);
        if (errno == ERANGE)
            return i;  /* Integer lies out of long int range */
        if (errno != 0 || pnext == p)
            return i;  /* Too few integers */
        coords[i] = (int)val;
        if ((long int)coords[i] != val)
            return i;  /* Integer lies out of int range */
    }
    return i;
}



static void
parseViewString(const char * const str,
                Quad *       const quad) {
/*----------------------------------------------------------------------------
  Parse a list of four integers in the order {ulx, uly, lrx, lry} into a
  quadrilateral.  The function aborts on error.
-----------------------------------------------------------------------------*/

    int coords[16];

    if (parseCoords(str, coords) != 4)
        pm_error("failed to parse \"%s\" as a list of four integers", str);
    quad->ul.x = quad->ll.x = coords[0];
    quad->ul.y = quad->ur.y = coords[1];
    quad->lr.x = quad->ur.x = coords[2];
    quad->lr.y = quad->ll.y = coords[3];
}



static void
parseQuadString(const char * const str,
                Quad *       const quad) {
/*----------------------------------------------------------------------------
  Parse a list of eight integers in the order {ulx, uly, urx, ury,
  lrx, lry, llx, lly} into a quadrilateral.  The function aborts on
  error.
-----------------------------------------------------------------------------*/

    int coords[16];

    if (parseCoords(str, coords) != 8)
        pm_error("failed to parse \"%s\" as a list of eight integers", str);
    quad->ul.x = coords[0];
    quad->ul.y = coords[1];
    quad->ur.x = coords[2];
    quad->ur.y = coords[3];
    quad->lr.x = coords[4];
    quad->lr.y = coords[5];
    quad->ll.x = coords[6];
    quad->ll.y = coords[7];
}



static void
readMapFile(const char * const fname,
            QuadMap *    const qmap) {
/*----------------------------------------------------------------------------
  Read from a file either 16 numbers in the order {ulx1, uly1, urx1, ury1,
  lrx1, lry1, llx1, lly1, ulx2, uly2, urx2, ury2, lrx2, lry2, llx2, lly2}
  or 8 numbers in the order {ulx2, uly2, urx2, ury2, lrx2, lry2, llx2,
  lly2}.  This function aborts on error.
-----------------------------------------------------------------------------*/

    FILE * fp;
    char * str;      /* Entire file contents */
    int coords[16];  /* File as a list of up to 16 coordinates */
    char * c;
    long int nread;

    /* Read the entire file. */
    fp = pm_openr(fname);
    str = pm_read_unknown_size(fp, &nread);
    REALLOCARRAY_NOFAIL(str, nread + 1);
    str[nread] = '\0';
    pm_close(fp);

    /* Replace newlines and tabs with spaces to prettify error reporting. */
    for (c = str; *c != '\0'; ++c)
        if (isspace(*c))
            *c = ' ';

    /* Read either {from, to} or just a {to} quadrilateral. */
    switch (parseCoords(str, coords)) {
    case 16:
        /* 16 integers: assign both the "from" and the "to" quadrilateral. */
        qmap->from.ul.x = coords[0];
        qmap->from.ul.y = coords[1];
        qmap->from.ur.x = coords[2];
        qmap->from.ur.y = coords[3];
        qmap->from.lr.x = coords[4];
        qmap->from.lr.y = coords[5];
        qmap->from.ll.x = coords[6];
        qmap->from.ll.y = coords[7];
        qmap->to.ul.x = coords[8];
        qmap->to.ul.y = coords[9];
        qmap->to.ur.x = coords[10];
        qmap->to.ur.y = coords[11];
        qmap->to.lr.x = coords[12];
        qmap->to.lr.y = coords[13];
        qmap->to.ll.x = coords[14];
        qmap->to.ll.y = coords[15];
        break;
    case 8:
        /* 8 integers: assign only the "to" quadrilateral. */
        memset((void *)&qmap->from, 0, sizeof(Quad));
        qmap->to.ul.x = coords[0];
        qmap->to.ul.y = coords[1];
        qmap->to.ur.x = coords[2];
        qmap->to.ur.y = coords[3];
        qmap->to.lr.x = coords[4];
        qmap->to.lr.y = coords[5];
        qmap->to.ll.x = coords[6];
        qmap->to.ll.y = coords[7];
        break;
    default:
        /* Any other number of integers: issue an error message. */
        pm_error("failed to parse \"%s\" as a list of either 8 or 16 integers",
                 str);
        break;
    }

    free(str);
}



static void
parseCommandLine(int                  argc,
                 const char **        const argv,
                 struct CmdlineInfo * const cmdlineP ) {
/*----------------------------------------------------------------------------
   Parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.
-----------------------------------------------------------------------------*/

    optEntry *option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int mapFileSpec = 0, fromSpec = 0, toSpec = 0;
    unsigned int viewSpec = 0, fillColorSpec = 0;
    char *mapFile, *from, *to, *view;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "mapfile",         OPT_STRING, &mapFile,
            &mapFileSpec,             0);
    OPTENT3(0, "from",            OPT_STRING, &from,
            &fromSpec,                0);
    OPTENT3(0, "to",              OPT_STRING, &to,
            &toSpec,                  0);
    OPTENT3(0, "view",            OPT_STRING, &view,
            &viewSpec,                0);
    OPTENT3(0, "fill",            OPT_STRING, &cmdlineP->fillColor,
            &fillColorSpec,           0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and local variables. */

    if (!fillColorSpec)
        cmdlineP->fillColor = NULL;

    memset((void *)&cmdlineP->qmap, 0, sizeof(QuadMap));
    if (mapFileSpec)
        readMapFile(mapFile, &cmdlineP->qmap);
    if (fromSpec)
        parseQuadString(from, &cmdlineP->qmap.from);
    if (toSpec)
        parseQuadString(to, &cmdlineP->qmap.to);
    if (!mapFileSpec && !fromSpec && !toSpec && !viewSpec)
        pm_error("You must specify at least one of "
                 "-mapfile, -qin, -qout, and -view");
    if (viewSpec)
        parseViewString(view, &cmdlineP->bbox);
    else
        memset((void *)&cmdlineP->bbox, 0, sizeof(Quad));

    if (argc < 2)
        cmdlineP->inputFilespec = "-";
    else if (argc == 2)
        cmdlineP->inputFilespec = argv[1];
    else
        pm_error("Too many non-option arguments: %u.  "
                 "Only argument is input file name", argc - 1);

    free((void *) option_def);
}



static tuple
parseFillColor(const struct pam *         const pamP,
               const struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
  Parse the fill color into the correct format for the given PAM metadata.
-----------------------------------------------------------------------------*/

    tuple rgb;
    tuple fillColor;

    if (!cmdlineP->fillColor) {
        pnm_createBlackTuple(pamP, &fillColor);
        return fillColor;
    }

    rgb = pnm_parsecolor(cmdlineP->fillColor, pamP->maxval);
    fillColor = pnm_allocpamtuple(pamP);
    switch (pamP->depth) {
    case 1:
        /* Grayscale */
        fillColor[0] = (rgb[PAM_RED_PLANE]*299 +
                        rgb[PAM_GRN_PLANE]*587 +
                        rgb[PAM_BLU_PLANE]*114)/1000;
        break;
    case 2:
        /* Grayscale + alpha */
        fillColor[0] = (rgb[PAM_RED_PLANE]*299 +
                        rgb[PAM_GRN_PLANE]*587 +
                        rgb[PAM_BLU_PLANE]*114)/1000;
        fillColor[PAM_GRAY_TRN_PLANE] = pamP->maxval;
        break;
    case 3:
        /* RGB */
        pnm_assigntuple(pamP, fillColor, rgb);
        break;
    case 4:
        /* RGB + alpha */
        pnm_assigntuple(pamP, fillColor, rgb);
        fillColor[PAM_TRN_PLANE] = pamP->maxval;
        break;
    default:
        pm_error("unexpected image depth %d", pamP->depth);
        break;
    }

    return fillColor;
}



static tuple **
initOutputImage(const struct pam *         const pamP,
                const struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
  Allocate and initialize the output image.
-----------------------------------------------------------------------------*/

    tuple fillColor;    /* Fill color to use for unused coordinates */
    tuple ** outImg;    /* Output image */
    unsigned int row;

    outImg = pnm_allocpamarray(pamP);

    fillColor = parseFillColor(pamP, cmdlineP);
    for (row = 0; row < pamP->height; ++row) {
        unsigned int col;

        for (col = 0; col < pamP->width; ++col) {
            pnm_assigntuple(pamP, outImg[row][col], fillColor);
        }
    }

    free((void *) fillColor);
    return outImg;
}



static void
computeSteps(const Quad * const qfrom,
             const Quad * const qto,
             double *     const ustep,
             double *     const vstep) {
/*----------------------------------------------------------------------------
  Compute increments for u and v as these range from 0.0 to 1.0.
-----------------------------------------------------------------------------*/

    double fx0, fx1, fxd;
    double tx0, tx1, txd;
    double fy0, fy1, fyd;
    double ty0, ty1, tyd;

    /* Compute ustep as the inverse of the maximum possible x delta across
       either the "from" or "to" quadrilateral. */
    fx0 = MIN4((double)qfrom->ur.x,
               (double)qfrom->ul.x,
               (double)qfrom->lr.x,
               (double)qfrom->ll.x);
    fx1 = MAX4((double)qfrom->ur.x,
               (double)qfrom->ul.x,
               (double)qfrom->lr.x,
               (double)qfrom->ll.x);
    fxd = fx1 - fx0;
    tx0 = MIN4((double)qto->ur.x,
               (double)qto->ul.x,
               (double)qto->lr.x,
               (double)qto->ll.x);
    tx1 = MAX4((double)qto->ur.x,
               (double)qto->ul.x,
               (double)qto->lr.x,
               (double)qto->ll.x);
    txd = tx1 - tx0;
    if (fxd == 0.0 && txd == 0.0)
        *ustep = 1.0;  /* Arbitrary nonzero step */
    *ustep = 0.5/MAX(fxd, txd);
        /* Divide into 0.5 instead of 1.0 for additional smoothing. */

    /* Compute vstep as the inverse of the maximum possible y delta across
       either the "from" or "to" quadrilateral
  . */
    fy0 = MIN4((double)qfrom->ur.y,
               (double)qfrom->ul.y,
               (double)qfrom->lr.y,
               (double)qfrom->ll.y);
    fy1 = MAX4((double)qfrom->ur.y,
               (double)qfrom->ul.y,
               (double)qfrom->lr.y,
               (double)qfrom->ll.y);
    fyd = fy1 - fy0;
    ty0 = MIN4((double)qto->ur.y,
               (double)qto->ul.y,
               (double)qto->lr.y,
               (double)qto->ll.y);
    ty1 = MAX4((double)qto->ur.y,
               (double)qto->ul.y,
               (double)qto->lr.y,
               (double)qto->ll.y);
    tyd = ty1 - ty0;
    if (fyd == 0.0 && tyd == 0.0)
        *vstep = 1.0;  /* Arbitrary nonzero step */
    *vstep = 0.5/MAX(fyd, tyd);
        /* Divide into 0.5 instead of 1.0 for additional smoothing. */
}



static Quad *
prepareQuadrilateral(const struct pam * const pamP,
                     const Quad *       const qdata) {
/*----------------------------------------------------------------------------
  If a quadrilateral has all zero points, replace it with a quadrilateral
  of the full size of the image.  The caller should free the result.
-----------------------------------------------------------------------------*/

    Quad * qcopy;

    MALLOCVAR_NOFAIL(qcopy);

    if (qdata->ul.x == 0 && qdata->ul.y == 0 &&
        qdata->ur.x == 0 && qdata->ur.y == 0 &&
        qdata->ll.x == 0 && qdata->ll.y == 0 &&
        qdata->lr.x == 0 && qdata->lr.y == 0) {
        /* Set the quadrilateral to the image's bounding box. */
        memset((void *)qcopy, 0, sizeof(Quad));
        qcopy->ur.x = pamP->width - 1;
        qcopy->lr.x = pamP->width - 1;
        qcopy->lr.y = pamP->height - 1;
        qcopy->ll.y = pamP->height - 1;
    } else {
        /* Use the quadrilateral as specified. */
        memcpy(qcopy, qdata, sizeof(Quad));
    }

    return qcopy;
}


static void
coordsAtPercent(const Quad * const quad,
                double       const u,
                double       const v,
                int *        const x,
                int *        const y) {
/*----------------------------------------------------------------------------
  Return the (x, y) coordinates that lie at (u%, v%) from the upper left to
  the lower right of a given quadrilateral.
-----------------------------------------------------------------------------*/

    *x = (int) nearbyint((1.0 - u)*(1.0 - v)*quad->ul.x +
                         u*(1.0 - v)*quad->ur.x +
                         u*v*quad->lr.x +
                         (1.0 - u)*v*quad->ll.x);
    *y = (int) nearbyint((1.0 - u)*(1.0 - v)*quad->ul.y +
                         u*(1.0 - v)*quad->ur.y +
                         u*v*quad->lr.y +
                         (1.0 - u)*v*quad->ll.y);
}



static void
computeBoundingBox(const Quad * const q,
                   Quad *       const bbox) {
/*----------------------------------------------------------------------------
  Compute the bounding box of a given quadrilateral.
-----------------------------------------------------------------------------*/

    bbox->ul.x = bbox->ll.x = MIN4(q->ul.x, q->ur.x, q->lr.x, q->ll.x);
    bbox->ul.y = bbox->ur.y = MIN4(q->ul.y, q->ur.y, q->lr.y, q->ll.y);
    bbox->ur.x = bbox->lr.x = MAX4(q->ul.x, q->ur.x, q->lr.x, q->ll.x);
    bbox->ll.y = bbox->lr.y = MAX4(q->ul.y, q->ur.y, q->lr.y, q->ll.y);
}



static void
mapQuadrilaterals(const struct pam * const inPamP,
                  const struct pam * const outPamP,
                  const Quad *       const qfrom,
                  const Quad *       const qto,
                  tuple **           const inImg,
                  tuple **           const outImg,
                  int                const xofs,
                  int                const yofs) {
/*----------------------------------------------------------------------------
  Map the quadrilateral in the source image to the quadrilateral in the
  target image.  This is the function that implemens pamhomography's
  primary functionality.
-----------------------------------------------------------------------------*/

    sample ** channel;
        /* Aggregated values for a single channel */
    unsigned long ** tally;
        /* Number of values at each coordinate in the above */
    double ustep, vstep;
        /* Steps to use when iterating from 0.0 to 1.0 */
    double u, v;
    unsigned int plane, row, col;

    MALLOCARRAY2_NOFAIL(channel, outPamP->height, outPamP->width);
    MALLOCARRAY2_NOFAIL(tally, outPamP->height, outPamP->width);

    computeSteps(qfrom, qto, &ustep, &vstep);

    for (plane = 0; plane < outPamP->depth; ++plane) {
        /* Reset the channel colors and tally for each plane, */
        for (row = 0; row < outPamP->height; ++row)
            for (col = 0; col < outPamP->width; ++col) {
                channel[row][col] = 0;
                tally[row][col] = 0;
            }

        /* Iterate from 0% to 100% in the y dimension. */
        for (v = 0.0; v <= 1.0; v += vstep) {
            /* Iterate from 0% to 100% in the x dimension. */
            for (u = 0.0; u <= 1.0; u += ustep) {
                int x0, y0;  /* "From" coordinate */
                int x1, y1;  /* "To" coordinate */

                /* Map (u%, v%) of one quadrilateral to (u%, v%) of the
                   other quadrilateral. */
                coordsAtPercent(qfrom, u, v, &x0, &y0);
                coordsAtPercent(qto, u, v, &x1, &y1);

                /* Copy the source image's (x0, y0) to the destination
                   image's (x1, y1) in the current plane. */
                x1 += xofs;
                y1 += yofs;
                if (x0 >= 0 && y0 >= 0 &&
                    x0 < inPamP->width && y0 < inPamP->height &&
                    x1 >= 0 && y1 >= 0 &&
                    x1 < outPamP->width && y1 < outPamP->height) {
                    channel[y1][x1] += inImg[y0][x0][plane];
                    tally[y1][x1]++;
                }
            }
        }

        /* Assign the current plane in the output image the average color
           at each point. */
        for (row = 0; row < outPamP->height; ++row)
            for (col = 0; col < outPamP->width; ++col)
                if (tally[row][col] != 0)
                    outImg[row][col][plane] =
                        (channel[row][col] + tally[row][col]/2) /
                        tally[row][col];
    }

    pm_freearray2((void ** const)tally);
    pm_freearray2((void ** const)channel);
    free((void *)qto);
    free((void *)qfrom);
}



static void
processFile(FILE *                     const ifP,
            const struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
  Read the input image, create the output image, and map a quadrilateral in
  the former to a quadrilateral in the latter.
-----------------------------------------------------------------------------*/

    struct pam inPam;    /* PAM metadata for the input file */
    struct pam outPam;   /* PAM metadata for the output file */
    tuple ** inImg;      /* Input image */
    tuple ** outImg;     /* Output image */
    Quad *qfrom, *qto;   /* Source and target quadrilaterals */
    Quad bbox;           /* Bounding box around the transformed input image */

    inImg = pnm_readpam(ifP, &inPam, PAM_STRUCT_SIZE(tuple_type));

    /* Extract quadrilaterals and populate them with the image bounds
       if necessary. */
    qfrom = prepareQuadrilateral(&inPam, &cmdlineP->qmap.from);
    qto = prepareQuadrilateral(&inPam, &cmdlineP->qmap.to);

    /* Allocate storage for the target image. */
    if (cmdlineP->bbox.ul.x == 0 && cmdlineP->bbox.ul.y == 0 &&
        cmdlineP->bbox.lr.x == 0 && cmdlineP->bbox.lr.y == 0)
        /* User did not specify a target bounding box.  Compute optimal
           dimensions. */
        computeBoundingBox(qto, &bbox);
    else
        /* User specified a target bounding box.  Use it. */
        bbox = cmdlineP->bbox;
    outPam = inPam;
    outPam.file = stdout;
    outPam.width = bbox.lr.x - bbox.ul.x + 1;
    outPam.height = bbox.lr.y - bbox.ul.y + 1;
    outImg = initOutputImage(&outPam, cmdlineP);

    mapQuadrilaterals(&inPam, &outPam,
                      qfrom, qto,
                      inImg, outImg,
                      -bbox.ul.x, -bbox.ul.y);

    pnm_writepam(&outPam, outImg);

    pnm_freepamarray(outImg, &outPam);
    pnm_freepamarray(inImg, &inPam);
}



int
main(int argc, const char *argv[]) {

    struct CmdlineInfo cmdline;      /* Parsed command line */
    FILE * ifP;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFilespec);

    processFile(ifP, &cmdline);

    pm_close(ifP);

    return 0;
}
