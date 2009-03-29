/* ppmtomitsu.c - read a PPM image and produce output for the
**                Mitsubishi S340-10 Thermo-Sublimation Printer
**                (or the S3410-30 parallel interface)
**
** Copyright (C) 1992,93 by S.Petra Zeidler
** Minor modifications by Ingo Wilken:
**  - mymalloc() and check_and_rotate() functions for often used
**    code fragments.  Reduces code size by a few KB.
**  - use pm_error() instead of fprintf(stderr)
**  - localized allocation of colorhastable
**
** This software was written for the Max Planck Institut fuer Radioastronomie,
** Bonn, Germany, Optical Interferometry group
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
#include <stdio.h>

#include "pm_c_util.h"
#include "nstring.h"
#include "mallocvar.h"
#include "ppm.h"

#include "mitsu.h"


#define HASHSIZE 2048
#define myhash(x) ((PPM_GETR(x)*3 + PPM_GETG(x)*5 + PPM_GETB(x)*7) % HASHSIZE)

typedef struct hashinfo {
        pixel     color;
        long      flag;
        struct hashinfo * next;
} hashinfo;

#define cmd(arg)           fputc((arg), stdout)
#define datum(arg)         fputc((char)(arg), stdout)
#define data(arg,num)      fwrite((arg), sizeof(char), (num), stdout)



static void
check_and_rotate(int              const cols,
                 int              const rows,
                 int              const enlarge,
                 struct mediasize const medias) {

    if (cols > rows) {
        ROTATEIMG(DOROTATE);                        /* rotate image */
        if (enlarge * rows > medias.maxcols || enlarge * cols > medias.maxrows)
            pm_error("Image too large, MaxPixels = %u x %u",
                     medias.maxrows, medias.maxcols);
        HPIXELS(cols);
        VPIXELS(rows);
        HPIXELSOFF((medias.maxcols/enlarge - rows)/2);
        VPIXELSOFF((medias.maxrows/enlarge - cols)/2);
        pm_message("rotating image for output");
    } else {
        ROTATEIMG(DONTROTATE);
        if (enlarge * rows > medias.maxrows || enlarge * cols > medias.maxcols)
            pm_error("Image too large, MaxPixels = %u x %u",
                     medias.maxrows, medias.maxcols);
        HPIXELS(cols);
        VPIXELS(rows);
        HPIXELSOFF((medias.maxcols/enlarge - cols)/2);
        VPIXELSOFF((medias.maxrows/enlarge - rows)/2);
    }
}



static void
lineputinit(int              const cols,
            int              const rows,
            int              const sharpness,
            int              const enlarge,
            int              const copy,
            struct mediasize const medias) {
    ONLINE;
    CLRMEM;
    MEDIASIZE(medias);

    switch (enlarge) {
    case 2:
        HENLARGE(ENLARGEx2); /* enlarge horizontal */
        VENLARGE(ENLARGEx2); /* enlarge vertical */
        break;
    case 3:
        HENLARGE(ENLARGEx3); /* enlarge horizontal */
        VENLARGE(ENLARGEx3); /* enlarge vertical */
        break;
    default:
        HENLARGE(NOENLARGE); /* enlarge horizontal */
        VENLARGE(NOENLARGE); /* enlarge vertical */
    }

    COLREVERSION(DONTREVERTCOLOR);
    NUMCOPY(copy);

    HOFFINCH('\000');
    VOFFINCH('\000');
    CENTERING(DONTCENTER);

    TRANSFERFORMAT(LINEORDER);
    COLORSYSTEM(RGB);
    GRAYSCALELVL(BIT_8);

    switch (sharpness) {          /* sharpness :-) */
    case 0:
        SHARPNESS(SP_NONE);
        break;
    case 1:
        SHARPNESS(SP_LOW);
        break;
    case 2:
        SHARPNESS(SP_MIDLOW);
        break;
    case 3:
        SHARPNESS(SP_MIDHIGH);
        break;
    case 4:
        SHARPNESS(SP_HIGH);
        break;
    default:
        SHARPNESS(SP_USER);
    }
    check_and_rotate(cols, rows, enlarge, medias);
    DATASTART;
}



static void
lookuptableinit(int              const sharpness,
                int              const enlarge,
                int              const copy,
                struct mediasize const medias) {

    ONLINE;
    CLRMEM;
    MEDIASIZE(medias);

    switch (enlarge) {
    case 2:
        HENLARGE(ENLARGEx2); /* enlarge horizontal */
        VENLARGE(ENLARGEx2); /* enlarge vertical */
        break;
    case 3:
        HENLARGE(ENLARGEx3); /* enlarge horizontal */
        VENLARGE(ENLARGEx3); /* enlarge vertical */
        break;
    default:
        HENLARGE(NOENLARGE); /* enlarge horizontal */
        VENLARGE(NOENLARGE); /* enlarge vertical */
    }

    COLREVERSION(DONTREVERTCOLOR);
    NUMCOPY(copy);

    HOFFINCH('\000');
    VOFFINCH('\000');
    CENTERING(DONTCENTER);

    TRANSFERFORMAT(LOOKUPTABLE);

    switch (sharpness) {          /* sharpness :-) */
    case 0:
        SHARPNESS(SP_NONE);
        break;
    case 1:
        SHARPNESS(SP_LOW);
        break;
    case 2:
        SHARPNESS(SP_MIDLOW);
        break;
    case 3:
        SHARPNESS(SP_MIDHIGH);
        break;
    case 4:
        SHARPNESS(SP_HIGH);
        break;
    default:
        SHARPNESS(SP_USER);
    }

    LOADLOOKUPTABLE;
}



static void
lookuptabledata(int              const cols,
                int              const rows,
                int              const enlarge,
                struct mediasize const medias) {

    DONELOOKUPTABLE;
    check_and_rotate(cols, rows, enlarge, medias);
    DATASTART;
}



static void
frametransferinit(int              const cols,
                  int              const rows,
                  int              const sharpness,
                  int              const enlarge,
                  int              const copy,
                  struct mediasize const medias) {

    ONLINE;
    CLRMEM;
    MEDIASIZE(medias);

    switch (enlarge) {
    case 2:
        HENLARGE(ENLARGEx2); /* enlarge horizontal */
        VENLARGE(ENLARGEx2); /* enlarge vertical */
        break;
    case 3:
        HENLARGE(ENLARGEx3); /* enlarge horizontal */
        VENLARGE(ENLARGEx3); /* enlarge vertical */
        break;
    default:
        HENLARGE(NOENLARGE); /* enlarge horizontal */
        VENLARGE(NOENLARGE); /* enlarge vertical */
    }

    COLREVERSION(DONTREVERTCOLOR);
    NUMCOPY(copy);

    HOFFINCH('\000');
    VOFFINCH('\000');
    CENTERING(DONTCENTER);

    TRANSFERFORMAT(FRAMEORDER);
    COLORSYSTEM(RGB);
    GRAYSCALELVL(BIT_8);

    switch (sharpness) {          /* sharpness :-) */
    case 0:
        SHARPNESS(SP_NONE);
        break;
    case 1:
        SHARPNESS(SP_LOW);
        break;
    case 2:
        SHARPNESS(SP_MIDLOW);
        break;
    case 3:
        SHARPNESS(SP_MIDHIGH);
        break;
    case 4:
        SHARPNESS(SP_HIGH);
        break;
    default:
        SHARPNESS(SP_USER);
    }
    check_and_rotate(cols, rows, enlarge, medias);
}



static void
doLookupTableColors(colorhist_vector const table,
                    unsigned int     const nColor,
                    hashinfo *       const colorhashtable) {
                
    unsigned int colval;
    for (colval = 0; colval < nColor; ++colval) {
        struct hashinfo * const hashchain =
            &colorhashtable[myhash((table[colval]).color)];

        struct hashinfo * hashrun;

        cmd('$');
        datum(colval);
        datum(PPM_GETR((table[colval]).color));
        datum(PPM_GETG((table[colval]).color));
        datum(PPM_GETB((table[colval]).color));
        
        hashrun = hashchain;  /* start at beginning of chain */

        if (hashrun->flag == -1) {
            hashrun->color = (table[colval]).color;
            hashrun->flag  = colval;
        } else {
            while (hashrun->next != NULL)
                hashrun = hashrun->next;
            MALLOCVAR_NOFAIL(hashrun->next);
            hashrun = hashrun->next;
            hashrun->color = (table[colval]).color;
            hashrun->flag  = colval;
            hashrun->next  = NULL;
        }
    }
}



static void
doLookupTableGrays(colorhist_vector const table,
                   unsigned int     const nColor,
                   hashinfo *       const colorhashtable) {

    unsigned int colval;
    for (colval = 0; colval < nColor; ++colval) {
        struct hashinfo * const hashchain =
            &colorhashtable[myhash((table[colval]).color)];
        struct hashinfo * hashrun;

        cmd('$');
        datum(colval);
        datum(PPM_GETB((table[colval]).color));
        datum(PPM_GETB((table[colval]).color));
        datum(PPM_GETB((table[colval]).color));
        
        hashrun = hashchain;  /* start at beginning of chain */

        if (hashrun->flag == -1) {
            hashrun->color = (table[colval]).color;
            hashrun->flag  = colval;
        } else {
            while (hashrun->next != NULL)
                hashrun = hashrun->next;
            MALLOCVAR_NOFAIL(hashrun->next);
            hashrun = hashrun->next;
            hashrun->color = (table[colval]).color;
            hashrun->flag  = colval;
            hashrun->next  = NULL;
        }
    }
}



static void
generateLookupTable(colorhist_vector const table,
                    unsigned int     const nColor,
                    unsigned int     const cols,
                    unsigned int     const rows,
                    int              const format,
                    int              const sharpness,
                    int              const enlarge,
                    int              const copy,
                    struct mediasize const medias,
                    hashinfo **      const colorhashtableP) {
/*----------------------------------------------------------------------------
   Write to the output file the palette (color lookup table) indicated by
   'table' and generate a hash table to use with it: *colorhashtableP.

   Also write the various properties 'sharpness', 'enlarge', 'copy', and
   'medias' to the output file.
-----------------------------------------------------------------------------*/
    hashinfo * colorhashtable;

    lookuptableinit(sharpness, enlarge, copy, medias);

    /* Initialize the hash table to empty */

    MALLOCARRAY_NOFAIL(colorhashtable, HASHSIZE);
    {
        unsigned int i;
        for (i = 0; i < HASHSIZE; ++i) {
            colorhashtable[i].flag = -1;
                    colorhashtable[i].next = NULL;
        }
    }

    switch(PPM_FORMAT_TYPE(format)) {
    case PPM_TYPE:
        doLookupTableColors(table, nColor, colorhashtable);
        break;
    default:
        doLookupTableGrays(table, nColor, colorhashtable);
    }
    lookuptabledata(cols, rows, enlarge, medias);

    *colorhashtableP = colorhashtable;
}



static void
writeColormapRaster(pixel **         const pixels,
                    unsigned int     const cols,
                    unsigned int     const rows,
                    hashinfo *       const colorhashtable) {
/*----------------------------------------------------------------------------
   Write a colormapped raster: write the pixels pixels[][] (dimensions cols x
   rows) as indices into the colormap (palette; lookup table) indicated by
   'colorhashtable'.
-----------------------------------------------------------------------------*/
    unsigned int row;

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        for (col = 0; col < cols; ++col) {
            pixel * const pixrow = pixels[row];
            struct hashinfo * const hashchain =
                &colorhashtable[myhash(pixrow[col])];
            struct hashinfo * p;
                
            p = hashchain;
            while (!PPM_EQUAL((p->color), pixrow[col])) {
                assert(p->next);
                p = p->next;
            }
            datum(p->flag);
        }
    }
}



static void
useLookupTable(pixel **         const pixels,
               colorhist_vector const table,
               int              const sharpness,
               int              const enlarge,
               int              const copy,
               struct mediasize const medias,
               unsigned int     const cols,
               unsigned int     const rows,
               int              const format,
               unsigned int     const nColor) {

    hashinfo * colorhashtable;

    pm_message("found %u colors - using the lookuptable-method", nColor);

    generateLookupTable(table, nColor, cols, rows, format,
                        sharpness, enlarge, copy, medias,
                        &colorhashtable);

    writeColormapRaster(pixels, cols, rows, colorhashtable);

    free(colorhashtable);
}



static void
noLookupColor(pixel **     const pixels,
              unsigned int const cols,
              unsigned int const rows) {

    unsigned int row;
    COLORDES(RED);
    DATASTART;                    /* red coming */
    for (row = 0; row < rows; ++row) {
        pixel * const pixrow = pixels[row];
        unsigned int col;
        for (col = 0; col < cols; ++col)
            datum(PPM_GETR(pixrow[col]));
    }
    COLORDES(GREEN);
    DATASTART;                    /* green coming */
    for (row = 0; row < rows; ++row) {
        pixel * const pixrow = pixels[row];
        unsigned int col;
        for (col = 0; col < cols; ++col)
            datum(PPM_GETG(pixrow[col]));
    }
    COLORDES(BLUE);
    DATASTART;                    /* blue coming */
    for (row = 0; row < rows; ++row) {
        pixel * const pixrow = pixels[row];
        unsigned int col;
        for (col = 0; col < cols; ++col)
            datum(PPM_GETB(pixrow[col]));
    }
}



static void
noLookupGray(pixel **     const pixels,
             unsigned int const cols,
             unsigned int const rows) {

    unsigned int row;
    COLORDES(RED);
    DATASTART;                    /* red coming */
    for (row = 0; row < rows; ++row) {
        pixel * const pixrow = pixels[row];
        unsigned int col;
        for (col = 0; col < cols; ++col)
            datum(PPM_GETB(pixrow[col]));
    }
    COLORDES(GREEN);
    DATASTART;                    /* green coming */
    for (row = 0; row < rows; ++row) {
        pixel * const pixrow = pixels[row];
        unsigned int col;
        for (col = 0; col < cols; ++col)
            datum(PPM_GETB(pixrow[col]));
    }
    COLORDES(BLUE);
    DATASTART;                    /* blue coming */
    for (row = 0; row < rows; ++row) {
        pixel * const pixrow = pixels[row];
        unsigned int col;
        for (col = 0; col < cols; ++col)
            datum(PPM_GETB(pixrow[col]));
    }
}



static void
useNoLookupTable(pixel **         const pixels,
                 int              const sharpness,
                 int              const enlarge,
                 int              const copy,
                 struct mediasize const medias,
                 unsigned int     const cols,
                 unsigned int     const rows,
                 int              const format) {

    /* $#%@^!& no lut possible, so send the pic as 24bit */

    pm_message("found too many colors for fast lookuptable mode");

    frametransferinit(cols, rows, sharpness, enlarge, copy, medias);
    switch(PPM_FORMAT_TYPE(format)) {
    case PPM_TYPE:
        noLookupColor(pixels, cols, rows);
        break;
    default:
        noLookupGray(pixels, cols, rows);
    }
}



static void
doTiny(FILE *           const ifP,
       unsigned int     const cols,
       unsigned int     const rows,
       pixval           const maxval,
       int              const format,
       int              const sharpness,
       int              const enlarge,
       int              const copy,
       struct mediasize const medias) {
       
    pixel * pixelrow;
    unsigned char * redrow;
    unsigned char * grnrow;
    unsigned char * blurow;
    unsigned int row;

    pixelrow = ppm_allocrow(cols);
    MALLOCARRAY_NOFAIL(redrow, cols);
    MALLOCARRAY_NOFAIL(grnrow, cols);
    MALLOCARRAY_NOFAIL(blurow, cols);
    lineputinit(cols, rows, sharpness, enlarge, copy, medias);

    for (row = 0; row < rows; ++row) {
        ppm_readppmrow(ifP, pixelrow, cols, maxval, format);
        switch(PPM_FORMAT_TYPE(format)) {
        case PPM_TYPE: {            /* color */
            unsigned int col;
            for (col = 0; col < cols; ++col) {
                redrow[col] = PPM_GETR(pixelrow[col]);
                grnrow[col] = PPM_GETG(pixelrow[col]);
                blurow[col] = PPM_GETB(pixelrow[col]);
            }
            data(redrow, cols);
            data(grnrow, cols);
            data(blurow, cols);
        } break;
        default: {           /* grayscale */
            unsigned int col;
            for (col = 0; col < cols; ++col)
                blurow[col] = PPM_GETB(pixelrow[col]);
            data(blurow, cols);
            data(blurow, cols);
            data(blurow, cols);
        }
        }
    }
}



int
main(int argc, char * argv[]) {
    FILE * ifP;
    int              argn;
    bool             dpi300;
    int              cols, rows, format;
    pixval           maxval;
    int              sharpness, enlarge, copy, tiny;
    struct mediasize medias;
    char             media[16];
    const char * const usage = "[-sharpness <1-4>] [-enlarge <1-3>] [-media <a,a4,as,a4s>] [-copy <1-9>] [-tiny] [-dpi300] [ppmfile]";

    ppm_init(&argc, argv);

    dpi300 = FALSE;
    argn = 1;
    sharpness = 32;
    enlarge   = 1;
    copy      = 1;
    memset(media, '\0', 16);
    tiny      = FALSE;

    /* check for flags */
    while (argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0') {
    if (pm_keymatch(argv[argn], "-sharpness", 2)) {
        ++argn;
        if (argn == argc || sscanf(argv[argn], "%d", &sharpness) != 1)
            pm_usage(usage);
        else if (sharpness < 1 || sharpness > 4)
            pm_usage(usage);
        }
    else if (pm_keymatch(argv[argn], "-enlarge", 2)) {
        ++argn;
        if (argn == argc || sscanf(argv[argn], "%d", &enlarge) != 1)
            pm_usage(usage);
        else if (enlarge < 1 || enlarge > 3)
            pm_usage(usage);
        }
    else if (pm_keymatch(argv[argn], "-media", 2)) {
        ++argn;
        if (argn == argc || sscanf(argv[argn], "%15s", media) < 1)
            pm_usage(usage);
        else if (TOUPPER(media[0]) != 'A')
            pm_usage(usage);
    }
    else if (pm_keymatch(argv[argn], "-copy", 2)) {
        ++argn;
        if (argn == argc || sscanf(argv[argn], "%d", &copy) != 1)
            pm_usage(usage);
        else if (copy < 1 || copy > 9)
            pm_usage(usage);
        }
    else if (pm_keymatch(argv[argn], "-dpi300", 2))
        dpi300 = TRUE;
    else if (pm_keymatch(argv[argn], "-tiny", 2))
        tiny = TRUE;
    else
        pm_usage(usage);
        ++argn;
    }

    if (argn < argc) {
        ifP = pm_openr(argv[argn]);
        ++argn;
    }
    else
        ifP = stdin;

    if (argn != argc)
        pm_usage(usage);

    if (TOUPPER(media[0]) == 'A')
        switch (TOUPPER(media[1])) {
        case 'S':
            medias = MSize_AS;
            break;
        case '4':
            if(TOUPPER(media[2]) == 'S')
                medias = MSize_A4S;
            else {
                medias = MSize_A4;
            }
            break;
        default:
            medias = MSize_A;
        }
    else
        medias = MSize_User;

    if (dpi300) {
        medias.maxcols *= 2;
        medias.maxrows *= 2;
    }

    ppm_readppminit(ifP, &cols, &rows, &maxval, &format);
    
    if (tiny) {
        doTiny(ifP, cols, rows, maxval, format,
               sharpness, enlarge, copy, medias);

    } else {
        pixel ** pixels;
        int nColor;
        colorhist_vector table;
        unsigned int row;

        pixels = ppm_allocarray(cols, rows);
        for (row = 0; row < rows; ++row)
            ppm_readppmrow(ifP, pixels[row], cols, maxval, format);

        /* first check wether we can use the lut transfer */

        table = ppm_computecolorhist(pixels, cols, rows, MAXLUTCOL+1, 
                                     &nColor);
        if (table)
            useLookupTable(pixels, table, sharpness, enlarge, copy, medias,
                           cols, rows, format, nColor);
        else
            useNoLookupTable(pixels, sharpness, enlarge, copy, medias,
                             cols, rows, format);
        ppm_freearray(pixels, rows);
    }
    PRINTIT;
    pm_close(ifP);
    return 0;
}
