/* ppmtopcl.c - convert PPM into PCL language for HP PaintJet and
 *              PaintJet XL color printers
 * AJCD 12/3/91
 *
 * usage:
 *       ppmtopcl [-nopack] [-gamma <n>] [-presentation] [-dark]
 *          [-diffuse] [-cluster] [-dither]
 *          [-xshift <s>] [-yshift <s>]
 *          [-xshift <s>] [-yshift <s>]
 *          [-xsize|-width|-xscale <s>] [-ysize|-height|-yscale <s>]
 *          [ppmfile]
 *
 */

#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "ppm.h"
#include "runlength.h"

#define MAXCOLORS 1024

const char * const usage="[-nopack] [-gamma <n>] [-presentation] [-dark]\n\
            [-diffuse] [-cluster] [-dither]\n\
            [-xshift <s>] [-yshift <s>]\n\
            [-xshift <s>] [-yshift <s>]\n\
            [-xsize|-width|-xscale <s>] [-ysize|-height|-yscale <s>]\n\
            [ppmfile]";

#define PCL_MAXWIDTH 2048
#define PCL_MAXHEIGHT 32767
#define PCL_MAXVAL 255

static bool nopack = false;
static int dark = 0;
static int diffuse = 0;
static int dither = 0;
static int cluster = 0;
static int xsize = 0;
static int ysize = 0;
static int xshift = 0;
static int yshift = 0;
static int quality = 0;
static double xscale = 0.0;
static double yscale = 0.0;
static double gammaVal = 0.0;

/* argument types */
#define DIM 0
#define REAL 1
#define BOOL 2
static const struct options {
    const char *name;
    int type;
    void *value;
} options[] = {
   {"-gamma",        REAL, &gammaVal },
   {"-presentation", BOOL, &quality },
   {"-width",        DIM,  &xsize },
   {"-xsize",        DIM,  &xsize },
   {"-height",       DIM,  &ysize },
   {"-ysize",        DIM,  &ysize },
   {"-xscale",       REAL, &xscale },
   {"-yscale",       REAL, &yscale },
   {"-xshift",       DIM,  &xshift },
   {"-yshift",       DIM,  &yshift },
   {"-dark",         BOOL, &dark },
   {"-diffuse",      BOOL, &diffuse },
   {"-dither",       BOOL, &dither },
   {"-cluster",      BOOL, &cluster },
   {"-nopack",       BOOL, &nopack },
};



static void
putword(unsigned short const w) {
    putchar((w >> 8) & 0xff);
    putchar((w >> 0) & 0xff);
}



static unsigned int
bitwidth(unsigned int v) {

    unsigned int bpp;

    /* calculate # bits for value */

    for (bpp = 0; v > 0; ) {
        ++bpp;
        v >>= 1;
    }
    return bpp;
}



/* The following belong to the bit putter.  They really should be in a
   struct passed to the methods of the bit putter instead.
*/

static char *inrow;
static char *outrow;
/* "signed" was commented out below, but it caused warnings on an SGI
   compiler, which defaulted to unsigned character.  2001.03.30 BJH */
static signed char * runcnt;
static int out = 0;
static int cnt = 0;
static int num = 0;
static bool pack = false;

static void
initbits(unsigned int const bytesPerRow) {

    MALLOCARRAY(inrow,  bytesPerRow);
    MALLOCARRAY(outrow, bytesPerRow * 2);
    MALLOCARRAY(runcnt, bytesPerRow);

    if (!inrow || !outrow || !runcnt)
        pm_error("can't allocate space for row");
}



static void
termbits() {

    free(runcnt);
    free(outrow);
    free(inrow);
}



static void
putbits(unsigned int const bArg,
        unsigned int const nArg) {
/*----------------------------------------------------------------------------
  Add 'bArg' to byte-packing output buffer as 'n' bits.

  n should never be > 8
-----------------------------------------------------------------------------*/
    int b;
    int n;
    int xo;
    int xc;

    b = bArg;
    n = nArg;

    assert(n <= 8);

    if (cnt + n > 8) {  /* overflowing current byte? */
        xc = cnt + n - 8;
        xo = (b & ~(-1 << xc)) << (8-xc);
        n -= xc;
        b >>= xc;
    } else {
        xo = 0;
        xc = 0;
    }

    cnt += n;

    out |= (b & ~(-1 << n)) << (8-cnt);

    if (cnt >= 8) {
        inrow[num++] = out;
        out = xo;
        cnt = xc;
    }
}



static void
flushbits() {
/*----------------------------------------------------------------------------
   flush a row of buffered bits.
-----------------------------------------------------------------------------*/
    if (cnt) {
        inrow[num++] = out;
        out = cnt = 0;
    }
    for (; num > 0 && inrow[num-1] == 0; --num);
    /* remove trailing zeros */
    printf("\033*b");
    if (num && !nopack) {            /* TIFF 4.0 packbits encoding */
        size_t outSize;
        pm_rlenc_compressbyte(
            (unsigned char *)inrow, (unsigned char *)outrow,
            PM_RLE_PACKBITS, num, &outSize);
        if (outSize < num) {
            num = outSize;
            if (!pack) {
                printf("2m");
                pack = true;
            }
        } else {
            if (pack) {
                printf("0m");
                pack = false;
            }
        }
    }
    printf("%dW", num);
    {
        unsigned int i;
        for (i = 0; i < num; ++i)
            putchar(pack ? outrow[i] : inrow[i]);
    }
    num = 0; /* new row */
}



static void
computeColormap(pixel **           const pixels,
                unsigned int       const cols,
                unsigned int       const rows,
                unsigned int       const maxColors,
                colorhist_vector * const chvP,
                colorhash_table *  const chtP,
                unsigned int *     const colorCtP) {

    colorhist_vector chv;
    colorhash_table cht;
    int colorCt;

    pm_message("Computing colormap...");

    chv = ppm_computecolorhist(pixels, cols, rows, MAXCOLORS, &colorCt);
    if (!chv)
        pm_error("too many colors; reduce to %u or fewer with 'pnmquant'",
                 MAXCOLORS);

    pm_message("... Done.  %u colors found.", colorCt);

    /* And make a hash table for fast lookup. */
    cht = ppm_colorhisttocolorhash(chv, colorCt);

    *chvP     = chv;
    *chtP     = cht;
    *colorCtP = colorCt;
}



static unsigned int
nextPowerOf2(unsigned int const arg) {
/*----------------------------------------------------------------------------
   Works only on 0-7
-----------------------------------------------------------------------------*/
        switch (arg) { /* round up to 1,2,4,8 */
        case 0:                         return 0; break;
        case 1:                         return 1; break;
        case 2:                         return 2; break;
        case 3: case 4:                 return 4; break;
        case 5: case 6: case 7: case 8: return 8; break;
        default:
            assert(false);
        }
}



static void
computeColorDownloadingMode(unsigned int   const colorCt,
                            unsigned int   const cols,
                            pixval         const maxval,
                            unsigned int * const bytesPerRowP,
                            bool *         const colorMappedP,
                            unsigned int * const bitsPerPixelRedP,
                            unsigned int * const bitsPerPixelGrnP,
                            unsigned int * const bitsPerPixelBluP,
                            unsigned int * const bitsPerIndexP) {
/*----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
    unsigned int const indexBitCt = bitwidth(colorCt);

    assert(colorCt > 0);
    assert(indexBitCt > 0);

    if (indexBitCt > 8) {
        /* Can't use indexed mode */
        /* We will instead write 1-3 full bytes per pixel, with those
           bytes divided into red bits, green bits, and blue bits.  We
           pad the red bits as needed to fill out whole bytes.  We
           stick to 1, 2, 4, or 8 bits per pixel only because this program's
           bit writer can't handle more than 8, which would happen with those
           padded red fields if we allowed e.g. 7 bits for green and blue
           (ergo 10 bits for red).
        */
        unsigned int const bitsPerSample = nextPowerOf2(bitwidth(maxval));
        unsigned int const bitsPerPixel  = ROUNDUP(3 * bitsPerSample, 8);
        unsigned int const bytesPerPixel = bitsPerPixel / 8;

        *colorMappedP     = false;
        *bitsPerPixelGrnP = bitsPerSample;
        *bitsPerPixelBluP = bitsPerSample;
        *bitsPerPixelRedP =
            bitsPerPixel - *bitsPerPixelGrnP - *bitsPerPixelBluP;
        *bytesPerRowP = bytesPerPixel * cols;
    } else {
        unsigned int const bitsPerPixel = nextPowerOf2(indexBitCt);

        unsigned int pixelsPerByte;

        *colorMappedP = true;

        *bitsPerIndexP = bitsPerPixel;
        pixelsPerByte = 8 / bitsPerPixel;
        *bytesPerRowP = (cols + pixelsPerByte - 1) / pixelsPerByte;
    }
    if (*colorMappedP)
        pm_message("Writing %u bit color indices", *bitsPerIndexP);
    else
        pm_message("Writing direct color, %u red bits, %u green, %u blue",
                   *bitsPerPixelRedP, *bitsPerPixelGrnP, *bitsPerPixelBluP);
}



static void
writePclHeader(unsigned int const cols,
               unsigned int const rows,
               pixval       const maxval,
               int          const xshift,
               int          const yshift,
               unsigned int const quality,
               unsigned int const xsize,
               unsigned int const ysize,
               double       const gammaVal,
               unsigned int const dark,
               unsigned int const render,
               bool         const colorMapped,
               unsigned int const bitsPerPixelRed,
               unsigned int const bitsPerPixelGrn,
               unsigned int const bitsPerPixelBlu,
               unsigned int const bitsPerIndex) {

#if 0
    printf("\033&l26A");                         /* paper size */
#endif
    printf("\033*r%us%uT", cols, rows);          /* source width, height */
    if (xshift != 0 || yshift != 0)
        printf("\033&a%+dh%+dV", xshift, yshift); /* xshift, yshift */
    if (quality)
        printf("\033*o%uQ", quality);             /* print quality */
    printf("\033*t");
    if (xsize == 0 && ysize == 0)
        printf("180r");                   /* resolution */
    else {                               /* destination width, height */
        if (xsize != 0)
            printf("%uh", xsize);
        if (ysize != 0)
            printf("%uv", ysize);
    }
    if (gammaVal != 0)
        printf("%.3fi", gammaVal);                    /* gamma correction */
    if (dark)
        printf("%uk", dark);              /* scaling algorithms */
    printf("%uJ", render);               /* rendering algorithm */
    printf("\033*v18W");                           /* configure image data */
    putchar(0); /* relative colors */
    putchar(colorMapped ? 1 : 3); /* index/direct pixel mode */
    putchar(bitsPerIndex); /* ignored in direct pixel mode */
    putchar(colorMapped ? 0 : bitsPerPixelRed);
    putchar(colorMapped ? 0 : bitsPerPixelGrn);
    putchar(colorMapped ? 0 : bitsPerPixelBlu);
    putword(maxval); /* max red reference */
    putword(maxval); /* max green reference */
    putword(maxval); /* max blue reference */
    putword(0); /* min red reference */
    putword(0); /* min green reference */
    putword(0); /* min blue reference */
}



static void
writePalette(colorhist_vector const chv,
             unsigned int     const colorCt) {

    unsigned int i;

    for (i = 0; i < colorCt; ++i) {
        unsigned int const r = PPM_GETR( chv[i].color);
        unsigned int const g = PPM_GETG( chv[i].color);
        unsigned int const b = PPM_GETB( chv[i].color);

        if (i == 0)
            printf("\033*v");
        if (r)
            printf("%ua", r);
        if (g)
            printf("%ub", g);
        if (b)
            printf("%uc", b);
        if (i == colorCt - 1)
            printf("%uI", i);    /* assign color index */
        else
            printf("%ui", i);    /* assign color index */
    }
}



static void
writeRaster(pixel **        const pixels,
            unsigned int    const rows,
            unsigned int    const cols,
            colorhash_table const cht,
            bool            const colorMapped,
            unsigned int    const bitsPerIndex,
            unsigned int    const bitsPerPixelRed,
            unsigned int    const bitsPerPixelGrn,
            unsigned int    const bitsPerPixelBlu) {

    unsigned int row;

    for (row = 0; row < rows; ++row) {
        pixel * const pixrow = pixels[row];

        if (colorMapped) {
            unsigned int col;

            for (col = 0; col < cols; ++col)
                putbits(ppm_lookupcolor(cht, &pixrow[col]), bitsPerIndex);
            flushbits();
        } else {
            unsigned int col;

            for (col = 0; col < cols; ++col) {
                putbits(PPM_GETR(pixrow[col]), bitsPerPixelRed);
                putbits(PPM_GETG(pixrow[col]), bitsPerPixelGrn);
                putbits(PPM_GETB(pixrow[col]), bitsPerPixelBlu);
                /* don't need to flush */
            }
            flushbits();
        }
    }
}



int
main(int argc, const char * argv[]) {

    FILE * ifP;
    pixel ** pixels;
    int rows, cols;
    pixval maxval;
    bool colorMapped;
    unsigned int bytesPerRow;
    unsigned int bitsPerPixelRed, bitsPerPixelGrn, bitsPerPixelBlu;
    unsigned int bitsPerIndex;
    int render;
    unsigned int colorCt;
    colorhist_vector chv;
    colorhash_table cht;

    pm_proginit(&argc, argv);

    while (argc > 1 && argv[1][0] == '-') {
        unsigned int i;
        for (i = 0; i < sizeof(options)/sizeof(struct options); i++) {
            if (pm_keymatch(argv[1], options[i].name,
                            MIN(strlen(argv[1]), strlen(options[i].name)))) {
                const char * c;
                switch (options[i].type) {
                case DIM:
                    if (++argv, --argc == 1)
                        pm_usage(usage);
                    for (c = argv[1]; ISDIGIT(*c); c++);
                    if (c[0] == 'p' && c[1] == 't') /* points */
                        *(int *)(options[i].value) = atoi(argv[1])*10;
                    else if (c[0] == 'd' && c[1] == 'p') /* decipoints */
                        *(int *)(options[i].value) = atoi(argv[1]);
                    else if (c[0] == 'i' && c[1] == 'n') /* inches */
                        *(int *)(options[i].value) = atoi(argv[1])*720;
                    else if (c[0] == 'c' && c[1] == 'm') /* centimetres */
                        *(int *)(options[i].value) = atoi(argv[1])*283.46457;
                    else if (!c[0]) /* dots */
                        *(int *)(options[i].value) = atoi(argv[1])*4;
                    else
                        pm_error("illegal unit of measure %s", c);
                    break;
                case REAL:
                    if (++argv, --argc == 1)
                        pm_usage(usage);
                    *(double *)(options[i].value) = atof(argv[1]);
                    break;
                case BOOL:
                    *(int *)(options[i].value) = 1;
                    break;
                }
                break;
            }
        }
        if (i >= sizeof(options)/sizeof(struct options))
            pm_usage(usage);
        argv++; argc--;
    }
    if (argc > 2)
        pm_usage(usage);
    else if (argc == 2)
        ifP = pm_openr(argv[1]);
    else
        ifP = stdin ;

    /* validate arguments */
    if (diffuse+cluster+dither > 1)
        pm_error("only one of -diffuse, -dither and -cluster may be used");
    render = diffuse ? 4 : dither ? 3 : cluster ? 7 : 0;

    if (xsize != 0.0 && xscale != 0.0)
        pm_error("only one of -xsize and -xscale may be used");

    if (ysize != 0.0 && yscale != 0.0)
        pm_error("only one of -ysize and -yscale may be used");

    pixels = ppm_readppm(ifP, &cols, &rows, &maxval);
    pm_close(ifP);

    /* limit checks */
    if (cols > PCL_MAXWIDTH || rows > PCL_MAXHEIGHT)
        pm_error("image too large; reduce with ppmscale");
    if (maxval > PCL_MAXVAL)
        pm_error("color range too large; reduce with ppmcscale");

    computeColormap(pixels, cols, rows, MAXCOLORS, &chv, &cht, &colorCt);

    computeColorDownloadingMode(
        colorCt, cols, maxval,
        &bytesPerRow, &colorMapped,
        &bitsPerPixelRed, &bitsPerPixelGrn, &bitsPerPixelBlu, &bitsPerIndex);

    initbits(bytesPerRow);

    /* set up image details */
    if (xscale != 0.0)
        xsize = cols * xscale * 4;
    if (yscale != 0.0)
        ysize = rows * yscale * 4;

    writePclHeader(cols, rows, maxval, xshift, yshift, quality, xsize, ysize,
                   gammaVal, dark, render,
                   colorMapped,
                   bitsPerPixelRed, bitsPerPixelGrn, bitsPerPixelBlu,
                   bitsPerIndex);

    if (colorMapped)
        writePalette(chv, colorCt);

    /* start raster graphics at CAP */
    printf("\033*r%dA", (xsize != 0 || ysize != 0) ? 3 : 1);

    writeRaster(pixels, rows, cols, cht, colorMapped,
                bitsPerIndex,
                bitsPerPixelRed, bitsPerPixelGrn, bitsPerPixelBlu);

    printf("\033*rC"); /* end raster graphics */

    ppm_freecolorhash(cht);
    ppm_freecolorhist(chv);

    termbits();

    return 0;
}


