/* pnmtosgi.c - convert portable anymap to SGI image
**
** Copyright (C) 1994 by Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
**
** Based on the SGI image description v0.9 by Paul Haeberli (paul@sgi.comp)
** Available via ftp from sgi.com:graphics/SGIIMAGESPEC
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
** 29Jan94: first version

** Feb 2010 afu
** Added dimension check to prevent short int from overflowing
*/

#include <assert.h>

#include "pnm.h"
#include "sgi.h"
#include "mallocvar.h"
#include "runlength.h"


/*#define DEBUG*/

typedef uint16_t       ScanElem;
typedef struct {
    ScanElem *  data;
    long        length;
} ScanLine;

#define MAXVAL_BYTE     255
#define MAXVAL_WORD     65535
#define INT16MAX        32767

static char storage = STORAGE_RLE;
static ScanLine * channel[3];
static xel * pnmrow;



#define putByte(b) (void)(putc((unsigned char)(b), stdout))


static void
putBigShort(short const s) {

    if (pm_writebigshort(stdout, s ) == -1)
        pm_error( "write error" );
}



static void
putBigLong(long const l) {

    if (pm_writebiglong( stdout, l ) == -1)
        pm_error( "write error" );
}



static void
putShortAsByte(short const s) {

    putByte((unsigned char)s);
}



static void
writeTable(long *       const table,
           unsigned int const tabsize) {

    unsigned int i;
    unsigned long offset;

    offset = HeaderSize + tabsize * 8;

    for (i = 0; i < tabsize; ++i) {
        putBigLong(offset);
        offset += table[i];
    }
    for (i = 0; i < tabsize; ++i)
        putBigLong(table[i]);
}



static void
writeChannels(unsigned int const cols,
              unsigned int const rows,
              unsigned int const channels,
              void (*put) (short)) {

    unsigned int i;

    for (i = 0; i < channels; ++i) {
        unsigned int row;
        for (row = 0; row < rows; ++row) {
            unsigned int col;
            for (col = 0; col < channel[i][row].length; ++col) {
                (*put)(channel[i][row].data[col]);
            }
            pm_rlenc_freebuf((unsigned char *) channel[i][row].data);
        }
    }
}



static ScanElem *
compress(ScanElem *   const tempArg,
         unsigned int const row,
         unsigned int const rows,
         unsigned int const cols,
         unsigned int const chanNum,
         long *       const table,
         unsigned int const bpc) {
/*----------------------------------------------------------------------------
   Compress a row, putting results in global 'channel' array, in newly
   allocated storage (which Caller must free).

   Except that if the compression is null compression, we make 'channel'
   point to existing storage, which Caller must not free.  Yuck.
-----------------------------------------------------------------------------*/
    ScanElem * retval;

    switch (storage) {
    case STORAGE_VERBATIM:
        channel[chanNum][row].length = cols;
        channel[chanNum][row].data = tempArg;
        MALLOCARRAY_NOFAIL(retval, cols);
        break;
    case STORAGE_RLE: {
        unsigned int const tabrow = chanNum * rows + row;

        unsigned int len;
        size_t lenBytes;
        ScanElem * p;

        pm_rlenc_allocoutbuf((unsigned char **) &p, cols, PM_RLE_SGI16);

        pm_rlenc_compressword(tempArg,(unsigned char *) p, PM_RLE_SGI16,
                              cols, &lenBytes);

        assert((unsigned)lenBytes == lenBytes);
            /* Guaranteed by pm_rlenc_compressword() */

        len = lenBytes / 2;  /* sizeof(ScanElem) */
        channel[chanNum][row].length = len;
        REALLOCARRAY(p, len);   /* reclaim some space */
        if (p == NULL)
            pm_error("realloc failure while reclaiming memory space "
                     "for output");
        channel[chanNum][row].data = p;
        table[tabrow] = len * bpc;
        retval = tempArg;
    } break;
    default:
        pm_error("unknown storage type - can't happen");
    }
    return retval;
}



static long *
buildChannels(FILE *       const ifP,
              unsigned int const cols,
              unsigned int const rows,
              xelval       const maxval,
              int          const format,
              unsigned int const bpc,
              unsigned int const channels) {

    unsigned int row;
    unsigned int sgirow;
    long * table;
    ScanElem * temp;

    if (storage != STORAGE_VERBATIM) {
        MALLOCARRAY_NOFAIL(table, channels * rows);
    } else
        table = NULL;

    MALLOCARRAY_NOFAIL(temp, cols);

    {
        unsigned int i;
        for (i = 0; i < channels; ++i)
            MALLOCARRAY_NOFAIL(channel[i], rows);
    }

    for (row = 0, sgirow = rows-1; row < rows; ++row, --sgirow) {
        pnm_readpnmrow(ifP, pnmrow, cols, maxval, format);
        if (channels == 1) {
            unsigned int col;
            for (col = 0; col < cols; ++col)
                temp[col] = (ScanElem)PNM_GET1(pnmrow[col]);
            temp = compress(temp, sgirow, rows, cols, 0, table, bpc);
        } else {
            unsigned int col;
            for (col = 0; col < cols; ++col)
                temp[col] = (ScanElem)PPM_GETR(pnmrow[col]);
            temp = compress(temp, sgirow, rows, cols, 0, table, bpc);
            for (col = 0; col < cols; ++col)
                temp[col] = (ScanElem)PPM_GETG(pnmrow[col]);
            temp = compress(temp, sgirow, rows, cols, 1, table, bpc);
            for (col = 0; col < cols; ++col)
                temp[col] = (ScanElem)PPM_GETB(pnmrow[col]);
            temp = compress(temp, sgirow, rows, cols, 2, table, bpc);
        }
    }

    free(temp);
    return table;
}



static void
writeHeader(unsigned int const cols, 
            unsigned int const rows, 
            xelval       const maxval, 
            unsigned int const bpc, 
            unsigned int const dimensions, 
            unsigned int const channels, 
            const char * const imagename) {

    unsigned int i;

    putBigShort(SGI_MAGIC);
    putByte(storage);
    putByte((char)bpc);
    putBigShort(dimensions);
    putBigShort(cols);
    putBigShort(rows);
    putBigShort(channels);
    putBigLong(0);                /* PIXMIN */
    putBigLong(maxval);           /* PIXMAX */

    for(i = 0; i < 4; ++i)
        putByte(0);

    for (i = 0; i < 79 && imagename[i] != '\0'; ++i)
        putByte(imagename[i]);

    for(; i < 80; ++i)
        putByte(0);

    putBigLong(CMAP_NORMAL);

    for (i = 0; i < 404; ++i)
        putByte(0);
}



int
main(int argc,char * argv[]) {

    FILE * ifP;
    int argn;
    const char * const usage = "[-verbatim|-rle] [-imagename <name>] [pnmfile]";
    int cols, rows;
    int format;
    xelval maxval, newmaxval;
    const char * imagename;
    unsigned int bpc;
    unsigned int dimensions;
    unsigned int channels;
    long * table;

    pnm_init(&argc, argv);

    imagename = "no name";  /* default value */
    argn = 1;
    while( argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0' ) {
        if( pm_keymatch(argv[argn], "-verbatim", 2) )
            storage = STORAGE_VERBATIM;
        else
        if( pm_keymatch(argv[argn], "-rle", 2) )
            storage = STORAGE_RLE;
        else
        if( pm_keymatch(argv[argn], "-imagename", 2) ) {
            if( ++argn >= argc )
                pm_usage(usage);
            imagename = argv[argn];
        }
        else
            pm_usage(usage);
        ++argn;
    }

    if( argn < argc ) {
        ifP = pm_openr( argv[argn] );
        argn++;
    }
    else
        ifP = stdin;

    if( argn != argc )
        pm_usage(usage);

    pnm_readpnminit(ifP, &cols, &rows, &maxval, &format);

    if (rows > INT16MAX || cols > INT16MAX)
        pm_error ("Input image is too large.");

    pnmrow = pnm_allocrow(cols);
    
    switch (PNM_FORMAT_TYPE(format)) {
        case PBM_TYPE:
            pm_message("promoting PBM to PGM");
            newmaxval = PGM_MAXMAXVAL;
        case PGM_TYPE:
            newmaxval = maxval;
            dimensions = 2;
            channels = 1;
            break;
        case PPM_TYPE:
            newmaxval = maxval;
            dimensions = 3;
            channels = 3;
            break;
        default:
            pm_error("can\'t happen");
    }
    if (newmaxval <= MAXVAL_BYTE)
        bpc = 1;
    else if (newmaxval <= MAXVAL_WORD)
        bpc = 2;
    else
        pm_error("maxval too large - try using \"pnmdepth %u\"", MAXVAL_WORD);

    table = buildChannels(ifP, cols, rows, newmaxval, format, bpc, channels);

    pnm_freerow(pnmrow);

    pm_close(ifP);

    writeHeader(cols, rows, newmaxval, bpc, dimensions, channels, imagename);

    if (table)
        writeTable(table, rows * channels);

    if (bpc == 1)
        writeChannels(cols, rows, channels, putShortAsByte);
    else
        writeChannels(cols, rows, channels, putBigShort);

    return 0;
}


