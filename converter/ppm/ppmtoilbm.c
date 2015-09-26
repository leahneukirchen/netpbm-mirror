/* ppmtoilbm.c - read a portable pixmap and produce an IFF ILBM file
**
** Copyright (C) 1989 by Jef Poskanzer.
** Modified by Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
**  20/Jun/93:
**  - 24-bit capability (new options -24if, -24force)
**  - HAM8 capability (well, anything from HAM3 to HAM(MAXPLANES))
**  - now writes up to 8 (16) planes (new options -maxplanes, -fixplanes)
**  - colormap file (new option -map)
**  - write colormap only (new option -cmaponly)
**  - only writes CAMG chunk if it is a HAM-picture
**  29/Aug/93:
**  - operates row-by-row whenever possible
**  - faster colorscaling with lookup-table (~20% faster on HAM pictures)
**  - options -ham8 and -ham6 now imply -hamforce
**  27/Nov/93:
**  - byterun1 compression (this is now default) with new options:
**    -compress, -nocompress, -cmethod, -savemem
**  - floyd-steinberg error diffusion (for std+mapfile and HAM)
**  - new options: -lace and -hires --> write CAMG chunk
**  - LUT for luminance calculation (used by ppm_to_ham)
**  23/Oct/94:
**  - rework of mapfile handling
**  - added RGB8 & RGBN image types
**  - added maskplane and transparent color capability
**  - 24-bit & direct color modified to n-bit deep ILBM
**  - removed "-savemem" option
**  22/Feb/95:
**  - minor bugfixes
**  - fixed "-camg 0" behaviour: now writes a CAMG chunk with value 0
**  - "-24if" is now default
**  - "-mmethod" and "-cmethod" options accept numeric args and keywords
**  - direct color (DCOL) reimplemented
**  - mapfile useable for HAM
**  - added HAM colormap "fixed"
**  29/Mar/95:
**  - added HAM colormap "rgb4" and "rgb5" (compute with 4/5-bit table)
**  - added IFF text chunks
**
**  Feb 2010: afu
**  - added dimension check to prevent short int from overflowing.
**
**  June 2015: afu
**  - moved byterun1 (or Packbits) compression to lib/util/runlenth.c
**  - fixed bug with HAM -nocompress
**
**  TODO:
**  - multipalette capability (PCHG chunk) for std and HAM
**
**
**           std   HAM  deep  cmap  RGB8  RGBN
**  -------+-----+-----+-----+-----+-----+-----
**  BMHD     yes   yes   yes   yes   yes   yes
**  CMAP     yes   (1)   no    yes   no    no
**  BODY     yes   yes   yes   no    yes   yes
**  CAMG     (2)   yes   (2)   no    yes   yes
**  nPlanes  1-16  3-16  3-48  0     25    13
**
**  (1): grayscale colormap
**  (2): only if "-lace", "-hires" or "-camg" option used
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <assert.h>
#include <string.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "ppm.h"
#include "ppmfloyd.h"
#include "pbm.h"
#include "runlength.h"
#include "ilbm.h"
#include "lum.h"

/*#define DEBUG*/

#define MODE_RGB8       6   /* RGB8: 8-bit RGB */
#define MODE_RGBN       5   /* RGBN: 4-bit RGB */
#define MODE_CMAP       4   /* ILBM: colormap only */
#define MODE_DCOL       3   /* ILBM: direct color */
#define MODE_DEEP       2   /* ILBM: deep (24-bit) */
#define MODE_HAM        1   /* ILBM: hold-and-modify (HAM) */
#define MODE_NONE       0   /* ILBM: colormapped */

#define HAMMODE_GRAY    0   /* HAM colormap: grayscale */
#define HAMMODE_FIXED   1   /* HAM colormap: 7 "rays" in RGB cube */
#define HAMMODE_MAPFILE 2   /* HAM colormap: loaded from mapfile */
#define HAMMODE_RGB4    3   /* HAM colormap: compute, 4bit RGB */
#define HAMMODE_RGB5    4   /* HAM colormap: compute, 5bit RGB */

#define ECS_MAXPLANES   5
#define ECS_HAMPLANES   6
#define AGA_MAXPLANES   8
#define AGA_HAMPLANES   8

#define HAMMAXPLANES    10  /* maximum planes for HAM */

#define DEF_MAXPLANES   ECS_MAXPLANES
#define DEF_HAMPLANES   ECS_HAMPLANES
#define DEF_COMPRESSION cmpByteRun1
#define DEF_DEEPPLANES  8
#define DEF_DCOLPLANES  5
#define DEF_IFMODE      MODE_DEEP

#define INT16MAX 32767

#define PAD(n)      (ODD(n) ? 1 : 0)    /* pad to a word */


/* global data */
static unsigned char *coded_rowbuf; /* buffer for uncompressed scanline */
static unsigned char *compr_rowbuf; /* buffer for compressed scanline */
static pixel **pixels;  /* PPM image (NULL for row-by-row operation) */
static pixel *pixrow;   
    /* current row in PPM image (pointer into pixels array, or buffer
       for row-by-row operation) 
    */

static long viewportmodes = 0;
static int slicesize = 1; 
    /* rows per slice for multipalette images - NOT USED */

static unsigned char compmethod = DEF_COMPRESSION;   /* default compression */
static unsigned char maskmethod = mskNone;

static pixel *transpColor = NULL;   /* transparent color */
static short  transpIndex = -1;     /* index of transparent color */

static short hammapmode = HAMMODE_GRAY;
static short sortcmap = 0;     /* sort colormap */

static FILE *maskfile = NULL;
static bit *maskrow = NULL;
static int maskcols, maskformat;
#define TOTALPLANES(nplanes) ((nplanes) + ((maskmethod == mskHasMask) ? 1 : 0))


#define ROWS_PER_BLOCK  1024
typedef struct bodyblock {
    int used;
    unsigned char *row[ROWS_PER_BLOCK];
    int            len[ROWS_PER_BLOCK];
    struct bodyblock *next;
} bodyblock;
static bodyblock firstblock = { 0 };
static bodyblock *cur_block = &firstblock;

static char *anno_chunk, *auth_chunk, *name_chunk, *text_chunk, *copyr_chunk;

/* flags */
static short compr_force = 0;   
    /* force compressed output, even if the image got larger  - NOT USED */
static short floyd = 0;         /* apply floyd-steinberg error diffusion */
static short gen_camg = 0;      /* write CAMG chunk */

#define WORSTCOMPR(bytes)       ((bytes) + (bytes)/128 + 1)
#define DO_COMPRESS             (compmethod != cmpNone)

#define NEWDEPTH(pix, table) PPM_ASSIGN((pix), (table)[PPM_GETR(pix)], (table)[PPM_GETG(pix)], (table)[PPM_GETB(pix)])

#define putByte(b)     (void)(putc((unsigned char)(b), stdout))



/************ other utility functions ************/



static void
writeBytes(unsigned char * const buffer,
           int             const bytes) {

    if( fwrite(buffer, 1, bytes, stdout) != bytes )
        pm_error("write error");
}



static int *
makeValTable(int const oldmaxval,
             int const newmaxval) {

    unsigned int i;
    int * table;

    MALLOCARRAY_NOFAIL(table, oldmaxval + 1);
    for (i = 0; i <= oldmaxval; ++i)
        table[i] = ROUNDDIV(i * newmaxval, oldmaxval);

    return table;
}



static int  gFormat;
static int  gCols;
static int  gMaxval;

static void
initRead(FILE *   const fp,
         int *    const colsP,
         int *    const rowsP,
         pixval * const maxvalP,
         int *    const formatP,
         int      const readall) {

    ppm_readppminit(fp, colsP, rowsP, maxvalP, formatP);

    if( *rowsP >INT16MAX || *colsP >INT16MAX )
      pm_error ("Input image is too large.");

    if( readall ) {
        int row;

        pixels = ppm_allocarray(*colsP, *rowsP);
        for( row = 0; row < *rowsP; row++ )
            ppm_readppmrow(fp, pixels[row], *colsP, *maxvalP, *formatP);
        /* pixels = ppm_readppm(fp, colsP, rowsP, maxvalP); */
    }
    else {
        pixrow = ppm_allocrow(*colsP);
    }
    gCols = *colsP;
    gMaxval = *maxvalP;
    gFormat = *formatP;
}



static pixel *
nextPixrow(FILE * const fp,
           int    const row) {

    if( pixels )
        pixrow = pixels[row];
    else {
        ppm_readppmrow(fp, pixrow, gCols, gMaxval, gFormat);
    }
    if( maskrow ) {
        int col;

        if( maskfile )
            pbm_readpbmrow(maskfile, maskrow, maskcols, maskformat);
        else {
            for( col = 0; col < gCols; col++ )
                maskrow[col] = PBM_BLACK;
        }
        if( transpColor ) {
            for( col = 0; col < gCols; col++ )
                if( PPM_EQUAL(pixrow[col], *transpColor) )
                    maskrow[col] = PBM_WHITE;
        }
    }
    return pixrow;
}



/************ ILBM functions ************/



static int
lengthOfTextChunks(void) {

    int len, n;

    len = 0;
    if( anno_chunk ) {
        n = strlen(anno_chunk);
        len += 4 + 4 + n + PAD(n);      /* ID chunksize text */
    }
    if( auth_chunk ) {
        n = strlen(auth_chunk);
        len += 4 + 4 + n + PAD(n);      /* ID chunksize text */
    }
    if( name_chunk ) {
        n = strlen(name_chunk);
        len += 4 + 4 + n + PAD(n);      /* ID chunksize text */
    }
    if( copyr_chunk ) {
        n = strlen(copyr_chunk);
        len += 4 + 4 + n + PAD(n);      /* ID chunksize text */
    }
    if( text_chunk ) {
        n = strlen(text_chunk);
        len += 4 + 4 + n + PAD(n);      /* ID chunksize text */
    }
    return len;
}



static void
writeTextChunks(void) {

    int n;

    if( anno_chunk ) {
        n = strlen(anno_chunk);
        pm_writebiglong(stdout, ID_ANNO);
        pm_writebiglong(stdout, n);
        writeBytes((unsigned char *)anno_chunk, n);
        if( ODD(n) )
            putByte(0);
    }
    if( auth_chunk ) {
        n = strlen(auth_chunk);
        pm_writebiglong(stdout, ID_AUTH);
        pm_writebiglong(stdout, n);
        writeBytes((unsigned char *)auth_chunk, n);
        if( ODD(n) )
            putByte(0);
    }
    if( copyr_chunk ) {
        n = strlen(copyr_chunk);
        pm_writebiglong(stdout, ID_copy);
        pm_writebiglong(stdout, n);
        writeBytes((unsigned char *)copyr_chunk, n);
        if( ODD(n) )
            putByte(0);
    }
    if( name_chunk ) {
        n = strlen(name_chunk);
        pm_writebiglong(stdout, ID_NAME);
        pm_writebiglong(stdout, n);
        writeBytes((unsigned char *)name_chunk, n);
        if( ODD(n) )
            putByte(0);
    }
    if( text_chunk ) {
        n = strlen(text_chunk);
        pm_writebiglong(stdout, ID_TEXT);
        pm_writebiglong(stdout, n);
        writeBytes((unsigned char *)text_chunk, n);
        if( ODD(n) )
            putByte(0);
    }
}


static void
writeCmap(pixel * const colormap,
          int     const colors,
          int     const maxval) {

    int cmapsize, i;

    cmapsize = 3 * colors;

    /* write colormap */
    pm_writebiglong(stdout, ID_CMAP);
    pm_writebiglong(stdout, cmapsize);
    if( maxval != MAXCOLVAL ) {
        int *table;
        pm_message("maxval is not %d - automatically rescaling colors", 
                   MAXCOLVAL);
        table = makeValTable(maxval, MAXCOLVAL);
        for( i = 0; i < colors; i++ ) {
            putByte(table[PPM_GETR(colormap[i])]);
            putByte(table[PPM_GETG(colormap[i])]);
            putByte(table[PPM_GETB(colormap[i])]);
        }
        free(table);
    }
    else {
        for( i = 0; i < colors; i++ ) {
            putByte(PPM_GETR(colormap[i]));
            putByte(PPM_GETG(colormap[i]));
            putByte(PPM_GETB(colormap[i]));
        }
    }
    if( ODD(cmapsize) )
        putByte(0);
}



static void
writeBmhd(int const cols,
          int const rows,
          int const nPlanes) {

    unsigned char xasp, yasp;

    xasp = 10;  /* initial value */
    yasp = 10;  /* initial value */

    if( viewportmodes & vmLACE )
        xasp *= 2;
    if( viewportmodes & vmHIRES )
        yasp *= 2;

    pm_writebiglong(stdout, ID_BMHD);
    pm_writebiglong(stdout, BitMapHeaderSize);

    pm_writebigshort(stdout, cols);
    pm_writebigshort(stdout, rows);
    pm_writebigshort(stdout, 0);                       /* x-offset */
    pm_writebigshort(stdout, 0);                       /* y-offset */
    putByte(nPlanes);                      /* no of planes */
    putByte(maskmethod);                   /* masking */
    putByte(compmethod);                   /* compression */
    putByte(BMHD_FLAGS_CMAPOK);            /* flags */
    if( maskmethod == mskHasTransparentColor )
        pm_writebigshort(stdout, transpIndex);
    else
        pm_writebigshort(stdout, 0);
    putByte(xasp);                         /* x-aspect */
    putByte(yasp);                         /* y-aspect */
    pm_writebigshort(stdout, cols);                    /* pageWidth */
    pm_writebigshort(stdout, rows);                    /* pageHeight */
}



/************ compression ************/

static void
storeBodyrow(unsigned char * const row,
             int             const len) {

    int idx;

    idx = cur_block->used;  /* initial value */

    if (idx >= ROWS_PER_BLOCK) {
        MALLOCVAR_NOFAIL(cur_block->next);
        cur_block = cur_block->next;
        cur_block->used = idx = 0;
        cur_block->next = NULL;
    }
    MALLOCARRAY_NOFAIL(cur_block->row[idx], len);
    cur_block->len[idx] = len;
    memcpy(cur_block->row[idx], row, len);
    ++cur_block->used;
}



static unsigned int
compressRow(unsigned int const bytes) {

    size_t compressedByteCt;

    switch (compmethod) {
        case cmpByteRun1:
            pm_rlenc_compressbyte(
                coded_rowbuf, compr_rowbuf, PM_RLE_PACKBITS, bytes,
                &compressedByteCt);
            break;
        default:
            pm_error("compressRow(): unknown compression method %d", 
                     compmethod);
    }
    storeBodyrow(compr_rowbuf, compressedByteCt);

    assert((unsigned)compressedByteCt == compressedByteCt);

    return (unsigned)compressedByteCt;
}



static const unsigned char bitmask[] = {1, 2, 4, 8, 16, 32, 64, 128};



static long
encodeRow(FILE *    const outfile,
              /* if non-NULL, write uncompressed row to this file */
          rawtype * const rawrow,
          int       const cols,
          int       const nPlanes) {

    /* encode algorithm by Johan Widen (jw@jwdata.se) */

    int plane, bytes;
    long retbytes = 0;

    bytes = RowBytes(cols);

    /* Encode and write raw bytes in plane-interleaved form. */
    for( plane = 0; plane < nPlanes; plane++ ) {
        register int col, cbit;
        register rawtype *rp;
        register unsigned char *cp;
        int mask;

        mask = 1 << plane;
        cbit = -1;
        cp = coded_rowbuf-1;
        rp = rawrow;
        for( col = 0; col < cols; col++, cbit--, rp++ ) {
            if( cbit < 0 ) {
                cbit = 7;
                *++cp = 0;
            }
            if( *rp & mask )
                *cp |= bitmask[cbit];
        }
        if( outfile ) {
            writeBytes(coded_rowbuf, bytes);
            retbytes += bytes;
        }
        else
            retbytes += compressRow(bytes);
    }
    return retbytes;
}



static long
encodeMaskrow(FILE *    const ofP,
              rawtype * const rawrow,
              int       const cols) {

    int col;

    for( col = 0; col < cols; col++ ) {
        if( maskrow[col] == PBM_BLACK )
            rawrow[col] = 1;
        else
            rawrow[col] = 0;
    }
    return encodeRow(ofP, rawrow, cols, 1);
}



static void
writeCamg(void) {
    pm_writebiglong(stdout, ID_CAMG);
    pm_writebiglong(stdout, CAMGChunkSize);
    pm_writebiglong(stdout, viewportmodes);
}



static void
reportTooManyColors(int         const ifmode,
                    int         const maxplanes,
                    int         const hamplanes,
                    DirectColor const dcol,
                    int         const deepbits) {
    
    int const maxcolors = 1 << maxplanes;

    switch( ifmode ) {
    case MODE_HAM:
        pm_message("too many colors for %d planes - "
                   "proceeding to write a HAM%d file", 
                   maxplanes, hamplanes);
        pm_message("if you want a non-HAM file, try doing a 'pnmquant %d'", 
                   maxcolors);
        break;
    case MODE_DCOL:
        pm_message("too many colors for %d planes - "
                   "proceeding to write a %d:%d:%d direct color ILBM", 
                   maxplanes, dcol.r, dcol.g, dcol.b);
        pm_message("if you want a non-direct color file, "
                   "try doing a 'pnmquant %d'", maxcolors);
        break;
    case MODE_DEEP:
        pm_message("too many colors for %d planes - "
                   "proceeding to write a %d-bit \'deep\' ILBM", 
                   maxplanes, deepbits*3);
        pm_message("if you want a non-deep file, "
                   "try doing a 'pnmquant %d'", 
                   maxcolors);
        break;
    default:
        pm_error("too many colors for %d planes - "
                 "try doing a 'pnmquant %d'", 
                 maxplanes, maxcolors);
        break;
    }
}



static int
getIntVal(const char * const string,
          const char * const option,
          int          const bot,
          int          const top) {

    int val;

    if (sscanf(string, "%d", &val) != 1 )
        pm_error("option \"%s\" needs integer argument", option);

    if (val < bot || val > top)
        pm_error("option \"%s\" argument value out of range (%d..%d)", 
                 option, bot, top);

    return val;
}



static int
getComprMethod(const char * const string) {

    int retval;
    if( pm_keymatch(string, "none", 1) || pm_keymatch(string, "0", 1) )
        retval = cmpNone;
    else if( pm_keymatch(string, "byterun1", 1) || 
             pm_keymatch(string, "1", 1) )
        retval = cmpByteRun1;
    else 
        pm_error("unknown compression method: %s", string);
    return retval;
}



static int
getMaskType(const char * const string) {

    int retval;

    if( pm_keymatch(string, "none", 1) || pm_keymatch(string, "0", 1) )
        retval = mskNone;
    else
    if( pm_keymatch(string, "plane", 1) || 
        pm_keymatch(string, "maskplane", 1) ||
        pm_keymatch(string, "1", 1) )
        retval = mskHasMask;
    else
    if( pm_keymatch(string, "transparentcolor", 1) || 
        pm_keymatch(string, "2", 1) )
        retval = mskHasTransparentColor;
    else
    if( pm_keymatch(string, "lasso", 1) || pm_keymatch(string, "3", 1) )
        retval = mskLasso;
    else
        pm_error("unknown masking method: %s", string);
    return retval;
}



static int
getHammapMode(const char * const string) {

    int retval;

    if( pm_keymatch(string, "grey", 1) || pm_keymatch(string, "gray", 1) )
        retval =  HAMMODE_GRAY;
    else
    if( pm_keymatch(string, "fixed", 1) )
        retval =  HAMMODE_FIXED;
    else
    if( pm_keymatch(string, "rgb4", 4) )
        retval = HAMMODE_RGB4;
    else
    if( pm_keymatch(string, "rgb5", 4) )
        retval = HAMMODE_RGB5;
    else 
        pm_error("unknown HAM colormap selection mode: %s", string);
    return retval;
}



/************ colormap file ************/



static void
ppmToCmap(pixel * const colorrow,
          int     const colors,
          int     const maxval) {

    int formsize, cmapsize;

    cmapsize = colors * 3;

    formsize =
        4 +                                 /* ILBM */
        4 + 4 + BitMapHeaderSize +          /* BMHD size header */
        4 + 4 + cmapsize + PAD(cmapsize) +  /* CMAP size colormap */
        lengthOfTextChunks();

    pm_writebiglong(stdout, ID_FORM);
    pm_writebiglong(stdout, formsize);
    pm_writebiglong(stdout, ID_ILBM);

    writeBmhd(0, 0, 0);
    writeTextChunks();
    writeCmap(colorrow, colors, maxval);
}



/************ HAM ************/



typedef struct {
    long count;
    pixval r, g, b;
} hentry;



#ifndef LITERAL_FN_DEF_MATCH
static qsort_comparison_fn hcmp;
#endif

static int
hcmp(const void * const a,
     const void * const b) {

    /* reverse sort, highest count first */

    const hentry * const vaP = a;
    const hentry * const vbP = b;

    return(vbP->count - vaP->count);  
}



static pixel *
computeHamCmap(int   const cols,
               int   const rows,
               int   const maxval,
               int   const maxcolors,
               int * const colorsP,
               int   const hbits) {

    int colors;
    hentry *hmap;
    pixel *cmap;
    pixval hmaxval;
    int i, r, g, b, col, row, *htable;
    unsigned long dist, maxdist;

    pm_message("initializing HAM colormap...");

    colors = 1<<(3*hbits);
    MALLOCARRAY(hmap, colors);
    if (hmap == NULL)
        pm_error("Unable to allocate memory for HAM colormap.");
    hmaxval = pm_bitstomaxval(hbits);

    i = 0;
    for( r = 0; r <= hmaxval; r++ ) {
        for( g = 0; g <= hmaxval; g++ ) {
            for( b = 0; b <= hmaxval; b++ ) {
                hmap[i].r = r; hmap[i].g = g; hmap[i].b = b;
                hmap[i].count = 0;
                i++;
            }
        }
    }

    htable = makeValTable(maxval, hmaxval);
    for( row = 0; row < rows; row++ ) {
        unsigned int col;
        for( col = 0; col < cols; ++col) {
            pixel const p = pixels[row][col];
            pixval const r = PPM_GETR(p);
            pixval const g = PPM_GETG(p);
            pixval const b = PPM_GETB(p);
            i = (htable[r]<<(2*hbits)) + (htable[g]<<hbits) + htable[b];
            hmap[i].count++;
        }
    }
    free(htable);

    qsort((void *)hmap, colors, sizeof(hentry), hcmp);
    for( i = colors-1; i >= 0; i-- ) {
        if( hmap[i].count )
            break;
    }
    colors = i+1;

    if( colors > maxcolors ) {
        pm_message("selecting HAM colormap from %d colors...", colors);
        for( maxdist = 1; ; maxdist++ ) {
            for( col = colors-1; col > 0; col-- ) {
                r = hmap[col].r; g = hmap[col].g; b = hmap[col].b;
                for( i = 0; i < col; i++ ) {
                    register int tmp;

                    tmp = hmap[i].r - r; dist = tmp * tmp;
                    tmp = hmap[i].g - g; dist += tmp * tmp;
                    tmp = hmap[i].b - b; dist += tmp * tmp;

                    if( dist <= maxdist ) {
                        unsigned int sum = hmap[i].count + hmap[col].count;

                        hmap[i].r = ROUNDDIV(hmap[i].r * hmap[i].count + 
                                             r * hmap[col].count, sum);
                        hmap[i].g = ROUNDDIV(hmap[i].g * hmap[i].count + 
                                             g * hmap[col].count, sum);
                        hmap[i].b = ROUNDDIV(hmap[i].b * hmap[i].count + 
                                             b * hmap[col].count, sum);
                        hmap[i].count = sum;

                        hmap[col] = hmap[i];    /* temp store */
                        for( tmp = i-1; 
                             tmp >= 0 && hmap[tmp].count < hmap[col].count; 
                             tmp-- )
                            hmap[tmp+1] = hmap[tmp];
                        hmap[tmp+1] = hmap[col];

                        for( tmp = col; tmp < colors-1; tmp++ )
                            hmap[tmp] = hmap[tmp+1];
                        if( --colors <= maxcolors )
                            goto out;
                        break;
                    }
                }
            }
#ifdef DEBUG
            pm_message("\tmaxdist=%ld: %d colors left", maxdist, colors);
#endif
        }
    }
out:
    pm_message("%d colors in HAM colormap", colors);

    cmap = ppm_allocrow(colors);
    *colorsP = colors;

    for( i = 0; i < colors; i++ ) {
        r = hmap[i].r; g = hmap[i].g; b = hmap[i].b;
        PPM_ASSIGN(cmap[i], r, g, b);
    }

    ppm_freerow(hmap);
    return cmap;
}



static void
writeBodyRows(void) {

    bodyblock *b;
    int i;
    long total = 0;

    for( b = &firstblock; b != NULL; b = b->next ) {
        for( i = 0; i < b->used; i++ ) {
            writeBytes(b->row[i], b->len[i]);
            total += b->len[i];
        }
    }
    if( ODD(total) )
        putByte(0);
}



static long
doHamBody(FILE *  const ifP,
          FILE *  const ofP,
          int     const cols,
          int     const rows,
          pixval  const maxval,
          pixval  const hammaxval,
          int     const nPlanes,
          pixel * const colormap,
          int     const colors) {

    int col, row, i;
    rawtype *raw_rowbuf;
    ppm_fs_info *fi = NULL;
    colorhash_table cht, cht2;
    long bodysize = 0;
    int *itoh;      /* table image -> ham */
    int usehash = 1;
    int colbits;
    int hamcode_red, hamcode_green, hamcode_blue;

    MALLOCARRAY_NOFAIL(raw_rowbuf, cols);

    cht = ppm_colorrowtocolorhash(colormap, colors);
    cht2 = ppm_alloccolorhash();
    colbits = pm_maxvaltobits(hammaxval);

    hamcode_red   = HAMCODE_RED << colbits;
    hamcode_green = HAMCODE_GREEN << colbits;
    hamcode_blue  = HAMCODE_BLUE << colbits;

    itoh = makeValTable(maxval, hammaxval);

    if( floyd )
        fi = ppm_fs_init(cols, maxval, 0);

    for( row = 0; row < rows; row++ ) {
        int noprev;
        int spr, spg, spb;   /* scaled values of previous pixel */
        int upr, upg, upb;   /* unscaled values of previous pixel, for floyd */
        pixel *prow;

        noprev = 1;
        prow = nextPixrow(ifP, row);
        for( col = ppm_fs_startrow(fi, prow); 
             col < cols; 
             col = ppm_fs_next(fi, col) ) {

            pixel const p = prow[col];

            /* unscaled values of current pixel */
            pixval const ur = PPM_GETR(p);
            pixval const ug = PPM_GETG(p);
            pixval const ub = PPM_GETB(p);

            /* scaled values of current pixel */
            int const sr = itoh[ur];
            int const sg = itoh[ug];
            int const sb = itoh[ub];

            i = ppm_lookupcolor(cht, &p);
            if( i == -1 ) { /* no matching color in cmap, find closest match */
                int ucr, ucg, ucb;  /* unscaled values of colormap entry */

                if(  hammapmode == HAMMODE_GRAY ) {
                    if( maxval <= 255 ) 
                        /* Use fast approximation to 
                           0.299 r + 0.587 g + 0.114 b. */
                        i = (int)ppm_fastlumin(p);
                    else 
                        /* Can't use fast approximation, 
                           so fall back on floats. 
                        */
                        i = (int)(PPM_LUMIN(p) + 0.5); 
                            /* -IUW added '+ 0.5' */
                    i = itoh[i];
                }
                else {
                    i = ppm_lookupcolor(cht2, &p);
                    if( i == -1 ) {
                        i = ppm_findclosestcolor(colormap, colors, &p);
                        if( usehash ) {
                            if( ppm_addtocolorhash(cht2, &p, i) < 0 ) {
                                pm_message("out of memory "
                                           "adding to hash table, "
                                           "proceeding without it");
                                usehash = 0;
                            }
                        }
                    }
                }
                ucr = PPM_GETR(colormap[i]); 
                ucg = PPM_GETG(colormap[i]); 
                ucb = PPM_GETB(colormap[i]);

                if( noprev ) {  /* no previous pixel, must use colormap */
                    raw_rowbuf[col] = i;    /* + (HAMCODE_CMAP << colbits) */
                    upr = ucr;          upg = ucg;          upb = ucb;
                    spr = itoh[upr];    spg = itoh[upg];    spb = itoh[upb];
                    noprev = 0;
                } else {
                    register long di, dr, dg, db;
                    int scr, scg, scb;   /* scaled values of colormap entry */

                    scr = itoh[ucr]; scg = itoh[ucg]; scb = itoh[ucb];

                    /* compute distances for the four options */
#if 1
                    dr = abs(sg - spg) + abs(sb - spb);
                    dg = abs(sr - spr) + abs(sb - spb);
                    db = abs(sr - spr) + abs(sg - spg);
                    di = abs(sr - scr) + abs(sg - scg) + abs(sb - scb);
#else
                    dr = (sg - spg)*(sg - spg) + (sb - spb)*(sb - spb);
                    dg = (sr - spr)*(sr - spr) + (sb - spb)*(sb - spb);
                    db = (sr - spr)*(sr - spr) + (sg - spg)*(sg - spg);
                    di = (sr - scr)*(sr - scr) + (sg - scg)*(sg - scg) +
                        (sb - scb)*(sb - scb);
#endif

                    if( di <= dr && di <= dg && di <= db ) {    
                        /* prefer colormap lookup */
                        raw_rowbuf[col] = i; 
                        upr = ucr;  upg = ucg;  upb = ucb;
                        spr = scr;  spg = scg;  spb = scb;
                    }
                    else
                    if( db <= dr && db <= dg ) {
                        raw_rowbuf[col] = sb + hamcode_blue; 
                        spb = sb;
                        upb = ub;
                    }
                    else
                    if( dr <= dg ) {
                        raw_rowbuf[col] = sr + hamcode_red;  
                        spr = sr;
                        upr = ur;
                    }
                    else {
                        raw_rowbuf[col] = sg + hamcode_green;
                        spg = sg;
                        upg = ug;
                    }
                }
            }
            else {  /* prefect match in cmap */
                raw_rowbuf[col] = i;    /* + (HAMCODE_CMAP << colbits) */
                upr = PPM_GETR(colormap[i]); 
                upg = PPM_GETG(colormap[i]); 
                upb = PPM_GETB(colormap[i]);
                spr = itoh[upr];            
                spg = itoh[upg];            
                spb = itoh[upb];
            }
            ppm_fs_update3(fi, col, upr, upg, upb);
        }
        bodysize += encodeRow(ofP, raw_rowbuf, cols, nPlanes);
        if( maskmethod == mskHasMask )
            bodysize += encodeMaskrow(ofP, raw_rowbuf, cols);
        ppm_fs_endrow(fi);
    }
    if( ofP && ODD(bodysize) )
        putByte(0);

    free(itoh);

    /* clean up */
    free(raw_rowbuf);
    ppm_fs_free(fi);

    return bodysize;
}



static void
ppmToHam(FILE *  const ifP,
         int     const cols,
         int     const rows,
         int     const maxval,
         pixel * const colormapArg,
         int     const colorsArg,
         int     const cmapmaxvalArg,
         int     const hamplanes) {

    int hamcolors, nPlanes, i, hammaxval;
    long oldsize, bodysize, formsize, cmapsize;
    int * table;
    int colors;
    pixel * colormap;
    int cmapmaxval;

    table = NULL;  /* initial value */
    colors = colorsArg;  /* initial value*/
    colormap = colormapArg;  /* initial value */
    cmapmaxval = cmapmaxvalArg;  /* initial value */

    if( maskmethod == mskHasTransparentColor ) {
        pm_message("masking method '%s' not usable with HAM - "
                   "using '%s' instead",
                   mskNAME[mskHasTransparentColor], mskNAME[mskHasMask]);
        maskmethod = mskHasMask;
    }

    hamcolors = 1 << (hamplanes-2);
    hammaxval = pm_bitstomaxval(hamplanes-2);

    if( colors == 0 ) {
        /* no colormap, make our own */
        switch( hammapmode ) {
            case HAMMODE_GRAY:
                colors = hamcolors;
                MALLOCARRAY_NOFAIL(colormap, colors);
                table = makeValTable(hammaxval, MAXCOLVAL);
                for( i = 0; i < colors; i++ )
                    PPM_ASSIGN(colormap[i], table[i], table[i], table[i]);
                free(table);
                cmapmaxval = MAXCOLVAL;
                break;
            case HAMMODE_FIXED: {
                int entries, val;
                double step;

                /* generate a colormap of 7 "rays" in an RGB color cube:
                        r, g, b, r+g, r+b, g+b, r+g+b
                   we need one colormap entry for black, so the number of
                   entries per ray is (maxcolors-1)/7 */

                entries = (hamcolors-1)/7;
                colors = 7*entries+1;
                MALLOCARRAY_NOFAIL(colormap, colors);
                step = (double)MAXCOLVAL / (double)entries;

                PPM_ASSIGN(colormap[0], 0, 0, 0);
                for( i = 1; i <= entries; i++ ) {
                    val = (int)((double)i * step);
                    PPM_ASSIGN(colormap[          i], val,   0,   0); /* r */
                    PPM_ASSIGN(colormap[  entries+i],   0, val,   0); /* g */
                    PPM_ASSIGN(colormap[2*entries+i],   0,   0, val); /* b */
                    PPM_ASSIGN(colormap[3*entries+i], val, val,   0); /* r+g */
                    PPM_ASSIGN(colormap[4*entries+i], val,   0, val); /* r+b */
                    PPM_ASSIGN(colormap[5*entries+i],   0, val, val); /* g+b */
                    PPM_ASSIGN(colormap[6*entries+i], val, val, val); /*r+g+b*/
                }
                cmapmaxval = MAXCOLVAL;
            }
            break;
            case HAMMODE_RGB4:
                colormap = computeHamCmap(cols, rows, maxval, hamcolors, 
                                          &colors, 4);
                cmapmaxval = 15;
                break;
            case HAMMODE_RGB5:
                colormap = computeHamCmap(cols, rows, maxval, 
                                          hamcolors, &colors, 5);
                cmapmaxval = 31;
                break;
            default:
                pm_error("ppm_to_ham(): unknown hammapmode - can't happen");
        }
    }
    else {
        hammapmode = HAMMODE_MAPFILE;
        if( colors > hamcolors ) {
            pm_message("colormap too large - using first %d colors", 
                       hamcolors);
            colors = hamcolors;
        }
    }

    if( cmapmaxval != maxval ) {
        int i, *table;
        pixel *newcmap;

        newcmap = ppm_allocrow(colors);
        table = makeValTable(cmapmaxval, maxval);
        for( i = 0; i < colors; i++ )
            PPM_ASSIGN(newcmap[i], 
                       table[PPM_GETR(colormap[i])], 
                       table[PPM_GETG(colormap[i])], 
                       table[PPM_GETB(colormap[i])]);
        free(table);
        ppm_freerow(colormap);
        colormap = newcmap;
    }
    if( sortcmap )
        ppm_sortcolorrow(colormap, colors, PPM_STDSORT);

    nPlanes = hamplanes;
    cmapsize = colors * 3;

    bodysize = oldsize = rows * TOTALPLANES(nPlanes) * RowBytes(cols);
    if( DO_COMPRESS ) {
        bodysize = doHamBody(ifP, NULL, cols, rows, maxval, 
                               hammaxval, nPlanes, colormap, colors);
        /*bodysize = doHamBody(ifP, NULL, cols, 
          rows, maxval, hammaxval, nPlanes, colbits, nocolor);*/
        if( bodysize > oldsize )
            pm_message("warning - %s compression increases BODY size "
                       "by %ld%%", 
                       cmpNAME[compmethod], 100*(bodysize-oldsize)/oldsize);
        else
            pm_message("BODY compression (%s): %ld%%", 
                       cmpNAME[compmethod], 100*(oldsize-bodysize)/oldsize);
    }


    formsize =
        4 +                                 /* ILBM */
        4 + 4 + BitMapHeaderSize +          /* BMHD size header */
        4 + 4 + CAMGChunkSize +             /* CAMG size viewportmodes */
        4 + 4 + cmapsize + PAD(cmapsize) +  /* CMAP size colormap */
        4 + 4 + bodysize + PAD(bodysize) +  /* BODY size data */
        lengthOfTextChunks();

    pm_writebiglong(stdout, ID_FORM);
    pm_writebiglong(stdout, formsize);
    pm_writebiglong(stdout, ID_ILBM);

    writeBmhd(cols, rows, nPlanes);
    writeTextChunks();
    writeCamg();       /* HAM requires CAMG chunk */
    writeCmap(colormap, colors, maxval);

    /* write body */
    pm_writebiglong(stdout, ID_BODY);
    pm_writebiglong(stdout, bodysize);
    if( DO_COMPRESS )
        writeBodyRows();
    else
        doHamBody(ifP, stdout, cols, rows, maxval, hammaxval, 
                  nPlanes, colormap, colors);
}



/************ deep (24-bit) ************/



static long
doDeepBody(FILE * const ifP,
           FILE * const ofP,
           int    const cols,
           int    const rows,
           pixval const maxval, 
           int    const bitspercolor) {

    int row, col;
    pixel *pP;
    int *table = NULL;
    long bodysize = 0;
    rawtype *redbuf, *greenbuf, *bluebuf;
    int newmaxval;

    MALLOCARRAY_NOFAIL(redbuf,   cols);
    MALLOCARRAY_NOFAIL(greenbuf, cols);
    MALLOCARRAY_NOFAIL(bluebuf,  cols);

    newmaxval = pm_bitstomaxval(bitspercolor);
    if( maxval != newmaxval ) {
        pm_message("maxval is not %d - automatically rescaling colors", 
                   newmaxval);
        table = makeValTable(maxval, newmaxval);
    }

    for( row = 0; row < rows; row++ ) {
        pP = nextPixrow(ifP, row);
        if( table ) {
            for( col = 0; col < cols; col++, pP++ ) {
                redbuf[col]     = table[PPM_GETR(*pP)];
                greenbuf[col]   = table[PPM_GETG(*pP)];
                bluebuf[col]    = table[PPM_GETB(*pP)];
            }
        }
        else {
            for( col = 0; col < cols; col++, pP++ ) {
                redbuf[col]     = PPM_GETR(*pP);
                greenbuf[col]   = PPM_GETG(*pP);
                bluebuf[col]    = PPM_GETB(*pP);
            }
        }
        bodysize += encodeRow(ofP, redbuf,   cols, bitspercolor);
        bodysize += encodeRow(ofP, greenbuf, cols, bitspercolor);
        bodysize += encodeRow(ofP, bluebuf,  cols, bitspercolor);
        if( maskmethod == mskHasMask )
            bodysize += encodeMaskrow(ofP, redbuf, cols);
    }
    if( ofP && ODD(bodysize) )
        putByte(0);

    /* clean up */
    if( table )
        free(table);
    free(redbuf);
    free(greenbuf);
    free(bluebuf);

    return bodysize;
}



static void
ppmToDeep(FILE * const ifP,
          int    const cols,
          int    const rows,
          int    const maxval,
          int    const bitspercolor) {

    int nPlanes;
    long bodysize, oldsize, formsize;

    if( maskmethod == mskHasTransparentColor ) {
        pm_message("masking method '%s' not usable with deep ILBM - "
                   "using '%s' instead",
                    mskNAME[mskHasTransparentColor], mskNAME[mskHasMask]);
        maskmethod = mskHasMask;
    }

    nPlanes = 3*bitspercolor;

    bodysize = oldsize = rows * TOTALPLANES(nPlanes) * RowBytes(cols);
    if( DO_COMPRESS ) {
        bodysize = doDeepBody(ifP, NULL, cols, rows, maxval, bitspercolor);
        if( bodysize > oldsize )
            pm_message("warning - %s compression increases BODY size by %ld%%",
                       cmpNAME[compmethod], 100*(bodysize-oldsize)/oldsize);
        else
            pm_message("BODY compression (%s): %ld%%", 
                       cmpNAME[compmethod], 100*(oldsize-bodysize)/oldsize);
    }

    formsize =
        4 +                                 /* ILBM */
        4 + 4 + BitMapHeaderSize +          /* BMHD size header */
        4 + 4 + bodysize + PAD(bodysize) +  /* BODY size data */
        lengthOfTextChunks();
    if( gen_camg )
        formsize += 4 + 4 + CAMGChunkSize;  /* CAMG size viewportmodes */

    pm_writebiglong(stdout, ID_FORM);
    pm_writebiglong(stdout, formsize);
    pm_writebiglong(stdout, ID_ILBM);

    writeBmhd(cols, rows, nPlanes);
    writeTextChunks();
    if( gen_camg )
        writeCamg();

    /* write body */
    pm_writebiglong(stdout, ID_BODY);
    pm_writebiglong(stdout, bodysize);
    if( DO_COMPRESS )
        writeBodyRows();
    else
        doDeepBody(ifP, stdout, cols, rows, maxval, bitspercolor);
}



/************ direct color ************/



static long
doDcolBody(FILE *        const ifP,
           FILE *        const ofP,
           int           const cols,
           int           const rows,
           pixval        const maxval, 
           DirectColor * const dcol) {

    int row, col;
    pixel *pP;
    long bodysize = 0;
    rawtype *redbuf, *greenbuf, *bluebuf;
    int *redtable, *greentable, *bluetable;

    MALLOCARRAY_NOFAIL(redbuf,   cols);
    MALLOCARRAY_NOFAIL(greenbuf, cols);
    MALLOCARRAY_NOFAIL(bluebuf,  cols);

    redtable   = makeValTable(maxval, pm_bitstomaxval(dcol->r));
    greentable = makeValTable(maxval, pm_bitstomaxval(dcol->g));
    bluetable  = makeValTable(maxval, pm_bitstomaxval(dcol->b));

    for( row = 0; row < rows; row++ ) {
        pP = nextPixrow(ifP, row);
        for( col = 0; col < cols; col++, pP++ ) {
            redbuf[col]   = redtable[PPM_GETR(*pP)];
            greenbuf[col] = greentable[PPM_GETG(*pP)];
            bluebuf[col]  = bluetable[PPM_GETB(*pP)];
        }
        bodysize += encodeRow(ofP, redbuf,   cols, dcol->r);
        bodysize += encodeRow(ofP, greenbuf, cols, dcol->g);
        bodysize += encodeRow(ofP, bluebuf,  cols, dcol->b);
        if( maskmethod == mskHasMask )
            bodysize += encodeMaskrow(ofP, redbuf, cols);
    }
    if( ofP && ODD(bodysize) )
        putByte(0);

    /* clean up */
    free(redtable);
    free(greentable);
    free(bluetable);
    free(redbuf);
    free(greenbuf);
    free(bluebuf);

    return bodysize;
}



static void
ppmToDcol(FILE *        const ifP,
          int           const cols,
          int           const rows,
          int           const maxval,
          DirectColor * const dcol) {

    int nPlanes;
    long bodysize, oldsize, formsize;

    if( maskmethod == mskHasTransparentColor ) {
        pm_message("masking method '%s' not usable with deep ILBM - "
                   "using '%s' instead",
                   mskNAME[mskHasTransparentColor], mskNAME[mskHasMask]);
        maskmethod = mskHasMask;
    }

    nPlanes = dcol->r + dcol->g + dcol->b;

    bodysize = oldsize = rows * TOTALPLANES(nPlanes) * RowBytes(cols);
    if( DO_COMPRESS ) {
        bodysize = doDcolBody(ifP, NULL, cols, rows, maxval, dcol);
        if( bodysize > oldsize )
            pm_message("warning - %s compression increases BODY size by %ld%%",
                       cmpNAME[compmethod], 
                       100*(bodysize-oldsize)/oldsize);
        else
            pm_message("BODY compression (%s): %ld%%", cmpNAME[compmethod], 
                       100*(oldsize-bodysize)/oldsize);
    }


    formsize =
        4 +                                 /* ILBM */
        4 + 4 + BitMapHeaderSize +          /* BMHD size header */
        4 + 4 + DirectColorSize +           /* DCOL size dcol */
        4 + 4 + bodysize + PAD(bodysize) +  /* BODY size data */
        lengthOfTextChunks();
    if( gen_camg )
        formsize += 4 + 4 + CAMGChunkSize;  /* CAMG size viewportmodes */

    pm_writebiglong(stdout, ID_FORM);
    pm_writebiglong(stdout, formsize);
    pm_writebiglong(stdout, ID_ILBM);

    writeBmhd(cols, rows, nPlanes);
    writeTextChunks();

    pm_writebiglong(stdout, ID_DCOL);
    pm_writebiglong(stdout, DirectColorSize);
    putByte(dcol->r);
    putByte(dcol->g);
    putByte(dcol->b);
    putByte(0);    /* pad */

    if( gen_camg )
        writeCamg();

    /* write body */
    pm_writebiglong(stdout, ID_BODY);
    pm_writebiglong(stdout, bodysize);
    if( DO_COMPRESS )
        writeBodyRows();
    else
        doDcolBody(ifP, stdout, cols, rows, maxval, dcol);
}



/************ normal colormapped ************/



static long
doStdBody(FILE *  const ifP,
          FILE *  const ofP,
          int     const cols,
          int     const rows,
          pixval  const maxval,
          pixel * const colormap,
          int     const colors,
          int     const nPlanes) {

    int row, col, i;
    pixel *pP;
    rawtype *raw_rowbuf;
    ppm_fs_info *fi = NULL;
    long bodysize = 0;
    int usehash = 1;
    colorhash_table cht;

    MALLOCARRAY_NOFAIL(raw_rowbuf, cols);
    cht = ppm_colorrowtocolorhash(colormap, colors);
    if( floyd )
        fi = ppm_fs_init(cols, maxval, FS_ALTERNATE);

    for( row = 0; row < rows; row++ ) {
        pixel *prow;
        prow = nextPixrow(ifP, row);

        for( col = ppm_fs_startrow(fi, prow); 
             col < cols; 
             col = ppm_fs_next(fi, col) ) {
            pP = &prow[col];

            if( maskmethod == mskHasTransparentColor && 
                maskrow[col] == PBM_WHITE )
                i = transpIndex;
            else {
                /* Check hash table to see if we have already matched
                   this color. 
                */
                i = ppm_lookupcolor(cht, pP);
                if( i == -1 ) {
                    i = ppm_findclosestcolor(colormap, colors, pP);    
                        /* No; search colormap for closest match. */
                    if( usehash ) {
                        if( ppm_addtocolorhash(cht, pP, i) < 0 ) {
                            pm_message("out of memory adding to hash table, "
                                       "proceeding without it");
                            usehash = 0;
                        }
                    }
                }
            }
            raw_rowbuf[col] = i;
            ppm_fs_update(fi, col, &colormap[i]);
        }
        bodysize += encodeRow(ofP, raw_rowbuf, cols, nPlanes);
        if( maskmethod == mskHasMask )
            bodysize += encodeMaskrow(ofP, raw_rowbuf, cols);
        ppm_fs_endrow(fi);
    }
    if( ofP && ODD(bodysize) )
        putByte(0);

    /* clean up */
    ppm_freecolorhash(cht);
    free(raw_rowbuf);
    ppm_fs_free(fi);

    return bodysize;
}



static void
ppmToStd(FILE *  const ifP,
         int     const cols,
         int     const rows,
         int     const maxval,
         pixel * const colormapArg,
         int     const colorsArg,
         int     const cmapmaxvalArg, 
         int     const maxcolors,
         int     const nPlanes) {

    long formsize, cmapsize, bodysize, oldsize;

    int colors;
    pixel * colormap;
    int cmapmaxval;

    colors = colorsArg;  /* initial value */
    colormap = colormapArg;  /* initial value */
    cmapmaxval = cmapmaxvalArg;  /* initial value */

    if( maskmethod == mskHasTransparentColor ) {
        if( transpColor ) {
            transpIndex = 
                ppm_addtocolorrow(colormap, &colors, maxcolors, transpColor);
        }
        else
        if( colors < maxcolors )
            transpIndex = colors;

        if( transpIndex < 0 ) {
            pm_message("too many colors for masking method '%s' - "
                       "using '%s' instead",
                       mskNAME[mskHasTransparentColor], mskNAME[mskHasMask]);
            maskmethod = mskHasMask;
        }
    }

    if( cmapmaxval != maxval ) {
        int i, *table;
        pixel *newcmap;

        newcmap = ppm_allocrow(colors);
        table = makeValTable(cmapmaxval, maxval);
        for (i = 0; i < colors; ++i)
            PPM_ASSIGN(newcmap[i], 
                       table[PPM_GETR(colormap[i])], 
                       table[PPM_GETG(colormap[i])], 
                       table[PPM_GETB(colormap[i])]);
        free(table);
        colormap = newcmap;
    }
    if( sortcmap )
        ppm_sortcolorrow(colormap, colors, PPM_STDSORT);

    bodysize = oldsize = rows * TOTALPLANES(nPlanes) * RowBytes(cols);
    if( DO_COMPRESS ) {
        bodysize = doStdBody(ifP, NULL, cols, rows, maxval, colormap, 
                             colors, nPlanes);
        if( bodysize > oldsize )
            pm_message("warning - %s compression increases BODY size by %ld%%",
                       cmpNAME[compmethod], 100*(bodysize-oldsize)/oldsize);
        else
            pm_message("BODY compression (%s): %ld%%", 
                       cmpNAME[compmethod], 100*(oldsize-bodysize)/oldsize);
    }

    cmapsize = colors * 3;

    formsize =
        4 +                                 /* ILBM */
        4 + 4 + BitMapHeaderSize +          /* BMHD size header */
        4 + 4 + cmapsize + PAD(cmapsize) +  /* CMAP size colormap */
        4 + 4 + bodysize + PAD(bodysize) +  /* BODY size data */
        lengthOfTextChunks();
    if( gen_camg )
        formsize += 4 + 4 + CAMGChunkSize;  /* CAMG size viewportmodes */

    pm_writebiglong(stdout, ID_FORM);
    pm_writebiglong(stdout, formsize);
    pm_writebiglong(stdout, ID_ILBM);

    writeBmhd(cols, rows, nPlanes);
    writeTextChunks();
    if( gen_camg )
        writeCamg();
    writeCmap(colormap, colors, maxval);

    /* write body */
    pm_writebiglong(stdout, ID_BODY);
    pm_writebiglong(stdout, bodysize);
    if( DO_COMPRESS )
        writeBodyRows();
    else
        doStdBody(ifP, stdout, cols, rows, maxval, colormap, colors, nPlanes);
}



/************ RGB8 ************/



static void
ppmToRgb8(FILE * const ifP,
          int    const cols,
          int    const rows,
          int    const maxval) {

    long bodysize, oldsize, formsize;
    pixel *pP;
    int *table = NULL;
    int row, col1, col2, compr_len, len;
    unsigned char *compr_row;

    maskmethod = 0;     /* no masking - RGB8 uses genlock bits */
    compmethod = 4;     /* RGB8 files are always compressed */
    MALLOCARRAY_NOFAIL(compr_row, cols * 4);

    if( maxval != 255 ) {
        pm_message("maxval is not 255 - automatically rescaling colors");
        table = makeValTable(maxval, 255);
    }

    oldsize = cols * rows * 4;
    bodysize = 0;
    for( row = 0; row < rows; row++ ) {
        pP = nextPixrow(ifP, row);
        compr_len = 0;
        for( col1 = 0; col1 < cols; col1 = col2 ) {
            col2 = col1 + 1;
            if( maskrow ) {
                while( col2 < cols && PPM_EQUAL(pP[col1], pP[col2]) && 
                       maskrow[col1] == maskrow[col2] )
                    col2++;
            }
            else {
                while( col2 < cols && PPM_EQUAL(pP[col1], pP[col2]) )
                    col2++;
            }
            len = col2 - col1;
            while( len ) {
                int count;
                count = (len > 127 ? 127 : len);
                len -= count;
                if( table ) {
                    compr_row[compr_len++] = table[PPM_GETR(pP[col1])];
                    compr_row[compr_len++] = table[PPM_GETG(pP[col1])];
                    compr_row[compr_len++] = table[PPM_GETB(pP[col1])];
                }
                else {
                    compr_row[compr_len++] = PPM_GETR(pP[col1]);
                    compr_row[compr_len++] = PPM_GETG(pP[col1]);
                    compr_row[compr_len++] = PPM_GETB(pP[col1]);
                }
                compr_row[compr_len] = count;
                if( maskrow && maskrow[col1] == PBM_WHITE )
                    compr_row[compr_len] |= 1<<7;     /* genlock bit */
                ++compr_len;
            }
        }
        storeBodyrow(compr_row, compr_len);
        bodysize += compr_len;
    }

    pm_message("BODY compression: %ld%%", 100*(oldsize-bodysize)/oldsize);

    formsize =
        4 +                                 /* RGB8 */
        4 + 4 + BitMapHeaderSize +          /* BMHD size header */
        4 + 4 + CAMGChunkSize +             /* CAMG size viewportmode */
        4 + 4 + bodysize + PAD(bodysize) +  /* BODY size data */
        lengthOfTextChunks();

    /* write header */
    pm_writebiglong(stdout, ID_FORM);
    pm_writebiglong(stdout, formsize);
    pm_writebiglong(stdout, ID_RGB8);

    writeBmhd(cols, rows, 25);
    writeTextChunks();
    writeCamg();               /* RGB8 requires CAMG chunk */

    pm_writebiglong(stdout, ID_BODY);
    pm_writebiglong(stdout, bodysize);
    writeBodyRows();
}



/************ RGBN ************/



static void
ppmToRgbn(FILE * const ifP,
          int    const cols,
          int    const rows,
          int    const maxval) {

    long bodysize, oldsize, formsize;
    pixel *pP;
    int *table = NULL;
    int row, col1, col2, compr_len, len;
    unsigned char *compr_row;

    maskmethod = 0;     /* no masking - RGBN uses genlock bits */
    compmethod = 4;     /* RGBN files are always compressed */
    MALLOCARRAY_NOFAIL(compr_row, cols * 2);

    if( maxval != 15 ) {
        pm_message("maxval is not 15 - automatically rescaling colors");
        table = makeValTable(maxval, 15);
    }

    oldsize = cols * rows * 2;
    bodysize = 0;
    for( row = 0; row < rows; row++ ) {
        pP = nextPixrow(ifP, row);
        compr_len = 0;
        for( col1 = 0; col1 < cols; col1 = col2 ) {
            col2 = col1 + 1;
            if( maskrow ) {
                while( col2 < cols && PPM_EQUAL(pP[col1], pP[col2]) && 
                       maskrow[col1] == maskrow[col2] )
                    col2++;
            }
            else {
                while( col2 < cols && PPM_EQUAL(pP[col1], pP[col2]) )
                    col2++;
            }
            len = col2 - col1;
            while( len ) {
                int count;
                count = (len > 65535 ? 65535 : len);
                len -= count;
                if( table ) {
                    compr_row[compr_len]  = table[PPM_GETR(pP[col1])] << 4;
                    compr_row[compr_len] |= table[PPM_GETG(pP[col1])];
                    ++compr_len;
                    compr_row[compr_len]  = table[PPM_GETB(pP[col1])] << 4;
                }
                else {
                    compr_row[compr_len]  = PPM_GETR(pP[col1]) << 4;
                    compr_row[compr_len] |= PPM_GETG(pP[col1]);
                    ++compr_len;
                    compr_row[compr_len]  = PPM_GETB(pP[col1]) << 4;
                }
                if( maskrow && maskrow[col1] == PBM_WHITE )
                    compr_row[compr_len] |= 1<<3;   /* genlock bit */
                if( count <= 7 )
                    compr_row[compr_len++] |= count;  /* 3 bit repeat count */
                else {
                    ++compr_len;                  /* 3 bit repeat count = 0 */
                    if( count <= 255 )
                        compr_row[compr_len++] = (unsigned char)count;  
                            /* byte repeat count */
                    else {
                        compr_row[compr_len++] = (unsigned char)0;   
                            /* byte repeat count = 0 */
                        compr_row[compr_len++] = (count >> 8) & 0xff; 
                            /* word repeat count MSB */
                        compr_row[compr_len++] = count & 0xff;    
                            /* word repeat count LSB */
                    }
                }
            }
        }
        storeBodyrow(compr_row, compr_len);
        bodysize += compr_len;
    }

    pm_message("BODY compression: %ld%%", 100*(oldsize-bodysize)/oldsize);

    formsize =
        4 +                                 /* RGBN */
        4 + 4 + BitMapHeaderSize +          /* BMHD size header */
        4 + 4 + CAMGChunkSize +             /* CAMG size viewportmode */
        4 + 4 + bodysize + PAD(bodysize) +  /* BODY size data */
        lengthOfTextChunks();

    /* write header */
    pm_writebiglong(stdout, ID_FORM);
    pm_writebiglong(stdout, formsize);
    pm_writebiglong(stdout, ID_RGBN);

    writeBmhd(cols, rows, 13);
    writeTextChunks();
    writeCamg();               /* RGBN requires CAMG chunk */

    pm_writebiglong(stdout, ID_BODY);
    pm_writebiglong(stdout, bodysize);
    writeBodyRows();
}



/************ multipalette ************/



#ifdef ILBM_PCHG
static pixel *ppmslice[2];  /* need 2 for laced ILBMs, else 1 */

void
ppmToPchg() {
/*
    read first slice
    build a colormap from this slice
    select up to <maxcolors> colors
    build colormap from selected colors
    map slice to colormap
    write slice
    while( !finished ) {
        read next slice
        compute distances for each pixel and select up to
            <maxchangesperslice> unused colors in this slice
        modify selected colors to the ones with maximum(?) distance
        map slice to colormap
        write slice
    }


    for HAM use a different mapping:
        compute distance to closest color in colormap
        if( there is no matching color in colormap ) {
            compute distances for the three "modify" cases
            use the shortest distance from the four cases
        }
*/
}
#endif



int
main(int argc, char ** argv) {

    FILE * ifP;
    int argn, rows, cols, format, nPlanes;
    int ifmode, forcemode, maxplanes, fixplanes, mode;
    int hamplanes;
    int deepbits;   /* bits per color component in deep ILBM */
    DirectColor dcol;
#define MAXCOLORS       (1<<maxplanes)
    pixval maxval;
    pixel * colormap;
    int colors = 0;
    pixval cmapmaxval;      /* maxval of colors in cmap */
    const char * mapfile;
    const char * transpname;

    ppm_init(&argc, argv);

    colormap = NULL;  /* initial value */
    ifmode = DEF_IFMODE; forcemode = MODE_NONE;
    maxplanes = DEF_MAXPLANES; fixplanes = 0;
    hamplanes = DEF_HAMPLANES;
    deepbits = DEF_DEEPPLANES;
    dcol.r = dcol.g = dcol.b = DEF_DCOLPLANES;
    mapfile = transpname = NULL;

    argn = 1;
    while( argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0' ) {
        if( pm_keymatch(argv[argn], "-ilbm", 5) ) {
            if( forcemode == MODE_RGB8 || forcemode == MODE_RGBN )
                forcemode = MODE_NONE;
        }
        else
        if( pm_keymatch(argv[argn], "-rgb8", 5) )
            forcemode = MODE_RGB8;
        else
        if( pm_keymatch(argv[argn], "-rgbn", 5) )
            forcemode = MODE_RGBN;
        else
        if( pm_keymatch(argv[argn], "-maxplanes", 4) || 
            pm_keymatch(argv[argn], "-mp", 3) ) {
            if( ++argn >= argc )
                pm_error("-maxplanes requires a value");
            maxplanes = getIntVal(argv[argn], argv[argn-1], 1, MAXPLANES);
            fixplanes = 0;
        }
        else
        if( pm_keymatch(argv[argn], "-fixplanes", 4) || 
            pm_keymatch(argv[argn], "-fp", 3) ) {
            if( ++argn >= argc )
                pm_error("-fixplanes requires a value");
            fixplanes = getIntVal(argv[argn], argv[argn-1], 1, MAXPLANES);
            maxplanes = fixplanes;
        }
        else
        if( pm_keymatch(argv[argn], "-mapfile", 4) ) {
            if( ++argn >= argc )
                pm_error("-mapfile requires a value");
            mapfile = argv[argn];
        }
        else
        if( pm_keymatch(argv[argn], "-mmethod", 3) ) {
            if( ++argn >= argc )
                pm_error("-mmethod requires a value");
            maskmethod = getMaskType(argv[argn]);
            switch( maskmethod ) {
                case mskNone:
                case mskHasMask:
                case mskHasTransparentColor:
                    break;
                default:
                    pm_error("This program does not know how to handle "
                             "masking method '%s'", 
                             mskNAME[maskmethod]);
            }
        }
        else
        if( pm_keymatch(argv[argn], "-maskfile", 4) ) {
            if( ++argn >= argc )
                pm_error("-maskfile requires a value");
            maskfile = pm_openr(argv[argn]);
            if( maskmethod == mskNone )
                maskmethod = mskHasMask;
        }
        else
        if( pm_keymatch(argv[argn], "-transparent", 3) ) {
            if( ++argn >= argc )
                pm_error("-transparent requires a value");
            transpname = argv[argn];
            if( maskmethod == mskNone )
                maskmethod = mskHasTransparentColor;
        }
        else
        if( pm_keymatch(argv[argn], "-sortcmap", 5) )
            sortcmap = 1;
        else
        if( pm_keymatch(argv[argn], "-cmaponly", 3) ) {
            forcemode = MODE_CMAP;
        }
        else
        if( pm_keymatch(argv[argn], "-lace", 2) ) {
            slicesize = 2;
            viewportmodes |= vmLACE;
            gen_camg = 1;
        }
        else
        if( pm_keymatch(argv[argn], "-nolace", 4) ) {
            slicesize = 1;
            viewportmodes &= ~vmLACE;
        }
        else
        if( pm_keymatch(argv[argn], "-hires", 3) ) {
            viewportmodes |= vmHIRES;
            gen_camg = 1;
        }
        else
        if( pm_keymatch(argv[argn], "-nohires", 5) )
            viewportmodes &= ~vmHIRES;
        else
        if( pm_keymatch(argv[argn], "-camg", 5) ) {
            char *tail;
            long value = 0L;

            if( ++argn >= argc )
                pm_error("-camg requires a value");
            value = strtol(argv[argn], &tail, 16);
            if(argv[argn] == tail)
                pm_error("-camg requires a value");
            viewportmodes |= value;
            gen_camg = 1;
        }
        else
        if( pm_keymatch(argv[argn], "-ecs", 2) ) {
            maxplanes = ECS_MAXPLANES;
            hamplanes = ECS_HAMPLANES;
        }
        else
        if( pm_keymatch(argv[argn], "-aga", 3) ) {
            maxplanes = AGA_MAXPLANES;
            hamplanes = AGA_HAMPLANES;
        }
        else
        if( pm_keymatch(argv[argn], "-hamplanes", 5) ) {
            if( ++argn >= argc )
                pm_error("-hamplanes requires a value");
            hamplanes = getIntVal(argv[argn], argv[argn-1], 3, HAMMAXPLANES);
        }
        else
        if( pm_keymatch(argv[argn], "-hambits", 5) ) {
            if( ++argn >= argc )
                pm_usage("-hambits requires a value");
            hamplanes = 
                getIntVal(argv[argn], argv[argn-1], 3, HAMMAXPLANES-2) +2;
        }
        else
        if( pm_keymatch(argv[argn], "-ham6", 5) ) {
            hamplanes = ECS_HAMPLANES;
            forcemode = MODE_HAM;
        }
        else
        if( pm_keymatch(argv[argn], "-ham8", 5) ) {
            hamplanes = AGA_HAMPLANES;
            forcemode = MODE_HAM;
        }
        else
        if( pm_keymatch(argv[argn], "-hammap", 5) ) {
            if( ++argn >= argc )
                pm_error("-hammap requires a value");
            hammapmode = getHammapMode(argv[argn]);
        }
        else
        if( pm_keymatch(argv[argn], "-hamif", 5) )
            ifmode = MODE_HAM;
        else
        if( pm_keymatch(argv[argn], "-nohamif", 7) ) {
            if( ifmode == MODE_HAM )
                ifmode = MODE_NONE;
        }
        else
        if( pm_keymatch(argv[argn], "-hamforce", 4) )
            forcemode = MODE_HAM;
        else
        if( pm_keymatch(argv[argn], "-nohamforce", 6) ) {
            if( forcemode == MODE_HAM )
                forcemode = MODE_NONE;
        }
        else
        if( pm_keymatch(argv[argn], "-24if", 4) ) {
            ifmode = MODE_DEEP;
            deepbits = 8;
        }
        else
        if( pm_keymatch(argv[argn], "-no24if", 6) ) {
            if( ifmode == MODE_DEEP )
                ifmode = MODE_NONE;
        }
        else
        if( pm_keymatch(argv[argn], "-24force", 3) ) {
            forcemode = MODE_DEEP;
            deepbits = 8;
        }
        else
        if( pm_keymatch(argv[argn], "-no24force", 5) ) {
            if( forcemode == MODE_DEEP )
                forcemode = MODE_NONE;
        }
        else
        if( pm_keymatch(argv[argn], "-deepplanes", 6) ) {
            if( ++argn >= argc )
                pm_error("-deepplanes requires a value");
            deepbits = getIntVal(argv[argn], argv[argn-1], 3, 3*MAXPLANES);
            if( deepbits % 3 != 0 )
                pm_error("option \"%s\" argument value must be divisible by 3",
                         argv[argn-1]);
            deepbits /= 3;
        }
        else
        if( pm_keymatch(argv[argn], "-deepbits", 6) ) {
            if( ++argn >= argc )
                pm_error("-deepbits requires a value");
            deepbits = getIntVal(argv[argn], argv[argn-1], 1, MAXPLANES);
        }
        else
        if( pm_keymatch(argv[argn], "-deepif", 6) )
            ifmode = MODE_DEEP;
        else
        if( pm_keymatch(argv[argn], "-nodeepif", 8) ) {
            if( ifmode == MODE_DEEP )
                ifmode = MODE_NONE;
        }
        else
        if( pm_keymatch(argv[argn], "-deepforce", 5) )
            forcemode = MODE_DEEP;
        else
        if( pm_keymatch(argv[argn], "-nodeepforce", 7) ) {
            if( forcemode == MODE_DEEP )
                forcemode = MODE_NONE;
        }
        else
        if( pm_keymatch(argv[argn], "-dcif", 4) )
            ifmode = MODE_DCOL;
        else
        if( pm_keymatch(argv[argn], "-nodcif", 6) ) {
            if( ifmode == MODE_DCOL )
                ifmode = MODE_NONE;
        }
        else
        if( pm_keymatch(argv[argn], "-dcforce", 4) )
            forcemode = MODE_DCOL;
        else
        if( pm_keymatch(argv[argn], "-nodcforce", 6) ) {
            if( forcemode == MODE_DCOL )
                forcemode = MODE_NONE;
        }
        else
        if( pm_keymatch(argv[argn], "-dcbits", 4) || 
            pm_keymatch(argv[argn], "-dcplanes", 4) ) {
            if( argc - argn < 4 )
                pm_error("-dcbits requires 4 arguments");
            dcol.r = getIntVal(argv[argn+1], argv[argn], 1, MAXPLANES);
            dcol.g = getIntVal(argv[argn+2], argv[argn], 1, MAXPLANES);
            dcol.b = getIntVal(argv[argn+3], argv[argn], 1, MAXPLANES);
            argn += 3;
        }
        else
        if( pm_keymatch(argv[argn], "-normal", 4) ) {
            ifmode = forcemode = MODE_NONE;
            compmethod = DEF_COMPRESSION;
        }
        else
        if( pm_keymatch(argv[argn], "-compress", 4) ) {
            compr_force = 1;
            if( compmethod == cmpNone )
#if DEF_COMPRESSION == cmpNone
                    compmethod = cmpByteRun1;
#else
                    compmethod = DEF_COMPRESSION;
#endif
        }
        else
        if( pm_keymatch(argv[argn], "-nocompress", 4) ) {
            compr_force = 0;
            compmethod = cmpNone;
        }
        else
        if( pm_keymatch(argv[argn], "-cmethod", 4) ) {
            if( ++argn >= argc )
                pm_error("-cmethod requires a value");
            compmethod = getComprMethod(argv[argn]);
        }
        else
        if( pm_keymatch(argv[argn], "-floyd", 3) || 
            pm_keymatch(argv[argn], "-fs", 3) )
            floyd = 1;
        else
        if( pm_keymatch(argv[argn], "-nofloyd", 5) || 
            pm_keymatch(argv[argn], "-nofs", 5) )
            floyd = 0;
        else
        if( pm_keymatch(argv[argn], "-annotation", 3) ) {
            if( ++argn >= argc )
                pm_error("-annotation requires a value");
            anno_chunk = argv[argn];
        }
        else
        if( pm_keymatch(argv[argn], "-author", 3) ) {
            if( ++argn >= argc )
                pm_error("-author requires a value");
            auth_chunk = argv[argn];
        }
        else
        if( pm_keymatch(argv[argn], "-copyright", 4) ) {
            if( ++argn >= argc )
                pm_error("-copyright requires a value");
            copyr_chunk = argv[argn];
        }
        else
        if( pm_keymatch(argv[argn], "-name", 3) ) {
            if( ++argn >= argc )
                pm_error("-name requires a value");
            name_chunk = argv[argn];
        }
        else
        if( pm_keymatch(argv[argn], "-text", 3) ) {
            if( ++argn >= argc )
                pm_error("-text requires a value");
            text_chunk = argv[argn];
        }
        else
            pm_error("invalid option: %s", argv[argn]);
        ++argn;
    }

    if( argn < argc ) {
        ifP = pm_openr(argv[argn]);
        ++argn;
    }
    else
        ifP = stdin;

    if( argn != argc )
        pm_error("Program takes no arguments.");

    mode = forcemode;
    switch(forcemode) {
        case MODE_HAM:
            if (hammapmode == HAMMODE_RGB4 || hammapmode == HAMMODE_RGB5)
                initRead(ifP, &cols, &rows, &maxval, &format, 1);
            else
                initRead(ifP, &cols, &rows, &maxval, &format, 0);
            break;
        case MODE_DCOL:
        case MODE_DEEP:
            mapfile = NULL;
            initRead(ifP, &cols, &rows, &maxval, &format, 0);
            break;
        case MODE_RGB8:
            mapfile = NULL;
            initRead(ifP, &cols, &rows, &maxval, &format, 0);
            break;
        case MODE_RGBN:
            mapfile = NULL;
            initRead(ifP, &cols, &rows, &maxval, &format, 0);
            break;
        case MODE_CMAP:
            /* Figure out the colormap. */
            pm_message("computing colormap...");
            colormap = ppm_mapfiletocolorrow(ifP, MAXCOLORS, &colors, 
                                             &cmapmaxval);
            if (colormap == NULL)
                pm_error("too many colors - try doing a 'pnmquant %d'", 
                         MAXCOLORS);
            pm_message("%d colors found", colors);
            break;
        default:
            if (mapfile)
                initRead(ifP, &cols, &rows, &maxval, &format, 0);
            else {
                initRead(ifP, &cols, &rows, &maxval, &format, 1);  
                    /* read file into memory */
                pm_message("computing colormap...");
                colormap = 
                    ppm_computecolorrow(pixels, cols, rows, MAXCOLORS, 
                                        &colors);
                if (colormap) {
                    cmapmaxval = maxval;
                    pm_message("%d colors found", colors);
                    nPlanes = pm_maxvaltobits(colors-1);
                    if (fixplanes > nPlanes)
                        nPlanes = fixplanes;
                } else {  /* too many colors */
                    mode = ifmode;
                    reportTooManyColors(ifmode, maxplanes, hamplanes,
                                           dcol, deepbits );
                }
            }
    }

    if (mapfile) {
        FILE * mapfp;

        pm_message("reading colormap file...");
        mapfp = pm_openr(mapfile);
        colormap = ppm_mapfiletocolorrow(mapfp, MAXCOLORS, &colors, 
                                         &cmapmaxval);
        pm_close(mapfp);
        if (colormap == NULL)
            pm_error("too many colors in mapfile for %d planes", maxplanes);
        if (colors == 0)
            pm_error("empty colormap??");
        pm_message("%d colors found in colormap", colors);
    }

    if (maskmethod != mskNone) {
        if (transpname) {
            MALLOCVAR_NOFAIL(transpColor);
            *transpColor = ppm_parsecolor(transpname, maxval);
        }
        if (maskfile) {
            int maskrows;
            pbm_readpbminit(maskfile, &maskcols, &maskrows, &maskformat);
            if (maskcols < cols || maskrows < rows)
                pm_error("maskfile too small - try scaling it");
            if (maskcols > cols || maskrows > rows)
                pm_message("warning - maskfile larger than image");
        } else
            maskcols = rows;
        maskrow = pbm_allocrow(maskcols);
    }

    if (mode != MODE_CMAP) {
        unsigned int i;
        MALLOCARRAY_NOFAIL(coded_rowbuf, RowBytes(cols));
        for (i = 0; i < RowBytes(cols); ++i)
            coded_rowbuf[i] = 0;
        if (DO_COMPRESS)
            pm_rlenc_allocoutbuf(&compr_rowbuf, RowBytes(cols), PM_RLE_PACKBITS);
    }
    
    switch (mode) {
        case MODE_HAM:
            viewportmodes |= vmHAM;
            ppmToHam(ifP, cols, rows, maxval, 
                     colormap, colors, cmapmaxval, hamplanes);
            break;
        case MODE_DEEP:
            ppmToDeep(ifP, cols, rows, maxval, deepbits);
            break;
        case MODE_DCOL:
            ppmToDcol(ifP, cols, rows, maxval, &dcol);
            break;
        case MODE_RGB8:
            ppmToRgb8(ifP, cols, rows, maxval);
            break;
        case MODE_RGBN:
            ppmToRgbn(ifP, cols, rows, maxval);
            break;
        case MODE_CMAP:
            ppmToCmap(colormap, colors, cmapmaxval);
            break;
        default:
            if (mapfile == NULL)
                floyd = 0;          /* would only slow down conversion */
            ppmToStd(ifP, cols, rows, maxval, colormap, colors, 
                     cmapmaxval, MAXCOLORS, nPlanes);
            break;
    }
    pm_close(ifP);
    return 0;
}



