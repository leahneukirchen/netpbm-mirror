/*=============================================================================
                                pbmpage
===============================================================================
  This program produces a printed page test pattern in PBM format.

  This was adapted from Tim Norman's 'pbmtpg' program, part of his
  'pbm2ppa' package, by Bryan Henderson on 2000.05.01.  The only
  change was to make it use the Netpbm libraries to generate the
  output.

  For copyright and licensing information, see the pbmtoppa program,
  which was also derived from the same package.
=============================================================================*/

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "nstring.h"
#include "pbm.h"

enum Pattern {PAT_GRID, PAT_VERTICAL, PAT_DIAGONAL};

/* US is 8.5 in by 11 in */

static unsigned int const usWidth  = 5100;
static unsigned int const usHeight = 6600;

/* A4 is 210 mm by 297 mm == 8.27 in by 11.69 in */

static unsigned int const a4Width  = 4960;
static unsigned int const a4Height = 7016;


struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    enum Pattern pattern;
    unsigned int a4;
};



static void
parseCommandLine(int argc, const char ** argv,
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

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "a4",         OPT_FLAG, NULL, &cmdlineP->a4,       0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
    /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 < 1)
        cmdlineP->pattern = PAT_GRID;
    else {
        if (argc-1 > 1)
            pm_error("Too many arguments (%u).  The only possible argument "
                     "is the pattern number", argc-1);
        if (streq(argv[1], "1"))
            cmdlineP->pattern = PAT_GRID;
        else if (streq(argv[1], "2"))
            cmdlineP->pattern = PAT_VERTICAL;
        else if (streq(argv[1], "3"))
            cmdlineP->pattern = PAT_DIAGONAL;
        else
            pm_error("Invalid test pattern name '%s'.  "
                     "We recognize only '1', '2', and '3'", argv[1]);
    }
    free(option_def);
}



struct Bitmap {
    /* width and height in 600ths of an inch */
    unsigned int width;
    unsigned int height;

    unsigned char ** bitmap;
};



static void
setpixel(struct Bitmap * const bitmapP,
         unsigned int    const x,
         unsigned int    const y,
         bit             const c) {

    char const bitmask = 128 >> (x % 8);

    if (x < 0 || x >= bitmapP->width) {
        /* Off the edge of the canvas */
    } else if (y < 0 || y >= bitmapP->height) {
        /* Off the edge of the canvas */
    } else {
        if (c == PBM_BLACK)
            bitmapP->bitmap[y][x/8] |= bitmask;
        else
            bitmapP->bitmap[y][x/8] &= ~bitmask;
    }
}



static void
setplus(struct Bitmap * const bitmapP,
        unsigned int    const x,
        unsigned int    const y,
        unsigned int    const s) {
/*----------------------------------------------------------------------------
   Draw a black plus sign centered at (x,y) with arms 's' pixels long.
   Leave the exact center of the plus white.
-----------------------------------------------------------------------------*/
    unsigned int i;

    for (i = 0; i < s; ++i) {
        setpixel(bitmapP, x + i, y,     PBM_BLACK);
        setpixel(bitmapP, x - i, y,     PBM_BLACK);
        setpixel(bitmapP, x ,    y + i, PBM_BLACK);
        setpixel(bitmapP, x ,    y - i, PBM_BLACK);
    }
}



static void
setblock(struct Bitmap * const bitmapP,
         unsigned int    const x,
         unsigned int    const y,
         unsigned int    const s) {

    unsigned int i;

    for (i = 0; i < s; ++i) {
        unsigned int j;

        for (j = 0; j < s; ++j)
            setpixel(bitmapP, x + i, y + j, PBM_BLACK);
    }
}



static void
setchar(struct Bitmap * const bitmapP,
        unsigned int    const x,
        unsigned int    const y,
        char            const c) {

    static char const charmap[10][5]= { { 0x3e, 0x41, 0x41, 0x41, 0x3e },
                                        { 0x00, 0x42, 0x7f, 0x40, 0x00 },
                                        { 0x42, 0x61, 0x51, 0x49, 0x46 },
                                        { 0x22, 0x41, 0x49, 0x49, 0x36 },
                                        { 0x18, 0x14, 0x12, 0x7f, 0x10 },
                                        { 0x27, 0x45, 0x45, 0x45, 0x39 },
                                        { 0x3e, 0x49, 0x49, 0x49, 0x32 },
                                        { 0x01, 0x01, 0x61, 0x19, 0x07 },
                                        { 0x36, 0x49, 0x49, 0x49, 0x36 },
                                        { 0x26, 0x49, 0x49, 0x49, 0x3e } };

    if (c <= '9' && c >= '0') {
        unsigned int xo;

        for (xo = 0; xo < 5; ++xo) {
            unsigned int yo;

            for (yo = 0; yo < 8; ++yo) {
                if ((charmap[c-'0'][xo] >> yo) & 0x01)
                    setblock(bitmapP, x + xo*3, y + yo*3, 3);
            }
        }
    }
}



static void
setstring(struct Bitmap * const bitmapP,
          unsigned int    const x,
          unsigned int    const y,
          const char *    const s) {

    const char * p;
    unsigned int xo;

    for (xo = 0, p = s; *p; xo += 21, ++p)
        setchar(bitmapP, x + xo, y, *p);
}



static void
setCG(struct Bitmap * const bitmapP,
      unsigned int    const x,
      unsigned int    const y) {

    unsigned int xo;

    for (xo = 0; xo <= 50; ++xo)   {
        unsigned int const yo = sqrt(SQR(50.0) - SQR(xo));

        unsigned int zo;

        setpixel(bitmapP, x + xo    , y + yo    , PBM_BLACK);
        setpixel(bitmapP, x + yo    , y + xo    , PBM_BLACK);
        setpixel(bitmapP, x - 1 - xo, y - 1 - yo, PBM_BLACK);
        setpixel(bitmapP, x - 1 - yo, y - 1 - xo, PBM_BLACK);
        setpixel(bitmapP, x + xo    , y - 1 - yo, PBM_BLACK);
        setpixel(bitmapP, x - 1 - xo, y + yo    , PBM_BLACK);

        for (zo = 0; zo < yo; ++zo) {
            setpixel(bitmapP, x + xo    , y - 1 - zo, PBM_BLACK);
            setpixel(bitmapP, x - 1 - xo, y + zo    , PBM_BLACK);
        }
    }
}



static void
outputPbm(FILE *        const ofP,
          struct Bitmap const bitmap) {
/*----------------------------------------------------------------------------
  Create a pbm file containing the image from the global variable bitmap[].
-----------------------------------------------------------------------------*/
    int const forceplain = 0;

    unsigned int row;

    pbm_writepbminit(ofP, bitmap.width, bitmap.height, forceplain);

    for (row = 0; row < bitmap.height; ++row) {
        pbm_writepbmrow_packed(ofP, bitmap.bitmap[row],
                               bitmap.width, forceplain);
    }
}



static void
framePerimeter(struct Bitmap * const bitmapP) {

    unsigned int x, y;

    /* Top edge */
    for (x = 0; x < bitmapP->width; ++x)
        setpixel(bitmapP, x, 0, PBM_BLACK);

    /* Bottom edge */
    for (x = 0; x < bitmapP->width; ++x)
        setpixel(bitmapP, x, bitmapP->height - 1, PBM_BLACK);

    /* Left edge */
    for (y = 0; y < bitmapP->height; ++y)
        setpixel(bitmapP, 0, y, PBM_BLACK);

    /* Right edge */
    for (y = 0; y < bitmapP->height; ++y)
        setpixel(bitmapP, bitmapP->width - 1, y, PBM_BLACK);
}



static void
makeWhite(struct Bitmap * const bitmapP) {

    unsigned int y;

    for (y = 0; y < bitmapP->height; ++y) {
        unsigned int x;
        for (x = 0; x < pbm_packed_bytes(bitmapP->width); ++x)
            bitmapP->bitmap[y][x] = 0x00;  /* 8 white pixels */
    }
}



static void
drawGrid(struct Bitmap * const bitmapP) {

    char buf[128];

    framePerimeter(bitmapP);
    {
        unsigned int x;
        for (x = 0; x < bitmapP->width; x += 100) {
            unsigned int y;
            for (y = 0; y < bitmapP->height; y += 100)
                setplus(bitmapP, x, y, 4);
        }
    }
    {
        unsigned int x;
        for (x = 0; x < bitmapP->width; x += 100) {
            sprintf(buf,"%u", x);
            setstring(bitmapP, x + 3, (bitmapP->height/200) * 100 + 3, buf);
        }
    }
    {
        unsigned int y;
        for (y = 0; y < bitmapP->height; y += 100) {
            sprintf(buf, "%u", y);
            setstring(bitmapP, (bitmapP->width/200) * 100 + 3, y + 3, buf);
        }
    }
    {
        unsigned int x;
        for (x = 0; x < bitmapP->width; x += 10) {
            unsigned int y;
            for (y = 0; y < bitmapP->height; y += 100)
                setplus(bitmapP, x, y, ((x%100) == 50) ? 2 : 1);
        }
    }
    {
        unsigned int x;
        for (x = 0; x < bitmapP->width; x += 100) {
            unsigned int y;
            for (y = 0; y < bitmapP->height; y += 10)
                setplus(bitmapP, x, y, ((y%100) == 50) ? 2 : 1);
        }
    }
    setCG(bitmapP, bitmapP->width/2, bitmapP->height/2);
}



static void
drawVertical(struct Bitmap * const bitmapP) {

    unsigned int y;

    for (y = 0; y < 300; ++y)
        setpixel(bitmapP, bitmapP->width/2, bitmapP->height/2 - y, PBM_BLACK);
}



static void
drawDiagonal(struct Bitmap * const bitmapP) {

    unsigned int y;

    for (y = 0; y < 300; ++y) {
        setpixel(bitmapP, y, y, PBM_BLACK);
        setpixel(bitmapP, bitmapP->width - 1 - y, bitmapP->height - 1 - y,
                 PBM_BLACK);
    }
}



int
main(int argc, const char** argv) {

    struct CmdlineInfo cmdline;
    /* width and height in 600ths of an inch */
    unsigned int width;
    unsigned int height;
    struct Bitmap bitmap;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    if (cmdline.a4) {
        width  = a4Width;
        height = a4Height;
    } else {
        width  = usWidth;
        height = usHeight;
    }

    bitmap.width  = width;
    bitmap.height = height;
    bitmap.bitmap = pbm_allocarray_packed(width, height);

    makeWhite(&bitmap);

    switch (cmdline.pattern) {
    case PAT_GRID:
        drawGrid(&bitmap);
        break;
    case PAT_VERTICAL:
        drawVertical(&bitmap);
        break;
    case PAT_DIAGONAL:
        drawDiagonal(&bitmap);
        break;
    }

    outputPbm(stdout, bitmap);

    pbm_freearray(bitmap.bitmap, height);

    pm_close(stdout);

    return 0;
}
