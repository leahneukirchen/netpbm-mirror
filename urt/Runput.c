/*
 * This software is copyrighted as noted below.  It may be freely copied,
 * modified, and redistributed, provided that the copyright notice is
 * preserved on all copies.
 *
 * There is no warranty or other guarantee of fitness for this software,
 * it is provided solely "as is".  Bug reports or fixes may be sent
 * to the author, who may or may not act on them as he desires.
 *
 * You may not include this software in a program or other software product
 * without supplying the source, or without informing the end-user that the
 * source is available for no extra charge.
 *
 * If you modify this software, you should include a notice giving the
 * name of the person performing the modification, the date of modification,
 * and the reason for such modification.
 *
 *  Modified at BRL 16-May-88 by Mike Muuss to avoid Alliant STDC desire
 *  to have all "void" functions so declared.
 */
/*
 * Runput.c - General purpose Run Length Encoding.
 *
 * Author:      Spencer W. Thomas
 *              Computer Science Dept.
 *              University of Utah
 * Date:        Mon Aug  9 1982
 * Copyright (c) 1982,1986 Spencer W. Thomas
 *
 * $Id: Runput.c,v 3.0.1.1 1992/01/28 18:17:40 spencer Exp $
 *
 * Modified by: Todd W. Fuqua
 *      Date:   Jul 22 1984
 * convert to new RLE format to make room for larger frame buffers
 */

/* THIS IS WAY OUT OF DATE.  See rle.5.
 * The output file format is:
 *
 * Word 0:      A "magic" number.  The top byte of the word contains
 *              the letter 'R' or the letter 'W'.  'W' indicates that
 *              only black and white information was saved.  The bottom
 *              byte is one of the following:
 *      ' ':    Means a straight "box" save, -S flag was given.
 *      'B':    Image saved with background color, clear screen to
 *              background before restoring image.
 *      'O':    Image saved in overlay mode.
 *
 * Words 1-6:   The structure
 * {     short   xpos,                  Lower left corner
 *             ypos,
 *             xsize,                   Size of saved box
 *             ysize;
 *     char    rgb[3];                  Background color
 *     char    map;                     flag for map presence
 * }
 *
 * If the map flag is non-zero, then the color map will follow as
 * 3*256 16 bit words, first the red map, then the green map, and
 * finally the blue map.
 *
 * Following the setup information is the Run Length Encoded image.
 * Each instruction consists of a 4-bit opcode, a 12-bit datum and
 * possibly one or more following words (all words are 16 bits).  The
 * instruction opcodes are:
 *
 * SkipLines (1):   The bottom 10 bits are an unsigned number to be added to
 *                  current Y position.
 *
 * SetColor (2):    The datum indicates which color is to be loaded with
 *                  the data described by the following ByteData and
 *                  RunData instructions.  0->red, 1->green, 2->blue.  The
 *                  operation also resets the X position to the initial
 *                  X (i.e. a carriage return operation is performed).
 *
 * SkipPixels (3):  The bottom 10 bits are an unsigned number to be
 *                  added to the current X position.
 *
 * ByteData (5):    The datum is one less than the number of bytes of
 *                  color data following.  If the number of bytes is
 *                  odd, a filler byte will be appended to the end of
 *                  the byte string to make an integral number of 16-bit
 *                  words.  The bytes are in PDP-11 order.  The X
 *                  position is incremented to follow the last byte of
 *                  data.
 *
 * RunData (6):     The datum is one less than the run length.  The
 *                  following word contains (in its lower 8 bits) the
 *                  color of the run.  The X position is incremented to
 *                  follow the last byte in the run.
 */

#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "mallocvar.h"
#include "pm.h"

#include "rle_put.h"
#include "rle.h"
#include "rle_code.h"
#include "vaxshort.h"
#include "Runput.h"

#define UPPER 255                       /* anything bigger ain't a byte */

/*
 * Macros to make writing instructions with correct byte order easier.
 */
/* Write a two-byte value in little_endian order. */
#define put16(a)   (putc((a)&0xff,rle_fd),putc((char)(((a)>>8)&0xff),rle_fd))

/* short instructions */
#define mk_short_1(oper,a1)             /* one argument short */ \
    putc(oper,rle_fd), putc((char)a1,rle_fd)

#define mk_short_2(oper,a1,a2)          /* two argument short */ \
    putc(oper,rle_fd), putc((char)a1,rle_fd), put16(a2)

/* long instructions */
#define mk_long_1(oper,a1)              /* one argument long */ \
    putc((char)(LONG|oper),rle_fd), putc('\0', rle_fd), put16(a1)

#define mk_long_2(oper,a1,a2)           /* two argument long */ \
    putc((char)(LONG|oper),rle_fd), putc('\0', rle_fd),        \
            put16(a1), put16(a2)

/* choose between long and short format instructions */
/* NOTE: these macros can only be used where a STATEMENT is legal */

#define mk_inst_1(oper,a1)              /* one argument inst */ \
    if (a1>UPPER) (mk_long_1(oper,a1)); else (mk_short_1(oper,a1))

#define mk_inst_2(oper,a1,a2)           /* two argument inst */ \
    if (a1>UPPER) (mk_long_2(oper,a1,a2)); else (mk_short_2(oper,a1,a2))

/*
 * Opcode definitions
 */
#define     RSkipLines(n)           mk_inst_1(RSkipLinesOp,(n))

#define     RSetColor(c)            mk_short_1(RSetColorOp,(c))
                                    /* has side effect of performing */
                                    /* "carriage return" action */

#define     RSkipPixels(n)          mk_inst_1(RSkipPixelsOp,(n))

#define     RNewLine                RSkipLines(1)

#define     RByteData(n)            mk_inst_1(RByteDataOp,n)
                                        /* followed by ((n+1)/2)*2 bytes */
                                        /* of data.  If n is odd, last */
                                        /* byte will be ignored */
                                        /* "cursor" is left at pixel */
                                        /* following last pixel written */

#define     RRunData(n,c)           mk_inst_2(RRunDataOp,(n),(c))
                                        /* next word contains color data */
                                        /* "cursor" is left at pixel after */
                                        /* end of run */

#define     REOF                    mk_inst_1(REOFOp,0)
                                        /* Really opcode only */


void
RunSetup(rle_hdr * const hdrP) {
/*----------------------------------------------------------------------------
  Put out initial setup data for RLE files.
-----------------------------------------------------------------------------*/
    FILE * const rle_fd = hdrP->rle_file;

    struct XtndRsetup setup;

    put16(RLE_MAGIC);

    if (hdrP->background == 2)
        setup.h_flags = H_CLEARFIRST;
    else if (hdrP->background == 0)
        setup.h_flags = H_NO_BACKGROUND;
    else
        setup.h_flags = 0;

    if (hdrP->alpha)
        setup.h_flags |= H_ALPHA;

    if (hdrP->comments != NULL && *hdrP->comments != NULL)
        setup.h_flags |= H_COMMENT;

    if (hdrP->ncolors > 255)
        pm_error("Too many colors (%u) for RLE format.  Maximum is 255",
                 hdrP->ncolors);

    setup.h_ncolors = (unsigned char)hdrP->ncolors;
    setup.h_pixelbits = 8;              /* Grinnell dependent */

    if ((hdrP->cmaplen) > sizeof(UINT_MAX) * 8 - 1) {
        pm_error("Color map size logarithm (%u) "
                 "is too large for computation.  Maximum is %lu",
                 hdrP->cmaplen, sizeof(UINT_MAX) * 8 - 2);
    }
    /* We need to be able to count 2 bytes for each channel of each entry
       in the color map:
    */
    if (1 << hdrP->cmaplen > UINT_MAX/2/hdrP->ncmap) {
        pm_error("Color map length %u and number of color channels in the "
                 "color map %u are too large for computation",
                 1 << hdrP->cmaplen, hdrP->ncmap);
    }

    if (hdrP->ncmap > 255)
        pm_error("Too many color channels in the color map (%u) "
                 "for the RLE format.  Maximum is 255", hdrP->ncmap);
    setup.h_ncmap = hdrP->ncmap;     /* no of color channels */

    if (hdrP->ncmap > 0 && hdrP->cmap == NULL) {
        pm_message("Warning: "
                   "Color map of size %d*%d specified, but not supplied, "
                   "writing '%s'",
                   hdrP->ncmap, (1 << hdrP->cmaplen), hdrP->file_name);
        hdrP->ncmap = 0;
    }
    setup.h_cmaplen = (unsigned char)hdrP->cmaplen;

    vax_pshort(setup.hc_xpos,hdrP->xmin);
    vax_pshort(setup.hc_ypos,hdrP->ymin);
    vax_pshort(setup.hc_xlen,hdrP->xmax - hdrP->xmin + 1);
    vax_pshort(setup.hc_ylen,hdrP->ymax - hdrP->ymin + 1);

    fwrite((char *)&setup, SETUPSIZE, 1, rle_fd);

    if (hdrP->background != 0) {
        unsigned int i;
        rle_pixel * background;
        int * bg_color;

        assert(hdrP->ncolors < UINT_MAX);

        MALLOCARRAY_NOFAIL(background, hdrP->ncolors + 1);

        /* If even number of bg color bytes, put out one more to get to
           16 bit boundary.
        */

        for (i = 0, bg_color = hdrP->bg_color; i < hdrP->ncolors; ++i)
            background[i] =  *bg_color++;

        /* Extra byte; if written, should be 0. */
        background[i] = 0;

        fwrite(background, (hdrP->ncolors / 2) * 2 + 1, 1, rle_fd);

        free(background);
    } else
        putc('\0', rle_fd);

    if (hdrP->ncmap > 0) {
        /* Big-endian machines are harder */
        unsigned int const nmap = (1 << hdrP->cmaplen) * hdrP->ncmap;

        unsigned char * h_cmap;
        unsigned int i;

        MALLOCARRAY(h_cmap, nmap * 2);

        if (!h_cmap) {
            pm_error("Failed to allocate memory for color map of size %u, "
                     "writing '%s'",
                     nmap, hdrP->file_name);
        }
        for (i = 0; i < nmap; ++i)
            vax_pshort(&h_cmap[i*2], hdrP->cmap[i]);

        fwrite(h_cmap, nmap, 2, rle_fd);

        free(h_cmap);
    }

    /* Now write out comments if given */
    if (setup.h_flags & H_COMMENT) {
        unsigned int comlen;
        const char ** comP;

        /* Get the total length of comments */
        for (comP = hdrP->comments, comlen = 0; *comP != NULL; ++comP)
            comlen += 1 + strlen(*comP);

        put16(comlen);

        for (comP = hdrP->comments; *comP != NULL; ++comP)
            fwrite(*comP, 1, strlen(*comP) + 1, rle_fd);

        if (comlen & 0x1)       /* if odd length, round up */
            putc('\0', rle_fd);
    }
}



void
RunSkipBlankLines(unsigned int const nblank,
                  rle_hdr *    const hdrP) {
/*----------------------------------------------------------------------------
  Skip one or more blank lines in the RLE file.
-----------------------------------------------------------------------------*/
    FILE * const rle_fd = hdrP->rle_file;

    RSkipLines(nblank);
}



void
RunSetColor(int       const c,
            rle_hdr * const hdrP) {
/*----------------------------------------------------------------------------
  Select a color and do carriage return.
  color: 0 = Red, 1 = Green, 2 = Blue.
-----------------------------------------------------------------------------*/
    FILE * const rle_fd = hdrP->rle_file;

    RSetColor(c);
}



void
RunSkipPixels(unsigned int const nskip,
              int          const last,
              int          const wasrun,
              rle_hdr *    const hdrP) {
/*----------------------------------------------------------------------------
  Skip a run of background.
-----------------------------------------------------------------------------*/
    FILE * const rle_fd = hdrP->rle_file;

    if (!last && nskip > 0) {
        RSkipPixels(nskip);
    }
}



void
RunNewScanLine(int       const flag,
               rle_hdr * const hdrP) {
/*----------------------------------------------------------------------------
  Perform a newline action.  Since CR is implied by the Set Color operation,
  only generate code if the newline flag is true.
-----------------------------------------------------------------------------*/
    FILE * const rle_fd = hdrP->rle_file;

    if (flag) {
        RNewLine;
    }
}



void
Runputdata(rle_pixel *  const buf,
           unsigned int const n,
           rle_hdr *    const hdrP) {
/*----------------------------------------------------------------------------
  Put one or more pixels of byte data into the output file.
-----------------------------------------------------------------------------*/
    FILE * const rle_fd = hdrP->rle_file;

    if (n != 0) {
        RByteData(n - 1);

        fwrite(buf, n, 1, rle_fd);

        if (n & 0x1)
            putc(0, rle_fd);
    }
}



void
Runputrun(int          const color,
          unsigned int const n,
          int          const last,
          rle_hdr *    const hdrP) {
/*----------------------------------------------------------------------------
  Output a single color run.
-----------------------------------------------------------------------------*/
    FILE * const rle_fd = hdrP->rle_file;

    RRunData(n - 1, color);
}



void
RunputEof(rle_hdr * const hdrP) {
/*----------------------------------------------------------------------------
  Output an EOF opcode
-----------------------------------------------------------------------------*/
    FILE * const rle_fd = hdrP->rle_file;

    REOF;
}


