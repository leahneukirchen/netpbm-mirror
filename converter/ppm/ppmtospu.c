/*
 *  ppmtpspu.c - Read a raw PPM file on stdin and write an uncompressed
 *  Spectrum file on stdout.
 *
 *  Copyright (C) 1990, Steve Belczyk
 */

#include <assert.h>
#include <stdio.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pam.h"

#define SPU_WIDTH 320
#define SPU_HEIGHT 200


struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* Name of input file */
    unsigned int dithflag;
        /* dithering flag */
};


static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Parse the program arguments (given by argc and argv) into a form
   the program can deal with more easily -- a cmdline_info structure.
   If the syntax is invalid, issue a message and exit the program via
   pm_error().

   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;  /* malloc'ed */
    optStruct3 opt;  /* set by OPTENT3 */
    unsigned int option_def_index;

    unsigned int d0Spec, d2Spec, d4Spec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "d0",       OPT_FLAG,   
            NULL,                       &d0Spec, 0);
    OPTENT3(0,   "d2",       OPT_FLAG,   
            NULL,                       &d2Spec, 0);
    OPTENT3(0,   "d4",       OPT_FLAG,   
            NULL,                       &d4Spec, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */


    if (d4Spec)
        cmdlineP->dithflag = 4;
    else if (d2Spec)
        cmdlineP->dithflag = 2;
    else if (d0Spec)
        cmdlineP->dithflag = 0;
    else
        cmdlineP->dithflag = 2;

    if (argc-1 < 1) 
        cmdlineP->inputFileName = "-";
    else {
        cmdlineP->inputFileName = argv[1];

        if (argc-1 > 1)
            pm_error("Program takes zero or one argument (filename).  You "
                     "specified %u", argc-1);
    }
}



/* This is the stuff to remember about each pixel */
struct PixelType {
    unsigned int index4;      /* 4-bit color, used in bitmap */
    unsigned int x;           /* Pixel's original x-position */
    unsigned int popularity;  /* Popularity of this pixel's color */
    unsigned int color9;      /* 9-bit color this pixel actually got */
};


typedef struct {
    int index[SPU_WIDTH][16];   /* Indices into the 48 color entries */
} Index48;

typedef struct {
/* These are the palettes, 3 16-color palettes per each of 200 scan lines */
    int pal[SPU_HEIGHT][48];  /* -1 means free */
} Pal;



static void
initializePalette(Pal * const palP) {
/*----------------------------------------------------------------------------
   Set palettes to zero
-----------------------------------------------------------------------------*/
    unsigned int row;

    for (row = 0; row < SPU_HEIGHT; ++row) {
        unsigned int j;
        for (j = 0; j < 48; ++j)
            palP->pal[row][j] = 0;
    }
}



static int
findIndex(unsigned int const col,
          unsigned int const index) {
/*----------------------------------------------------------------------------
   Given an x-coordinate and a color index, return the corresponding
   Spectrum palette index.
-----------------------------------------------------------------------------*/
    int r, x1;
    
    x1 = 10 * index;  /* initial value */
    if (index & 0x1)
        x1 -= 5;
    else
        ++x1;

    r = index;  /* initial value */

    if ((col >= x1) && (col < (x1+160)))
        r += 16;

    if (col >= (x1+160))
        r += 32;
    
    return r;
}



static void
setup48(Index48 * const index48P) {
/*----------------------------------------------------------------------------
  For each pixel position, set up the indices into the 48-color
  palette
-----------------------------------------------------------------------------*/
    unsigned int col;

    for (col = 0; col < SPU_WIDTH; ++col) {
        unsigned int i;
        for (i = 0; i < 16; ++i)
            index48P->index[col][i] = findIndex(col, i);
    }
}



static void
dither(unsigned int       const row,
       tuple *            const tuplerow,
       unsigned int       const dithflag,
       struct PixelType * const pixelType) {

    static int const dith4[4][4] = {
        { 0,  8,  2, 10 },
        { 12, 4, 14,  6 },
        { 3, 11,  1,  9 },
        { 15, 7, 13,  5 }
    };

    static int const dith2[2][2] = {
        { 0, 2 },
        { 3, 1 }
    };
    
    unsigned int c[3];  /* An element for each plane */
    unsigned int col;

    for (col = 0; col < SPU_WIDTH; ++col) {
        unsigned int plane;

        for (plane = 0; plane < 3; ++plane) {
            unsigned int t;

            c[plane] = ((tuplerow[col][plane] & 0xe0) >> 5) & 0x7;
                /* initial value */

            switch (dithflag) {
            case 0:
                break;

            case 2:
                t = (tuplerow[col][plane] & 0x18 ) >> 3;
                if (t > dith2[col%2][row%2])
                    ++c[plane];
                break;

            case 4:
                t = (tuplerow[col][plane] & 0x1e) >> 1;
                if (t > dith4[col%4][row%4])
                    ++c[plane];
                break;
            }
            c[plane] = MIN(7, c[plane]);
        }
        pixelType[col].color9 = (c[0] << 6) | (c[1] << 3) | c[2];
        pixelType[col].x = col;
    }
}



static void
swapPixelType(struct PixelType *  const pixelType,
              unsigned int        const i,
              unsigned int        const j) {

    struct PixelType const w = pixelType[i];

    pixelType[i] = pixelType[j];
    pixelType[j] = w;
}



static void
sort(struct PixelType * const pixelType,
     unsigned int       const left,
     unsigned int       const right) {
/*----------------------------------------------------------------------------
  Sort pixelType[] from element 'left' to (not including) element 'right' in
  increasing popularity.

  Good ol' Quicksort.
-----------------------------------------------------------------------------*/
    unsigned int const pivot = pixelType[(left+right-1)/2].popularity;

    unsigned int i, j;

    /* Rearrange so that everything less than 'pivot' is on the left side of
       the subject array slice and everything greater than is on the right
       side and elements equal could be on either side (we won't know until
       we're done where the dividing line between the sides is), then sort
       those two sides.
    */

    assert(left < right);

    for (i = left, j = right; i < j; ) {
        while (pixelType[i].popularity < pivot)
            ++i;
        while (pixelType[j-1].popularity > pivot)
            --j;
        
        if (i < j) {
            /* An element not less popular than pivot is to the left of a
               pixel not more popular than pivot, so swap them.  Note that we
               could be swapping equal (pivot-valued) elements.  Though the
               swap isn't necessary, moving 'i' and 'j' is.
            */
            swapPixelType(pixelType, i, j-1);
            ++i;
            --j;
        }
    }
    
    if (j - left > 1)
        sort(pixelType, left, j);
    if (right - i > 1)
        sort(pixelType, i, right);
}



static void
computePalette(struct PixelType * const pixelType) {

    unsigned int hist[512];      /* Count for each color */
    unsigned int col;
    unsigned int i;

    /* Uses popularity algorithm */

    /* Count the occurences of each color */

    for (i = 0; i < 512; ++i)
        hist[i] = 0;

    for (col = 0; col < SPU_WIDTH; ++col)
        ++hist[pixelType[col].color9];

    /* Set the popularity of each pixel's color */
    for (col = 0; col < SPU_WIDTH; ++col)
        pixelType[col].popularity = hist[pixelType[col].color9];

    /* Sort to find the most popular colors */
    sort(pixelType, 0, SPU_WIDTH);
}



static int
dist9(unsigned int const x,
      unsigned int const y) {
/*----------------------------------------------------------------------------
    Return the distance between two 9-bit colors.
-----------------------------------------------------------------------------*/
    unsigned int i;
    unsigned int d;
    int x0[3], y0[3];

    x0[0] = (x & 0x007);
    x0[1] = (x & 0x038) >> 3;
    x0[2] = (x & 0x1c0) >> 6;

    y0[0] = (y & 0x007);
    y0[1] = (y & 0x038) >> 3;
    y0[2] = (y & 0x1c0) >> 6;

    for (i = 0, d = 0; i < 3; ++i) {
        unsigned int const t = x0[i] - y0[i];
        d += t * t;
    }

    return d;
}



static void
convertPixel(unsigned int       const col,
             unsigned int       const row,
             struct PixelType * const pixelType,
             Pal *              const palP,
             const Index48 *    const index48P) {

    int ifree;

    unsigned int const x = pixelType[col].x;
    unsigned int const c = pixelType[col].color9;

    ifree = -1;       /* Set if free slot found */

    /* Handle each possible case, from easiest to hardest, in the hopes the
       easy ones are more frequent.
    */

    /* If it wants black, it gets it */
    if (c == 0)
        pixelType[col].index4 = 0;
    else {
        /* If another pixel is using this color, it gets it */
        unsigned int i;
        for (i = 1; i < 15; ++i) {
            /* Check for free slots while we're here */
            if ((ifree < 0) &&
                (palP->pal[row][index48P->index[x][i]] == -1))
                ifree = i;
            else if (c == palP->pal[row][index48P->index[x][i]]) {
                pixelType[col].index4 = i;
                return;
            }
        }

        /* If there are no free slots, we must use the closest entry in use so
           far
        */
        if (ifree < 0) {
            unsigned int i;
            unsigned int d;
            unsigned int b;

            for (i = 1, d = 1000; i < 15; ++i) {
                unsigned int const t =
                    dist9(c, palP->pal[row][index48P->index[x][i]]);
                if (t < d) {
                    d = t;
                    b = i;
                }
            }

            /* See if it would be better off with black */
            if (d > dist9(c, 0))
                b = 0;

            pixelType[col].index4 = b;
        } else {
            /* Use up a slot and give it what it wants */
            palP->pal[row][index48P->index[x][ifree]] = c;
            pixelType[col].index4 = ifree;
        }
    }
}



static void
setPixel(unsigned int const col,
         unsigned int const row,
         unsigned int const c,
         short *      const screen) {

    unsigned int index, bit, plane;

    /* In the next few statements, the bit operations are a little
       quicker, but the arithmetic versions are easier to read and
       maybe more portable.  Please try swapping them if you have
       trouble on your machine.
    */

    /*  index = (80 * row) + 4 * (col / 16);    */
    index = (row << 6) + (row << 4) + ((col >> 4) << 2);

    /*  bit = 0x8000 >> (col % 16);   */
    bit = 0x8000 >> (col & 0x0f);

    for (plane=0; plane<4; ++plane) {
        if (c & (1 << plane))
            screen[index + plane] |= bit;
    }
}



static void
convertRow(unsigned int       const row,
           struct PixelType * const pixelType,
           Pal *              const palP,
           const Index48 *    const index48P,
           short *            const screen) {

    unsigned int i;

    /* Mark palette entries as all free */
    for (i = 0; i < 48; ++i)
        palP->pal[row][i] = -1;
    
    /* Mark reserved palette entries */
    palP->pal[row][0]  = palP->pal[row][15] = palP->pal[row][16] = 0;
    palP->pal[row][31] = palP->pal[row][32] = palP->pal[row][47] = 0;

    /* Convert each pixel */

    {
        /* Process the pixels in order of the popularity of the desired
           color
        */
        int col;
        for (col = SPU_WIDTH-1; col >= 0; --col) {
            convertPixel(col, row, pixelType, palP, index48P);
            setPixel(pixelType[col].x, row, pixelType[col].index4, screen);
        }
    }
}



static void
doRow(unsigned int    const row,
      tuple *         const tuplerow,
      unsigned int    const dithflag,
      const Index48 * const index48P,
      Pal *           const palP,
      short *         const screen) {

    struct PixelType pixelType[SPU_WIDTH];

    /* Dither and reduce to 9 bits */
    dither(row, tuplerow, dithflag, pixelType);

    /* Compute the best colors for this row */
    computePalette(pixelType);

    /* Convert this row */
    convertRow(row, pixelType, palP, index48P, screen);
}



static void
writeScreen(const short * const screen) {

    /* Write the bitmap */

    unsigned int i;
    
    for (i = 0; i < 16000; ++i) {
        char const c0 = 0xff & (screen[i] >> 8);
        char const c1 = 0xff & screen[i];
        putchar(c0);
        putchar(c1);
    }
}



static void
writePalettes(const Pal * const palP) {

    unsigned int row;

    for (row = 1; row < SPU_HEIGHT; ++row) {
        unsigned int i;
        for (i = 0; i < 48; ++i) {
            int const p = palP->pal[row][i];
            unsigned int const q =
                ((p & 0x1c0) << 2) +
                ((p & 0x038) << 1) +
                ((p & 0x007) << 0);

            putchar((q >> 8) & 0xff);
            putchar((q >> 0) & 0xff);
        }
    }
}



static void
writeSpu(const short * const screen,
         const Pal *   const palP ) {

    writeScreen(screen);

    writePalettes(palP);
}



int
main (int argc, const char ** argv) {

    struct pam pam;
    struct CmdlineInfo cmdline;
    FILE * ifP;
    tuple ** tuples;
    Pal pal;
    Index48 index48;
    short screen[16000];  /* This is the ST's video RAM */

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    tuples = pnm_readpam(ifP, &pam, PAM_STRUCT_SIZE(tuple_type));

    if (pam.depth < 3)
        pm_error("Image must be RGB, so at least 3 deep.  This image is "
                 "only %u deep", pam.depth);

    if ((pam.width != SPU_WIDTH) || (pam.height != SPU_HEIGHT))
        pm_error("Image size must be %ux%u.  This one is %u x %u",
                 SPU_WIDTH, SPU_HEIGHT, pam.width, pam.height);

    {
        unsigned int i;
        for (i = 0; i < 16000; screen[i++] = 0);
    }
    setup48(&index48);

    initializePalette(&pal);

    {
        /* Set first row of screen data to black */
        unsigned int i;
        for (i = 0; i < 80; ++i)
            screen[i] = 0;
    }
    {
        unsigned int row;
        for (row = 0; row < SPU_HEIGHT; ++row)
            doRow(row, tuples[row], cmdline.dithflag, &index48, &pal, screen);
    }
    writeSpu(screen, &pal);

    return 0;
}



