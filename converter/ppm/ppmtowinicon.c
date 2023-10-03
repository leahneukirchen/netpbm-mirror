/* ppmtowinicon.c - read PPM images and write a MS Windows .ico
**
** Copyright (C) 2000 by Lee Benfield - lee@benf.org
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <math.h>
#include <string.h>

#include "pm_c_util.h"
#include "winico.h"
#include "ppm.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "nstring.h"

#define MAJVERSION 0
#define MINVERSION 3

#define MAXCOLORS 256

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    unsigned int  iconCount;
    const char ** inputFileNm;  /* '-' if stdin; malloc'ed array */
    const char ** andpgmFileNm;    /* NULL if unspecified; malloc'ed array */
    const char *  output;     /* '-' if unspecified */
    unsigned int  truetransparent;
    unsigned int  verbose;
};


static bool verbose;

static int      fileOffset = 0; /* not actually used, but useful for debug. */

static void
parseCommandLine(int                  argc,
                 const char **        argv,
                 struct CmdlineInfo * cmdlineP) {
/*----------------------------------------------------------------------------
   Parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry *option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int outputSpec, andpgms;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "output",     OPT_STRING, &cmdlineP->output,
            &outputSpec,                   0);
    OPTENT3(0, "andpgms",    OPT_FLAG,   NULL,
            &andpgms,                      0);
    OPTENT3(0, "truetransparent", OPT_FLAG,   NULL,
            &cmdlineP->truetransparent,    0);
    OPTENT3(0, "verbose",    OPT_STRING, NULL,
            &cmdlineP->verbose,            0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!outputSpec)
        cmdlineP->output = "-";

    if (!andpgms) {
        if (argc-1 == 0) {
            cmdlineP->iconCount = 1;
            MALLOCARRAY_NOFAIL(cmdlineP->inputFileNm, cmdlineP->iconCount);
            cmdlineP->inputFileNm[0] = "-";
        } else {
            unsigned int iconIndex;

            cmdlineP->iconCount = argc-1;
            MALLOCARRAY_NOFAIL(cmdlineP->inputFileNm, cmdlineP->iconCount);
            for (iconIndex = 0; iconIndex < cmdlineP->iconCount; ++iconIndex)
                cmdlineP->inputFileNm[iconIndex] = argv[iconIndex+1];
        }
        {
            unsigned int iconIndex;
            MALLOCARRAY_NOFAIL(cmdlineP->andpgmFileNm, cmdlineP->iconCount);
            for (iconIndex = 0; iconIndex < cmdlineP->iconCount; ++iconIndex)
                cmdlineP->andpgmFileNm[iconIndex] = NULL;
        }
    } else {
        if (argc-1 < 2)
            pm_error("with -andpgms, you must specify at least two "
                     "arguments: image file name and and mask file name.  "
                     "You specified %d", argc-1);
        else if ((argc-1)/2*2 != (argc-1))
            pm_error("with -andpgms, you must specify an even number of "
                     "arguments.  You specified %d", argc-1);
        else {
            unsigned int iconIndex;
            cmdlineP->iconCount = (argc-1)/2;
            MALLOCARRAY_NOFAIL(cmdlineP->inputFileNm, cmdlineP->iconCount);
            MALLOCARRAY_NOFAIL(cmdlineP->andpgmFileNm, cmdlineP->iconCount);
            for (iconIndex = 0; iconIndex < cmdlineP->iconCount; ++iconIndex) {
                cmdlineP->inputFileNm[iconIndex] = argv[1 + iconIndex*2];
                cmdlineP->andpgmFileNm[iconIndex] = argv[2 + iconIndex*2];
            }
        }
    }

}



static void
freeCmdline(struct CmdlineInfo const cmdline) {

    free(cmdline.inputFileNm);
    free(cmdline.andpgmFileNm);
}



static void
writeU1(FILE * const ofP,
        u1     const v) {

    ++fileOffset;
    pm_writechar(ofP, v);
}



static  void
writeU2(FILE * const ofP,
        u2     const v) {

    fileOffset +=2;
    pm_writelittleshort(ofP, v);
}



static void
writeU4(FILE * const ofP,
        u4     const v) {

    fileOffset += 4;
    pm_writelittlelong(ofP, v);
}



static MS_Ico *
newIconFile(void) {

   MS_Ico * MSIconDataP;

   MALLOCVAR_NOFAIL(MSIconDataP);

   MSIconDataP->reserved     = 0;
   MSIconDataP->type         = 1;
   MSIconDataP->count        = 0;
   MSIconDataP->entries      = NULL;

   return MSIconDataP;
}



static ICON_bmp *
newAndBitmap(gray **      const ba,
             unsigned int const cols,
             unsigned int const rows,
             gray         const maxval) {
/*----------------------------------------------------------------------------
  create andBitmap from PGM
-----------------------------------------------------------------------------*/
    unsigned int const xByteCt = ROUNDUP(cols, 32)/8;
       /* How wide the u1 string for each row should be  */

    ICON_bmp * icBitmapP;
    unsigned int row;
    u1 ** rowData;

    MALLOCVAR_NOFAIL(icBitmapP);

    MALLOCARRAY_NOFAIL(rowData, rows);
    icBitmapP->xBytes = xByteCt;
    icBitmapP->data   = rowData;
    icBitmapP->size   = xByteCt * rows;
    for (row = 0; row < rows; ++row) {
        u1 * thisRow;  /* malloc'ed */
        unsigned int byteOn;
        unsigned int bitOn;

        MALLOCARRAY_NOFAIL(thisRow, xByteCt);

        byteOn =   0;  /* initial value */
        bitOn  = 128;  /* initial value */

        memset (thisRow, 0, xByteCt);
        rowData[rows - row - 1] = thisRow;

        if (ba) {
            unsigned int col;

            for (col = 0; col < cols; ++col) {
                /* Black (bit clear) is transparent in PGM alpha maps,
                   in ICO bit *set* is transparent.
                */
                if (ba[row][col] <= maxval/2) thisRow[byteOn] |= bitOn;

                if (bitOn == 1) {
                    ++byteOn;
                    bitOn = 128;
                } else {
                    bitOn >>= 1;
                }
            }
        } else {
            /* No array -- we're just faking this */
        }
    }
    return icBitmapP;
}



/* Depending on if the image is stored as 1bpp, 4bpp or 8bpp, the
   encoding mechanism is different.

   I didn't re-use the code from ppmtobmp since I need to keep the
   bitmaps in memory until I've loaded all ppms.

   8bpp => 1 byte/palette index.
   4bpp => High Nibble, Low Nibble
   1bpp => 1 palette value per bit, high bit 1st.
*/



static ICON_bmp *
new1Bitmap (pixel **        const pa,
            unsigned int    const cols,
            unsigned int    const rows,
            colorhash_table const cht) {

    /* How wide should the u1 string for each row be?  Each byte is 8 pixels,
       but must be a multiple of 4 bytes.
    */
    ICON_bmp * icBitmapP;  /* malloc'ed */
    unsigned int xByteCt;
    unsigned int row;
    unsigned int wt;
    u1 ** rowData;  /* malloc'ed */

    MALLOCVAR_NOFAIL(icBitmapP);

    wt = cols;  /* initial value */
    wt >>= 3;
    if (wt & 0x3) {
        wt = (wt & ~0x3) + 4;
    }
    xByteCt = wt;
    MALLOCARRAY_NOFAIL(rowData, rows);
    icBitmapP->xBytes = xByteCt;
    icBitmapP->data   = rowData;
    icBitmapP->size   = xByteCt * rows;

    for (row = 0; row <rows; ++row) {
        u1 * thisRow;  /* malloc'ed */
        unsigned int byteOn;
        unsigned int bitOn;

        MALLOCARRAY_NOFAIL(thisRow, xByteCt);
        memset (thisRow, 0, xByteCt);
        rowData[rows - row - 1] = thisRow;
        byteOn =   0;  /* initial value */
        bitOn  = 128;  /* initial value */

        if (pa) {
            unsigned int col;

            for (col = 0; col < cols; ++col) {
                /* So we've got a colorhash_table with two colors in it.  Which
                   is black?!

                   Unless the hashing function changes, 0's black.
                */
                int const value = ppm_lookupcolor(cht, &pa[row][col]);
                if (!value) {
                    /* leave black. */
                } else {
                    thisRow[byteOn] |= bitOn;
                }
                if (bitOn == 1) {
                    ++byteOn;
                    bitOn = 128;
                } else {
                    bitOn >>= 1;
                }
            }
        } else {
            /* No pixel array -- we're just faking this */
        }
    }
    return icBitmapP;
}



static ICON_bmp *
new4Bitmap(pixel **        const pa,
           unsigned int    const cols,
           unsigned int    const rows,
           colorhash_table const cht) {

    /* How wide should the u1 string for each row be?  Each byte is 8 pixels,
       but must be a multiple of 4 bytes.
    */
    ICON_bmp * icBitmapP;  /* malloc'ed */
    unsigned int row;
    unsigned int wt;
    unsigned int xByteCt;
    u1 ** rowData;  /* malloc'ed */

    MALLOCVAR_NOFAIL(icBitmapP);

    wt = cols;  /* initial value */
    wt >>= 1;
    if (wt & 0x3) {
        wt = (wt & ~0x3) + 4;
    }
    xByteCt = wt;
    MALLOCARRAY_NOFAIL(rowData, rows);
    icBitmapP->xBytes = xByteCt;
    icBitmapP->data   = rowData;
    icBitmapP->size   = xByteCt * rows;

    for (row = 0; row <rows; ++row) {
        u1 * thisRow;
        unsigned int byteOn;
        unsigned int nibble;   /* high nibble = 1, low nibble = 0; */

        MALLOCARRAY_NOFAIL(thisRow, xByteCt);

        memset(thisRow, 0, xByteCt);
        rowData[rows - row - 1] = thisRow;
        byteOn = 0;  /* initial value */
        nibble = 1;  /* initial value */

        if (pa) {
            unsigned int col;

            for (col = 0; col < cols; ++col) {
                int value;

                value = ppm_lookupcolor(cht, &pa[row][col]);  /* init value */
                /* Shift it, if we're putting it in the high nibble. */
                if (nibble)
                    value <<= 4;
                thisRow[byteOn] |= value;
                if (nibble == 1)
                    nibble = 0;
                else {
                    nibble = 1;
                    ++byteOn;
                }
            }
        } else {
            /* No pixel array -- we're just faking this */
        }
    }
    return icBitmapP;
}



static ICON_bmp *
new8Bitmap(pixel **        const pa,
           unsigned int    const cols,
           unsigned int    const rows,
           colorhash_table const cht) {

    /* How wide should the u1 string for each row be?  Each byte is 8 pixels,
       but must be a multiple of 4 bytes.
    */
    ICON_bmp * icBitmapP;  /* malloc'ed */
    unsigned int xByteCt;
    unsigned int row;
    unsigned int wt;
    u1 ** rowData;  /* malloc'ed */

    MALLOCVAR_NOFAIL(icBitmapP);

    wt = cols;  /* initial value */
    if (wt & 0x3) {
        wt = (wt & ~0x3) + 4;
    }
    xByteCt = wt;  /* initial value */
    MALLOCARRAY_NOFAIL(rowData, rows);
    icBitmapP->xBytes = xByteCt;
    icBitmapP->data   = rowData;
    icBitmapP->size   = xByteCt * rows;

    for (row = 0; row < rows; ++row) {
        u1 * thisRow;  /* malloc'ed */

        MALLOCARRAY_NOFAIL(thisRow, xByteCt);
        memset (thisRow, 0, xByteCt);
        rowData[rows - row - 1] = thisRow;
        if (pa) {
            unsigned int col;

            for (col = 0; col < cols; ++col)
                thisRow[col] = ppm_lookupcolor(cht, &pa[row][col]);
        } else {
            /* No pixel array -- we're just faking this */
        }
    }
    return icBitmapP;
}



static IC_InfoHeader *
newInfoHeader(IC_Entry const entry) {

   IC_InfoHeader * ihP;

   MALLOCVAR_NOFAIL(ihP);

   ihP->size             = 40;
   ihP->width            = entry.width;
   ihP->height           = entry.height * 2;
   ihP->planes           = 1;
   ihP->bitcount         = entry.bitcount;
   ihP->compression      = 0;
   ihP->imagesize        = entry.width * entry.height * 8 / entry.bitcount;
   ihP->x_pixels_per_m   = 0;
   ihP->y_pixels_per_m   = 0;
   ihP->colors_used      = 1 << entry.bitcount;
   ihP->colors_important = 0;

   return ihP;
}



static IC_Palette *
newCleanPalette(void) {

    IC_Palette * paletteP;  /* malloc'ed */

    unsigned int i;

    MALLOCVAR_NOFAIL(paletteP);

    MALLOCARRAY_NOFAIL(paletteP->colors, MAXCOLORS);

    for (i=0; i <MAXCOLORS; ++i) {
        paletteP->colors[i] = NULL;
    }
    return paletteP;
}



static void
addColorToPalette(IC_Palette * const paletteP,
                  unsigned int const i,
                  unsigned int const r,
                  unsigned int const g,
                  unsigned int const b) {

    MALLOCVAR_NOFAIL(paletteP->colors[i]);

    paletteP->colors[i]->red      = r;
    paletteP->colors[i]->green    = g;
    paletteP->colors[i]->blue     = b;
    paletteP->colors[i]->reserved = 0;
}



static ICON_bmp *
newBitmap(unsigned int    const bpp,
          pixel **        const pa,
          unsigned int    const cols,
          unsigned int    const rows,
          colorhash_table const cht) {

    ICON_bmp * retval;

    int const assumedBpp = (pa == NULL) ? 1 : bpp;

    switch (assumedBpp) {
    case 1:
        retval = new1Bitmap(pa, cols, rows, cht);
        break;
    case 4:
        retval = new4Bitmap(pa, cols, rows, cht);
        break;
    case 8:
    default:
        retval = new8Bitmap(pa, cols, rows, cht);
        break;
    }
    return retval;
}



static void
makePalette(pixel **          const xorPPMarray,
            unsigned int      const xorCols,
            unsigned int      const xorRows,
            pixval            const xorMaxval,
            IC_Palette **     const palettePP,
            colorhash_table * const xorChtP,
            unsigned int *    const colorsP,
            const char **     const errorP) {
/*----------------------------------------------------------------------------
   Figure out the colormap and turn it into the appropriate GIF colormap -
   this code's pretty much straight from 'ppmtobpm'.
-----------------------------------------------------------------------------*/
    IC_Palette * const paletteP = newCleanPalette();

    colorhist_vector xorChv;
    unsigned int i;
    int colorCt;

    if (verbose)
        pm_message("computing colormap...");

    xorChv = ppm_computecolorhist(xorPPMarray, xorCols, xorRows, MAXCOLORS,
                                  &colorCt);
    if (!xorChv)
        pm_asprintf(errorP,
                    "image has too many colors - try doing a 'pnmquant %u'",
                    MAXCOLORS);
    else {
        *errorP = NULL;

        if (verbose)
            pm_message("%u colors found", colorCt);

        if (verbose && (xorMaxval > 255))
            pm_message("maxval is not 255 - automatically rescaling colors");
        for (i = 0; i < colorCt; ++i) {
            if (xorMaxval == 255) {
                addColorToPalette(paletteP, i,
                                  PPM_GETR(xorChv[i].color),
                                  PPM_GETG(xorChv[i].color),
                                  PPM_GETB(xorChv[i].color));
            } else {
                addColorToPalette(paletteP, i,
                                  PPM_GETR(xorChv[i].color) * 255 / xorMaxval,
                                  PPM_GETG(xorChv[i].color) * 255 / xorMaxval,
                                  PPM_GETB(xorChv[i].color) * 255 / xorMaxval);
            }
        }

        /* And make a hash table for fast lookup. */
        *xorChtP = ppm_colorhisttocolorhash(xorChv, colorCt);

        ppm_freecolorhist(xorChv);

        *palettePP = paletteP;
        *colorsP   = colorCt;
    }
}



static void
getOrFakeAndMap(const char *      const andPgmFname,
                unsigned int      const xorCols,
                unsigned int      const xorRows,
                gray ***          const andPGMarrayP,
                pixval *          const andMaxvalP,
                colorhash_table * const andChtP,
                const char **     const errorP) {

    int andRows, andCols;

    if (!andPgmFname) {
        /* He's not supplying a bitmap for 'and'.  Fake the bitmap. */
        *andPGMarrayP = NULL;
        *andMaxvalP   = 1;
        *andChtP      = NULL;
        *errorP       = NULL;
    } else {
        FILE * andFileP;
        andFileP = pm_openr(andPgmFname);
        *andPGMarrayP = pgm_readpgm(andFileP, &andCols, &andRows, andMaxvalP);
        pm_close(andFileP);

        if ((andCols != xorCols) || (andRows != xorRows)) {
            pm_asprintf(errorP,
                        "And mask and image have different dimensions "
                        "(%u x %u vs %u x %u).  Aborting.",
                        andCols, xorCols, andRows, xorRows);
        } else
            *errorP = NULL;
    }
}



static void
blackenTransparentAreas(pixel **     const xorPPMarray,
                        unsigned int const cols,
                        unsigned int const rows,
                        gray **      const andPGMarray,
                        pixval       const andMaxval) {

    unsigned int row;

    if (verbose)
        pm_message("Setting transparent pixels to black");

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        for (col = 0; col < cols; ++col) {
            if (andPGMarray[row][col] < andMaxval)
                /* It's not opaque here; make it black */
                PPM_ASSIGN(xorPPMarray[row][col], 0, 0, 0);
        }
    }
}



static void
addEntryToIcon(MS_Ico *     const MSIconDataP,
               const char * const xorPpmFname,
               const char * const andPgmFname,
               bool         const trueTransparent) {

    IC_Entry * entryP;
    FILE * xorfile;
    pixel ** xorPPMarray;
    gray ** andPGMarray;
    ICON_bmp * xorBitmapP;
    ICON_bmp * andBitmapP;
    int rows, cols;
    unsigned int bpp;
    unsigned int colorCt;
    unsigned int entryCols;
    IC_Palette * paletteP;
    colorhash_table xorCht;
    colorhash_table andCht;
    const char * error;

    pixval xorMaxval;
    gray andMaxval;

    MALLOCVAR_NOFAIL(entryP);

    /* Read the xor PPM. */
    xorfile = pm_openr(xorPpmFname);
    xorPPMarray = ppm_readppm(xorfile, &cols, &rows, &xorMaxval);
    pm_close(xorfile);

    /* Since the entry uses 1 byte to hold the width and height of the icon,
       the image can't be more than 256 x 256.
    */
    if (rows > 255 || cols > 255) {
        pm_error("Max size for a icon is 255 x 255 (1 byte fields).  "
                 "%s is %u x %u", xorPpmFname, cols, rows);
    }

    if (verbose)
        pm_message("read PPM: %uw x %uh, maxval = %u", cols, rows, xorMaxval);

    makePalette(xorPPMarray, cols, rows, xorMaxval,
                &paletteP, &xorCht, &colorCt, &error);

    if (error)
        pm_error("Unable to make palette for '%s'.  %s", xorPpmFname, error);

    /* All the icons I found seemed to pad the palette to the max entries for
       that bitdepth.

       The spec indicates this isn't necessary, but I'll follow this behaviour
       just in case.
    */
    if (colorCt < 3) {
        bpp = 1;
        entryCols = 2;
    } else if (colorCt < 17) {
        bpp = 4;
        entryCols = 16;
    } else {
        bpp = 8;
        entryCols = 256;
    }

    getOrFakeAndMap(andPgmFname, cols, rows,
                    &andPGMarray, &andMaxval, &andCht, &error);
    if (error)
        pm_error("Error in and map for '%s'.  %s", xorPpmFname, error);

    if (andPGMarray && trueTransparent)
        blackenTransparentAreas(xorPPMarray, cols, rows,
                                andPGMarray, andMaxval);

    xorBitmapP = newBitmap(bpp, xorPPMarray, cols, rows, xorCht);
    andBitmapP = newAndBitmap(andPGMarray, cols, rows, andMaxval);

    /* Fill in the entry data fields. */
    entryP->width         = cols;
    entryP->height        = rows;
    entryP->color_count   = entryCols;
    entryP->reserved      = 0;
    entryP->planes        = 1;
    entryP->bitcount      = bpp;
    /* all the icons I looked at ignored this value */
    entryP->ih            = newInfoHeader(*entryP);
    entryP->colors        = paletteP->colors;
    entryP->size_in_bytes =
        xorBitmapP->size + andBitmapP->size + 40 + (4 * entryP->color_count);
    if (verbose)
        pm_message("entry->size_in_bytes = %u + %u + %u = %u",
                   xorBitmapP->size, andBitmapP->size,
                   40, entryP->size_in_bytes );

    /* We don't know the offset ATM, set to 0 for now.  Have to calculate this
       at the end.
    */
    entryP->file_offset  = 0;
    entryP->xorBitmapOut = xorBitmapP->data;
    entryP->andBitmapOut = andBitmapP->data;
    entryP->xBytesXor    = xorBitmapP->xBytes;
    entryP->xBytesAnd    = andBitmapP->xBytes;

    /* Add the entry to the entries array. */
    ++MSIconDataP->count;

    /* Perhaps I should allocate ahead, and take fewer trips to the well. */
    REALLOCARRAY(MSIconDataP->entries, MSIconDataP->count);
    MSIconDataP->entries[MSIconDataP->count - 1] = entryP;
}



static void
writeIC_Entry(FILE *     const ofP,
              IC_Entry * const entryP) {

   writeU1(ofP, entryP->width);
   writeU1(ofP, entryP->height);
   writeU1(ofP, entryP->color_count); /* chops 256->0 on its own.. */
   writeU1(ofP, entryP->reserved);
   writeU2(ofP, entryP->planes);
   writeU2(ofP, entryP->bitcount);
   writeU4(ofP, entryP->size_in_bytes);
   writeU4(ofP, entryP->file_offset);
}



static void
writeIC_InfoHeader(FILE *          const ofP,
                   IC_InfoHeader * const ihP) {

   writeU4(ofP, ihP->size);
   writeU4(ofP, ihP->width);
   writeU4(ofP, ihP->height);
   writeU2(ofP, ihP->planes);
   writeU2(ofP, ihP->bitcount);
   writeU4(ofP, ihP->compression);
   writeU4(ofP, ihP->imagesize);
   writeU4(ofP, ihP->x_pixels_per_m);
   writeU4(ofP, ihP->y_pixels_per_m);
   writeU4(ofP, ihP->colors_used);
   writeU4(ofP, ihP->colors_important);
}



static void
writeIC_Color(FILE *     const ofP,
              IC_Color * const colorP) {

    /* Since the ppm might not have as many colors in it as we'd like,
       (2, 16, 256), stick 0 in the gaps.

       This means that we lose palette information, but that can't be helped.
    */
    if (!colorP) {
        writeU1(ofP, 0);
        writeU1(ofP, 0);
        writeU1(ofP, 0);
        writeU1(ofP, 0);
    } else {
        writeU1(ofP, colorP->blue);
        writeU1(ofP, colorP->green);
        writeU1(ofP, colorP->red);
        writeU1(ofP, colorP->reserved);
    }
}



static void
writeBitmap(FILE *       const ofP,
            u1 **        const bitmap,
            unsigned int const xByteCt,
            unsigned int const height) {

    unsigned int row;

    for (row = 0; row < height; ++row) {
        fwrite(bitmap[row], 1, xByteCt, ofP);
        fileOffset += xByteCt;
    }
}



static void
writeMS_Ico(MS_Ico *     const MSIconDataP,
            const char * const outFname) {

    FILE * const ofP = pm_openw(outFname);

    unsigned int i;

    writeU2(ofP, MSIconDataP->reserved);
    writeU2(ofP, MSIconDataP->type);
    writeU2(ofP, MSIconDataP->count);

    for (i = 0; i < MSIconDataP->count; ++i)
        writeIC_Entry(ofP, MSIconDataP->entries[i]);

    for (i = 0; i < MSIconDataP->count; ++i) {
        IC_Entry * const entryP = MSIconDataP->entries[i];

        unsigned int j;

        writeIC_InfoHeader(ofP, MSIconDataP->entries[i]->ih);

        for (j = 0; j < (MSIconDataP->entries[i]->color_count); ++j) {
            writeIC_Color(ofP, MSIconDataP->entries[i]->colors[j]);
        }
        if (verbose)
            pm_message("writing xor bitmap");

        writeBitmap(ofP,
                    entryP->xorBitmapOut, entryP->xBytesXor, entryP->height);
        if (verbose)
            pm_message("writing and bitmap");

        writeBitmap(ofP,
                    entryP->andBitmapOut, entryP->xBytesAnd, entryP->height);
    }
    fclose(ofP);
}



int
main(int argc, const char ** argv) {

    MS_Ico * const MSIconDataP = newIconFile();

    struct CmdlineInfo cmdline;
    unsigned int iconIndex;
    unsigned int offset;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    verbose = cmdline.verbose;

    for (iconIndex = 0; iconIndex < cmdline.iconCount; ++iconIndex) {
        addEntryToIcon(MSIconDataP, cmdline.inputFileNm[iconIndex],
                       cmdline.andpgmFileNm[iconIndex],
                       cmdline.truetransparent);
    }
    /*
     * Now we have to go through and calculate the offsets.
     * The first infoheader starts at 6 + count*16 bytes.
     */
    offset = (MSIconDataP->count * 16) + 6;
    for (iconIndex = 0; iconIndex < MSIconDataP->count; ++iconIndex) {
        IC_Entry * const entryP = MSIconDataP->entries[iconIndex];

        entryP->file_offset = offset;
        /*
         * Increase the offset by the size of this offset & data.
         * this includes the size of the color data.
         */
        offset += entryP->size_in_bytes;
    }
    /*
     * And now, we have to actually SAVE the .ico!
     */
    writeMS_Ico(MSIconDataP, cmdline.output);

    freeCmdline(cmdline);

    return 0;
}


