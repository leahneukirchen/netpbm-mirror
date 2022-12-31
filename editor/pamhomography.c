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

typedef struct {
/*----------------------------------------------------------------------------
  A point on the image plane.  It may or may not lie within the
  bounds of the image itself.
-----------------------------------------------------------------------------*/
    int x;
    int y;
} Point;


static Point
pointXy(int const x,
        int const y) {

    Point retval;

    retval.x = x;
    retval.y = y;

    return retval;
}



typedef struct {
/*----------------------------------------------------------------------------
  A quadrilateral on the image plane.
-----------------------------------------------------------------------------*/
    Point ul;
    Point ur;
    Point lr;
    Point ll;
} Quad;

typedef struct {
/*----------------------------------------------------------------------------
  A specification of a quadrilateral on the image plane, either explicitly
  or just as "the whole image".
-----------------------------------------------------------------------------*/
    bool wholeImage;
    Quad explicit;  /* Meaningful only if 'wholeImage' is false */
} QuadSpec;

typedef struct {
/*----------------------------------------------------------------------------
   Specification of a mapping from one quadrilateral to another
-----------------------------------------------------------------------------*/
    QuadSpec from;
    QuadSpec to;
} QuadMap;

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFilespec;  /* "-" if stdin */
    QuadMap      qmap;
        /* Source and target quadrilaterals as specified by -to and -from;
           Note that the file identified by 'mapfile' also supplies such
           information.
        */
    QuadSpec     view;
        /* Bounding box for the target image */
    const char * mapfile;        /* Null if not specified */
    const char * fill;           /* Null if not specified */
    unsigned int verbose;
};



static void
parseCoords(const char *   const str,
            int *          const coords,
            unsigned int * const nP) {
/*----------------------------------------------------------------------------
  Parse a list of up to 16 integers.  Return as *nP how may there are.
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
            break;  /* Integer lies out of long int range */
        if (errno != 0 || pnext == p)
            break;  /* Too few integers */
        coords[i] = (int)val;
        if ((long int)coords[i] != val)
            break;  /* Integer lies out of int range */
    }
    *nP = i;
}



static Quad
quadFmViewString(const char * const str) {
/*----------------------------------------------------------------------------
  Parse a list of four integers in the order {ulx, uly, lrx, lry} into a
  quadrilateral.

  Abort the program if 'str' is not valid.
-----------------------------------------------------------------------------*/
    Quad retval;
    int coords[16];
    unsigned int nCoord;

    parseCoords(str, coords, &nCoord);

    if (nCoord != 4)
        pm_error("failed to parse '%s' as a list of four integers", str);

    retval.ul.x = retval.ll.x = coords[0];
    retval.ul.y = retval.ur.y = coords[1];
    retval.lr.x = retval.ur.x = coords[2];
    retval.lr.y = retval.ll.y = coords[3];

    return retval;
}



static Quad
quadFmIntList(const int * const coords) {
    Quad retval;

    retval.ul.x = coords[0];
    retval.ul.y = coords[1];
    retval.ur.x = coords[2];
    retval.ur.y = coords[3];
    retval.lr.x = coords[4];
    retval.lr.y = coords[5];
    retval.ll.x = coords[6];
    retval.ll.y = coords[7];

    return retval;
}



static Quad
quadFmString(const char * const str) {
/*----------------------------------------------------------------------------
  Parse a list of eight integers in the order {ulx, uly, urx, ury,
  lrx, lry, llx, lly} into a quadrilateral.

  Abort the program if 'str' is not a valid list of eight integers.
-----------------------------------------------------------------------------*/
    int coords[16];
    unsigned int nCoord;

    parseCoords(str, coords, &nCoord);

    if (nCoord != 8)
        pm_error("failed to parse '%s' as a list of eight integers", str);

    return quadFmIntList(coords);
}



static void
parseCommandLine(int                        argc,
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

    unsigned int mapfileSpec, fromSpec, toSpec, viewSpec, fillSpec;
    const char * from;
    const char * to;
    const char * view;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "mapfile",         OPT_STRING, &cmdlineP->mapfile,
            &mapfileSpec,             0);
    OPTENT3(0, "from",            OPT_STRING, &from,
            &fromSpec,                0);
    OPTENT3(0, "to",              OPT_STRING, &to,
            &toSpec,                  0);
    OPTENT3(0, "view",            OPT_STRING, &view,
            &viewSpec,                0);
    OPTENT3(0, "fill",            OPT_STRING, &cmdlineP->fill,
            &fillSpec,                0);
    OPTENT3(0, "verbose",         OPT_FLAG,   NULL,
            &cmdlineP->verbose,       0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and local variables. */

    if (!fillSpec)
        cmdlineP->fill = NULL;

    if (!mapfileSpec)
        cmdlineP->mapfile = NULL;

    if (fromSpec) {
        cmdlineP->qmap.from.wholeImage = false;
        cmdlineP->qmap.from.explicit = quadFmString(from);
    } else
        cmdlineP->qmap.from.wholeImage = true;

    if (toSpec) {
        cmdlineP->qmap.to.wholeImage = false;
        cmdlineP->qmap.to.explicit = quadFmString(to);
    } else
        cmdlineP->qmap.to.wholeImage = true;

    if (viewSpec) {
        cmdlineP->view.wholeImage = false;
        cmdlineP->view.explicit   = quadFmViewString(view);
    } else
        cmdlineP->view.wholeImage = true;

    if (argc < 2)
        cmdlineP->inputFilespec = "-";
    else if (argc == 2)
        cmdlineP->inputFilespec = argv[1];
    else
        pm_error("Too many non-option arguments: %u.  "
                 "Only possible argument is input file name", argc - 1);

    free((void *) option_def);
}



static void
readMapFile(const char * const fname,
            QuadMap *    const qmapP) {
/*----------------------------------------------------------------------------
  Read from a file either 16 numbers in the order {ulx1, uly1, urx1, ury1,
  lrx1, lry1, llx1, lly1, ulx2, uly2, urx2, ury2, lrx2, lry2, llx2, lly2}
  or 8 numbers in the order {ulx2, uly2, urx2, ury2, lrx2, lry2, llx2,
  lly2}.

  Abort the program if the file does not contain data in this format.
-----------------------------------------------------------------------------*/
    FILE * fp;
    char * str;      /* Entire file contents */
    int coords[16];  /* File as a list of up to 16 coordinates */
    unsigned int nCoord;
    long int nread;

    /* Read the entire file. */
    fp = pm_openr(fname);
    str = pm_read_unknown_size(fp, &nread);
    REALLOCARRAY_NOFAIL(str, nread + 1);
    str[nread] = '\0';
    pm_close(fp);

    {
        unsigned int i;
        /* Replace newlines and tabs with spaces to prettify error
           reporting.
        */
        for (i = 0; str[i]; ++i)
            if (isspace(str[i]))
                str[i] = ' ';
    }
    parseCoords(str, coords, &nCoord);

    /* Read either {from, to} or just a {to} quadrilateral. */
    switch (nCoord) {
    case 16:
        /* 16 integers: assign both the "from" and the "to" quadrilateral. */
        qmapP->from.wholeImage = false;
        qmapP->from.explicit   = quadFmIntList(&coords[0]);
        qmapP->to.wholeImage   = false;
        qmapP->to.explicit     = quadFmIntList(&coords[8]);
        break;
    case 8:
        /* 8 integers: assign only the "to" quadrilateral. */
        qmapP->from.wholeImage = true;
        qmapP->to.explicit     = quadFmIntList(coords);
        break;
    default:
        /* Not valid input */
        pm_error("failed to parse contents of map file '%s' ('%s') "
                 "as a list of either 8 or 16 integers",
                 fname, str);
        break;
    }

    free(str);
}



static void
reportQuads(Quad const qfrom,
            Quad const qto) {

    pm_message("Copying from ((%d,%d),(%d,%d),(%d,%d),(%d,%d)) "
               "to ((%d,%d),(%d,%d),(%d,%d),(%d,%d))",
               qfrom.ul.x, qfrom.ul.y,
               qfrom.ur.x, qfrom.ur.y,
               qfrom.lr.x, qfrom.lr.y,
               qfrom.ll.x, qfrom.ll.y,
               qto.ul.x,   qto.ul.y,
               qto.ur.x,   qto.ur.y,
               qto.lr.x,   qto.lr.y,
               qto.ll.x,   qto.ll.y
        );
}



static void
reportBbox(Quad const bbox) {

    pm_message("The bounding box is ((%d,%d),(%d,%d),(%d,%d),(%d,%d))",
               bbox.ul.x, bbox.ul.y,
               bbox.ur.x, bbox.ur.y,
               bbox.lr.x, bbox.lr.y,
               bbox.ll.x, bbox.ll.y
        );
}



static tuple
parseFillColor(const struct pam * const pamP,
               const char *       const fillColorSpec) {
/*----------------------------------------------------------------------------
  Parse the fill color into the correct format for the given PAM metadata.
-----------------------------------------------------------------------------*/
    tuple retval;

    if (!fillColorSpec)
        pnm_createBlackTuple(pamP, &retval);
    else {
        tuple const rgb = pnm_parsecolor(fillColorSpec, pamP->maxval);

        retval = pnm_allocpamtuple(pamP);

        switch (pamP->depth) {
        case 1:
            /* Grayscale */
            retval[0] = (rgb[PAM_RED_PLANE]*299 +
                         rgb[PAM_GRN_PLANE]*587 +
                         rgb[PAM_BLU_PLANE]*114)/1000;
            break;
        case 2:
            /* Grayscale + alpha */
            retval[0] = (rgb[PAM_RED_PLANE]*299 +
                         rgb[PAM_GRN_PLANE]*587 +
                         rgb[PAM_BLU_PLANE]*114)/1000;
            retval[PAM_GRAY_TRN_PLANE] = pamP->maxval;
            break;
        case 3:
            /* RGB */
            pnm_assigntuple(pamP, retval, rgb);
            break;
        case 4:
            /* RGB + alpha */
            pnm_assigntuple(pamP, retval, rgb);
            retval[PAM_TRN_PLANE] = pamP->maxval;
            break;
        default:
            pm_error("unexpected image depth %d", pamP->depth);
            break;
        }
    }
    return retval;
}



static tuple **
initOutputImage(const struct pam * const pamP,
                const char *       const fillColorSpec) {
/*----------------------------------------------------------------------------
  Allocate and initialize the output image data structure.
-----------------------------------------------------------------------------*/
    tuple fillColor;    /* Fill color to use for unused coordinates */
    tuple ** outImg;    /* Output image */
    unsigned int row;

    outImg = pnm_allocpamarray(pamP);

    fillColor = parseFillColor(pamP, fillColorSpec);
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
computeSteps(Quad     const qfrom,
             Quad     const qto,
             double * const ustepP,
             double * const vstepP) {
/*----------------------------------------------------------------------------
  Compute increments for u and v as these range from 0.0 to 1.0.
-----------------------------------------------------------------------------*/
    double fx0, fx1, fxd;
    double tx0, tx1, txd;
    double fy0, fy1, fyd;
    double ty0, ty1, tyd;

    /* Compute ustep as the inverse of the maximum possible x delta across
       either the "from" or "to" quadrilateral.
    */
    fx0 = MIN4((double)qfrom.ur.x,
               (double)qfrom.ul.x,
               (double)qfrom.lr.x,
               (double)qfrom.ll.x);
    fx1 = MAX4((double)qfrom.ur.x,
               (double)qfrom.ul.x,
               (double)qfrom.lr.x,
               (double)qfrom.ll.x);
    fxd = fx1 - fx0;

    tx0 = MIN4((double)qto.ur.x,
               (double)qto.ul.x,
               (double)qto.lr.x,
               (double)qto.ll.x);
    tx1 = MAX4((double)qto.ur.x,
               (double)qto.ul.x,
               (double)qto.lr.x,
               (double)qto.ll.x);
    txd = tx1 - tx0;

    if (fxd == 0.0 && txd == 0.0)
        *ustepP = 1.0;  /* Arbitrary nonzero step */
    else
        *ustepP = 0.5/MAX(fxd, txd);
            /* Divide into 0.5 instead of 1.0 for additional smoothing. */

    /* Compute vstep as the inverse of the maximum possible y delta across
       either the "from" or "to" quadrilateral.
    */
    fy0 = MIN4((double)qfrom.ur.y,
               (double)qfrom.ul.y,
               (double)qfrom.lr.y,
               (double)qfrom.ll.y);
    fy1 = MAX4((double)qfrom.ur.y,
               (double)qfrom.ul.y,
               (double)qfrom.lr.y,
               (double)qfrom.ll.y);
    fyd = fy1 - fy0;
    ty0 = MIN4((double)qto.ur.y,
               (double)qto.ul.y,
               (double)qto.lr.y,
               (double)qto.ll.y);
    ty1 = MAX4((double)qto.ur.y,
               (double)qto.ul.y,
               (double)qto.lr.y,
               (double)qto.ll.y);
    tyd = ty1 - ty0;

    if (fyd == 0.0 && tyd == 0.0)
        *vstepP = 1.0;  /* Arbitrary nonzero step */
    else
        *vstepP = 0.5/MAX(fyd, tyd);
            /* Divide into 0.5 instead of 1.0 for additional smoothing. */
}



static Quad
quadrilateralFmSpec(const struct pam * const pamP,
                    QuadSpec           const qdata) {
/*----------------------------------------------------------------------------
  The quadrilateral specified by 'qdata'.
-----------------------------------------------------------------------------*/
    Quad retval;

    if (qdata.wholeImage) {
        /* Set the quadrilateral to the image's bounding box. */
        retval.ul = pointXy(0,               0               );
        retval.ur = pointXy(pamP->width - 1, 0               );
        retval.ll = pointXy(0,               pamP->height - 1);
        retval.lr = pointXy(pamP->width - 1, pamP->height - 1);
    } else {
        /* Use the quadrilateral as specified. */
        retval = qdata.explicit;
    }

    return retval;
}



static Point
coordsAtPercent(Quad   const quad,
                double const u,
                double const v) {
/*----------------------------------------------------------------------------
  Return the (x, y) coordinates that lie at (u%, v%) from the upper left to
  the lower right of a given quadrilateral.
-----------------------------------------------------------------------------*/
    return pointXy(
        (int) nearbyint((1.0 - u) * (1.0 - v) * quad.ul.x +   /* x */
                        u * (1.0 - v) * quad.ur.x +
                        u * v * quad.lr.x +
                        (1.0 - u) * v * quad.ll.x),
        (int) nearbyint((1.0 - u) * (1.0 - v) * quad.ul.y +   /* y */
                        u * (1.0 - v) * quad.ur.y +
                        u * v * quad.lr.y +
                        (1.0 - u) * v * quad.ll.y)
        );
}



static Quad
boundingBoxOfQuadrilateral(Quad const q) {
/*----------------------------------------------------------------------------
  The bounding box of quadrilateral 'q'.
-----------------------------------------------------------------------------*/
    Quad retval;

    int const leftLimit = MIN4(q.ul.x, q.ur.x, q.lr.x, q.ll.x);
    int const rghtLimit = MAX4(q.ul.x, q.ur.x, q.lr.x, q.ll.x);
    int const topLimit  = MIN4(q.ul.y, q.ur.y, q.lr.y, q.ll.y);
    int const botLimit  = MAX4(q.ul.y, q.ur.y, q.lr.y, q.ll.y);

    retval.ul = pointXy(leftLimit, topLimit);
    retval.ur = pointXy(rghtLimit, topLimit);
    retval.ll = pointXy(leftLimit, botLimit);
    retval.lr = pointXy(rghtLimit, botLimit);

    return retval;
}



static void
mapQuadrilaterals(const struct pam * const inPamP,
                  const struct pam * const outPamP,
                  Quad               const qfrom,
                  Quad               const qto,
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
    unsigned int plane;

    MALLOCARRAY2_NOFAIL(channel, outPamP->height, outPamP->width);
    MALLOCARRAY2_NOFAIL(tally,   outPamP->height, outPamP->width);

    computeSteps(qfrom, qto, &ustep, &vstep);

    for (plane = 0; plane < outPamP->depth; ++plane) {
        /* Reset the channel colors and tally for each plane, */
        unsigned int row;
        double v;
        for (row = 0; row < outPamP->height; ++row) {
            unsigned int col;
            for (col = 0; col < outPamP->width; ++col) {
                channel[row][col] = 0;
                tally  [row][col] = 0;
            }
        }
        /* Iterate from 0% to 100% in the y dimension. */
        for (v = 0.0; v <= 1.0; v += vstep) {
            /* Iterate from 0% to 100% in the x dimension. */
            double u;
            for (u = 0.0; u <= 1.0; u += ustep) {
                Point from;  /* "From" coordinate */
                Point to;    /* "To" coordinate */

                /* Map (u%, v%) of one quadrilateral to (u%, v%) of the other
                   quadrilateral.
                */
                from = coordsAtPercent(qfrom, u, v);
                to   = coordsAtPercent(qto,   u, v);

                /* Copy the source image's 'from' pixel as the destination
                   image's 'to' pixel in the current plane.
                */
                to.x += xofs;
                to.y += yofs;
                if (from.x >= 0 && from.y >= 0 &&
                    from.x < inPamP->width && from.y < inPamP->height &&
                    to.x >= 0 && to.y >= 0 &&
                    to.x < outPamP->width && to.y < outPamP->height) {

                    channel[to.y][to.x] += inImg[from.y][from.x][plane];
                    ++tally[to.y][to.x];
                }
            }
        }

        /* Assign the current plane in the output image the average color
           at each point. */
        for (row = 0; row < outPamP->height; ++row) {
            unsigned int col;
            for (col = 0; col < outPamP->width; ++col) {
                if (tally[row][col] != 0)
                    outImg[row][col][plane] =
                        (channel[row][col] + tally[row][col]/2) /
                        tally[row][col];
            }
        }
    }

    pm_freearray2((void ** const)tally);
    pm_freearray2((void ** const)channel);
}



static void
processFile(FILE *       const ifP,
            QuadMap      const qmap,
            QuadSpec     const view,
            const char * const fillColorSpec,
            bool         const verbose) {
/*----------------------------------------------------------------------------
  Read the input image, create the output image, and map a quadrilateral in
  the former to a quadrilateral in the latter.
-----------------------------------------------------------------------------*/
    struct pam inPam;    /* PAM metadata for the input file */
    struct pam outPam;   /* PAM metadata for the output file */
    tuple ** inImg;      /* Input image */
    tuple ** outImg;     /* Output image */
    Quad qfrom, qto;     /* Source and target quadrilaterals */
    Quad bbox;           /* Bounding box around the transformed input image */

    inImg = pnm_readpam(ifP, &inPam, PAM_STRUCT_SIZE(tuple_type));

    /* Extract quadrilaterals and populate them with the image bounds
       if necessary. */
    qfrom = quadrilateralFmSpec(&inPam, qmap.from);
    qto   = quadrilateralFmSpec(&inPam, qmap.to);

    if (verbose)
        reportQuads(qfrom, qto);

    /* Allocate storage for the target image. */
    if (view.wholeImage)
        bbox = boundingBoxOfQuadrilateral(qto);
    else
        bbox = view.explicit;

    if (verbose)
        reportBbox(bbox);

    outPam = inPam;  /* initial value */
    outPam.file   = stdout;
    outPam.width  = bbox.lr.x - bbox.ul.x + 1;
    outPam.height = bbox.lr.y - bbox.ul.y + 1;
    outImg        = initOutputImage(&outPam, fillColorSpec);

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
    QuadMap qmap;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    if (cmdline.mapfile) {
        /* Use the from and/or to values from the map file where the user
           didn't explicitly state them
        */
        QuadMap mapFileValue;

        readMapFile(cmdline.mapfile, &mapFileValue);

        if (cmdline.qmap.from.wholeImage)
            qmap.from = mapFileValue.from;
        else
            qmap.from = cmdline.qmap.from;

        if (cmdline.qmap.to.wholeImage)
            qmap.to = mapFileValue.to;
        else
            qmap.to = cmdline.qmap.to;
    } else
        qmap = cmdline.qmap;

    ifP = pm_openr(cmdline.inputFilespec);

    processFile(ifP, qmap, cmdline.view, cmdline.fill, !!cmdline.verbose);

    pm_close(ifP);

    return 0;
}


