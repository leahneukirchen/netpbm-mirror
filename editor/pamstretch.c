/* pamstretch - scale up portable anymap by interpolating between pixels.
 *
 * This program is based on 'pnminterp' by Russell Marks, rename
 * pnmstretch for inclusion in Netpbm, then rewritten and renamed to
 * pamstretch by Bryan Henderson in December 2001.
 *
 * Copyright (C) 1998,2000 Russell Marks.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pam.h"

enum EdgeMode {
    EDGE_DROP,
        /* drop one (source) pixel at right/bottom edges. */
    EDGE_INTERP_TO_BLACK,
        /* interpolate right/bottom edge pixels to black. */
    EDGE_NON_INTERP
        /* don't interpolate right/bottom edge pixels
           (default, and what zgv does). */
};


struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* Filespecs of input files */
    enum EdgeMode edgeMode;
    unsigned int xscale;
    unsigned int yscale;
};



tuple blackTuple;
   /* A "black" tuple.  Unless our input image is PBM, PGM, or PPM, we
      don't really know what "black" means, so this is just something
      arbitrary in that case.
      */


static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file name array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optStruct3 opt;  /* set by OPTENT3 */
    optEntry * option_def;
    unsigned int option_def_index;

    unsigned int blackedge;
    unsigned int dropedge;
    unsigned int xscaleSpec;
    unsigned int yscaleSpec;

    MALLOCARRAY(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3('b', "blackedge",    OPT_FLAG, NULL, &blackedge,            0);
    OPTENT3('d', "dropedge",     OPT_FLAG, NULL, &dropedge,             0);
    OPTENT3(0,   "xscale",       OPT_UINT,
            &cmdlineP->xscale, &xscaleSpec, 0);
    OPTENT3(0,   "yscale",       OPT_UINT,
            &cmdlineP->yscale, &yscaleSpec, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE; /* We have some short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (blackedge && dropedge)
        pm_error("Can't specify both -blackedge and -dropedge options.");
    else if (blackedge)
        cmdlineP->edgeMode = EDGE_INTERP_TO_BLACK;
    else if (dropedge)
        cmdlineP->edgeMode = EDGE_DROP;
    else
        cmdlineP->edgeMode = EDGE_NON_INTERP;

    if (xscaleSpec && cmdlineP->xscale == 0)
        pm_error("You specified zero for the X scale factor.");
    if (yscaleSpec && cmdlineP->yscale == 0)
        pm_error("You specified zero for the Y scale factor.");

    if (xscaleSpec && !yscaleSpec)
        cmdlineP->yscale = 1;
    if (yscaleSpec && !xscaleSpec)
        cmdlineP->xscale = 1;

    if (!(xscaleSpec || yscaleSpec)) {
        /* scale must be specified in an argument */
        if (argc-1 != 1 && argc-1 != 2)
            pm_error("Wrong number of arguments (%d).  Without scale options, "
                     "you must supply 1 or 2 arguments:  scale and "
                     "optional file specification", argc-1);

        {
            const char * error;   /* error message of pm_string_to_uint */
            unsigned int scale;

            pm_string_to_uint(argv[1], &scale, &error);

            if (error)
                pm_error("Invalid scale factor: %s", error);
            else {
                if (scale < 1)
                    pm_error("Scale argument must be at least 1.  "
                             "You specified %d", scale);
                cmdlineP->xscale = scale;
                cmdlineP->yscale = scale;
            }
        }

        if (argc-1 > 1)
            cmdlineP->inputFileName = argv[2];
        else
            cmdlineP->inputFileName = "-";
    } else {
        /* No scale argument allowed */
        if (argc-1 > 1)
            pm_error("Too many arguments (%d).  With a scale option, "
                     "the only argument is the "
                     "optional file specification", argc-1);
        else {
            if (argc-1 > 0)
                cmdlineP->inputFileName = argv[1];
            else
                cmdlineP->inputFileName = "-";
        }
    }
}



static void
stretchLine(struct pam *  const inpamP,
            const tuple * const line,
            const tuple * const lineStretched,
            unsigned int  const scale,
            enum EdgeMode const edgeMode) {
/*----------------------------------------------------------------------------
   Stretch the line of tuples 'line' into the output buffer 'line_stretched',
   by factor 'scale'.
-----------------------------------------------------------------------------*/
    enum EdgeMode const horizontalEdgeMode =
        (scale == 1) ? EDGE_NON_INTERP : edgeMode;

    int scaleincr;
    int sisize;
        /* normalizing factor to make fractions representable as integers.
           E.g. if sisize = 100, one half is represented as 50.
        */
    unsigned int col;
    unsigned int outcol;

    sisize=0;
    while (sisize<256)
        sisize += scale;
    scaleincr = sisize/scale;  /* (1/scale, normalized) */

    outcol = 0;  /* initial value */

    for (col = 0; col < inpamP->width; ++col) {
        unsigned int pos;
            /* The fraction of the way we are from curline to nextline,
               normalized by sisize.
            */
        if (col >= inpamP->width-1) {
            /* We're at the edge.  There is no column to the right with which
               to interpolate.
            */
            switch(horizontalEdgeMode) {
            case EDGE_DROP:
                /* No output column needed for this input column */
                break;
            case EDGE_INTERP_TO_BLACK: {
                unsigned int pos;
                for (pos = 0; pos < sisize; pos += scaleincr) {
                    unsigned int plane;
                    for (plane = 0; plane < inpamP->depth; ++plane)
                        lineStretched[outcol][plane] =
                            (line[col][plane] * (sisize-pos)) / sisize;
                    ++outcol;
                }
            } break;
            case EDGE_NON_INTERP: {
                unsigned int pos;
                for (pos = 0; pos < sisize; pos += scaleincr) {
                    unsigned int plane;
                    for (plane = 0; plane < inpamP->depth; ++plane)
                        lineStretched[outcol][plane] = line[col][plane];
                    ++outcol;
                }
            } break;
            default:
                pm_error("INTERNAL ERROR: invalid value for edgeMode");
            }
        } else {
            /* Interpolate with the next input column to the right */
            for (pos = 0; pos < sisize; pos += scaleincr) {
                unsigned int plane;
                for (plane = 0; plane < inpamP->depth; ++plane)
                    lineStretched[outcol][plane] =
                        (line[col][plane] * (sisize-pos)
                         +  line[col+1][plane] * pos) / sisize;
                ++outcol;
            }
        }
    }
}



static void
writeInterpRows(struct pam *      const outpamP,
                const tuple *     const curline,
                const tuple *     const nextline,
                tuple *           const outbuf,
                int               const scale) {
/*----------------------------------------------------------------------------
   Write out 'scale' rows, being 'curline' followed by rows that are
   interpolated between 'curline' and 'nextline'.
-----------------------------------------------------------------------------*/
    unsigned int scaleIncr;
    unsigned int siSize;
    unsigned int pos;

    for (siSize = 0; siSize < 256; siSize += scale);

    scaleIncr = siSize / scale;

    for (pos = 0; pos < siSize; pos += scaleIncr) {
        unsigned int col;

        for (col = 0; col < outpamP->width; ++col) {
            unsigned int plane;

            for (plane = 0; plane < outpamP->depth; ++plane)
                outbuf[col][plane] = (curline[col][plane] * (siSize - pos)
                    + nextline[col][plane] * pos) / siSize;
        }
        pnm_writepamrow(outpamP, outbuf);
    }
}



static void
swapBuffers(tuple ** const buffer1P,
            tuple ** const buffer2P) {
/*----------------------------------------------------------------------------
  Advance "next" line to "current" line by switching line buffers.
-----------------------------------------------------------------------------*/
    tuple *tmp;

    tmp = *buffer1P;
    *buffer1P = *buffer2P;
    *buffer2P = tmp;
}



static void
stretch(struct pam *  const inpamP,
        struct pam *  const outpamP,
        unsigned int  const xscale,
        unsigned int  const yscale,
        enum EdgeMode const edgeMode) {

    enum EdgeMode const verticalEdgeMode =
        (yscale == 1) ? EDGE_NON_INTERP : edgeMode;

    tuple *linebuf1, *linebuf2;  /* Input buffers for two rows at a time */
    tuple *curline, *nextline;   /* Pointers to one of the two above buffers */
    /* And the stretched versions: */
    tuple *stretchedLinebuf1, *stretchedLinebuf2;
    tuple *curlineStretched, *nextlineStretched;

    tuple *outbuf;   /* One-row output buffer */
    unsigned int row;
    unsigned int nRowsToStretch;

    linebuf1          = pnm_allocpamrow(inpamP);
    linebuf2          = pnm_allocpamrow(inpamP);
    stretchedLinebuf1 = pnm_allocpamrow(outpamP);
    stretchedLinebuf2 = pnm_allocpamrow(outpamP);
    outbuf            = pnm_allocpamrow(outpamP);

    curline = linebuf1;
    curlineStretched = stretchedLinebuf1;
    nextline = linebuf2;
    nextlineStretched = stretchedLinebuf2;

    pnm_readpamrow(inpamP, curline);
    stretchLine(inpamP, curline, curlineStretched, xscale, edgeMode);

    if (verticalEdgeMode == EDGE_DROP)
        nRowsToStretch = inpamP->height - 1;
    else
        nRowsToStretch = inpamP->height;

    for (row = 0; row < nRowsToStretch; ++row) {
        if (row == inpamP->height - 1) {
            /* last line is about to be output. there is no further
             * `next line'.  if EDGE_DROP, we stop here, with output
             * of rows-1 rows.  if EDGE_INTERP_TO_BLACK we make next
             * line black.  if EDGE_NON_INTERP (default) we make it a
             * copy of the current line.
             */
            switch (verticalEdgeMode) {
            case EDGE_INTERP_TO_BLACK: {
                unsigned int col;
                for (col = 0; col < outpamP->width; ++col)
                    nextlineStretched[col] = blackTuple;
            }
            break;
            case EDGE_NON_INTERP: {
                /* EDGE_NON_INTERP */
                unsigned int col;
                for (col = 0; col < outpamP->width; ++col)
                    nextlineStretched[col] = curlineStretched[col];
            }
            break;
            case EDGE_DROP:
                pm_error("INTERNAL ERROR: processing last row, but "
                         "edgeMode is EDGE_DROP.");
            }
        } else {
            pnm_readpamrow(inpamP, nextline);
            stretchLine(inpamP, nextline, nextlineStretched, xscale, edgeMode);
        }

        /* interpolate curline towards nextline into outbuf */
        writeInterpRows(outpamP, curlineStretched, nextlineStretched,
                        outbuf, yscale);

        swapBuffers(&curline, &nextline);
        swapBuffers(&curlineStretched, &nextlineStretched);
    }
    pnm_freerow(outbuf);
    pnm_freerow(stretchedLinebuf2);
    pnm_freerow(stretchedLinebuf1);
    pnm_freerow(linebuf2);
    pnm_freerow(linebuf1);
}


static void
computeOutputWidthHeight(int           const inWidth,
                         int           const inHeight,
                         unsigned int  const xScale,
                         unsigned int  const yScale,
                         enum EdgeMode const edgeMode,
                         int *         const outWidthP,
                         int *         const outHeightP) {

    unsigned int const xDropped =
        (edgeMode == EDGE_DROP && xScale != 1) ? 1 : 0;
    unsigned int const yDropped =
        (edgeMode == EDGE_DROP && yScale != 1) ? 1 : 0;
    double const width  = (inWidth  - xDropped) * xScale;
    double const height = (inHeight - yDropped) * yScale;

    if (width > INT_MAX - 2)
        pm_error("output image width (%f) too large for computations",
                 width);
    if (height > INT_MAX - 2)
        pm_error("output image height (%f) too large for computation",
                 height);

    *outWidthP  = (unsigned int)width;
    *outHeightP = (unsigned int)height;
}



int
main(int argc, const char ** argv) {

    FILE * ifP;

    struct CmdlineInfo cmdline;
    struct pam inpam, outpam;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    if (inpam.width < 2)
        pm_error("Image is too narrow.  Must be at least 2 columns.");
    if (inpam.height < 2)
        pm_error("Image is too short.  Must be at least 2 lines.");

    outpam = inpam;  /* initial value */
    outpam.file = stdout;

    if (PNM_FORMAT_TYPE(inpam.format) == PBM_TYPE) {
        outpam.format = PGM_TYPE;
        /* usual filter message when reading PBM but writing PGM: */
        pm_message("promoting from PBM to PGM");
    } else {
        outpam.format = inpam.format;
    }
    computeOutputWidthHeight(inpam.width, inpam.height,
                             cmdline.xscale, cmdline.yscale, cmdline.edgeMode,
                             &outpam.width, &outpam.height);

    pnm_writepaminit(&outpam);

    pnm_createBlackTuple(&outpam, &blackTuple);

    stretch(&inpam, &outpam,
            cmdline.xscale, cmdline.yscale, cmdline.edgeMode);

    pm_close(ifP);

    exit(0);
}



