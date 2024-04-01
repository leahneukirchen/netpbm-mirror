/* ppmtoxpm.c - read a PPM image and produce a (version 3) X11 pixmap
**
** Copyright (C) 1990 by Mark W. Snitily
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
** This tool was developed for Schlumberger Technologies, ATE Division, and
** with their permission is being made available to the public with the above
** copyright notice and permission notice.
**
** Upgraded to XPM2 by
**   Paul Breslaw, Mecasoft SA, Zurich, Switzerland (paul@mecazh.uu.ch)
**   Thu Nov  8 16:01:17 1990
**
** Upgraded to XPM version 3 by
**   Arnaud Le Hors (lehors@mirsa.inria.fr)
**   Tue Apr 9 1991
**
** Rainer Sinkwitz sinkwitz@ifi.unizh.ch - 21 Nov 91:
**  - Bug fix, should should malloc space for rgbn[j].name+1 in line 441
**    caused segmentation faults
**
**  - lowercase conversion of RGB names def'ed out,
**    considered harmful.
**
** Michael Pall (pall@rz.uni-karlsruhe.de) - 29 Nov 93:
**  - Use the algorithm from xpm-lib for pixel encoding
**    (base 93 not base 28 -> saves a lot of space for colorful xpms)
*/

#define _DEFAULT_SOURCE /* New name for SVID & BSD source defines */
#define _BSD_SOURCE   /* Make sure strdup() is in string.h */
#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */

#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "pm_c_util.h"
#include "ppm.h"
#include "shhopt.h"
#include "nstring.h"
#include "mallocvar.h"

/* Max number of entries we will put in the XPM color map
   Don't forget the one entry for transparency.

   We don't use this anymore.  Ppmtoxpm now has no arbitrary limit on
   the number of colors.

   We're assuming this isn't in fact an XPM format requirement, because
   we've seen it work with 257, and 257 seems to be common, because it's
   the classic 256 colors, plus transparency.  The value was 256 for
   ages before we added transparency capability to this program in May
   2001.  At that time, we started failing with 256 color images.
   Some limit was also necessary before then because ppm_computecolorhash()
   required us to specify a maximum number of colors.  It doesn't anymore.

   If we find out that some XPM processing programs choke on more than
   256 colors, we'll have to readdress this issue.  - Bryan.  2001.05.13.
*/
#define MAXCOLORS    256

#define MAXPRINTABLE 92         /* number of printable ascii chars
                                 * minus \ and " for string compat
                                 * and ? to avoid ANSI trigraphs. */

static const char * const printable =
" .XoO+@#$%&*=-;:>,<1234567890qwertyuipasdfghjklzxcvbnmMNBVCZ\
ASDFGHJKLPIUYTREWQ!~^/()_`'][{}|";


struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFilename;
    const char * name;
    const char * rgb;
    const char * alphaFilename;
    unsigned int hexonly;
    unsigned int verbose;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
    optStruct3 opt;

    unsigned int option_def_index;
    const char * nameOpt;
    unsigned int nameSpec;

    MALLOCARRAY(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0,   "alphamask",   OPT_STRING, &cmdlineP->alphaFilename,
            NULL, 0);
    OPTENT3(0,   "name",        OPT_STRING, &nameOpt,
            &nameSpec, 0);
    OPTENT3(0,   "rgb",         OPT_STRING, &cmdlineP->rgb,
            NULL, 0);
    OPTENT3(0,   "hexonly",     OPT_FLAG, NULL,
            &cmdlineP->hexonly, 0);
    OPTENT3(0,   "verbose",     OPT_FLAG, NULL,
            &cmdlineP->verbose, 0);

    /* Set the defaults */
    cmdlineP->alphaFilename = NULL;  /* no transparency */
    cmdlineP->rgb = NULL;      /* no rgb file specified */

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We may have parms that are negative numbers */

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 == 0)
        cmdlineP->inputFilename = "-";
    else if (argc-1 != 1)
        pm_error("Program takes zero or one argument (filename).  You "
                 "specified %d", argc-1);
    else
        cmdlineP->inputFilename = argv[1];

    /* If output filename not specified, use input filename as default. */
    if (nameSpec)
        cmdlineP->name = nameOpt;
    else if (streq(cmdlineP->inputFilename, "-"))
        cmdlineP->name = "noname";
    else {
        static char name[80+1];
        char *cp;

        STRSCPY(name, cmdlineP->inputFilename);
        cp = strchr(name, '.');
        if (cp)
            *cp = '\0';     /* remove extension */
        cmdlineP->name = name;
    }
}



typedef struct {
    /* Information for an XPM color map entry */
    char *cixel;
       /* XPM color number, as might appear in the XPM raster */
    const char *rgbname;
       /* color name, e.g. "blue" or "#01FF23" */
} CixelMap;



static char *
numstr(unsigned int const input,
       unsigned int const digitCt) {
/*---------------------------------------------------------------------------
   Given a number and a base (MAXPRINTABLE), this routine prints the
   number into a malloc'ed string and returns it.  The length of the
   string is specified by 'digitCt'.  It is not printed in decimal or
   any other number system you're used to.  Rather, each digit is from
   the set printable[], which contains MAXPRINTABLE characters; the
   character printable[n] has value n.

   The string is printable[0] filled with high order zeroes.

   Example:
     Assume:
       printable[0] == 'q'
       printable[1] == '%'
       MAXPRINTABLE == 2
       digitCt == 5
       input == 3
     Result is the malloc'ed string "qqq%%"
---------------------------------------------------------------------------*/
    char * str;  /* malloc'ed */
    char * p;
    unsigned int i;

    /* Allocate memory for printed number.  Abort if error. */
    MALLOCARRAY_NOFAIL(str, digitCt + 1);

    i = input; /* initial value */
    /* Generate characters starting with least significant digit. */
    p = str + digitCt;  /* initial value */
    *p-- = '\0';            /* nul terminate string */
    while (p >= str) {
        unsigned int const d = i % MAXPRINTABLE;

        i /= MAXPRINTABLE;
        *p-- = printable[d];
    }

    if (i != 0)
        pm_error("Overflow converting %u to %u digits in base %u",
                 input, digitCt, MAXPRINTABLE);

    return str;
}



static unsigned int
xpmMaxvalFromMaxval(pixval const maxval) {

    unsigned int retval;

    /* Determine how many hex digits we'll be normalizing to if the rgb
       value doesn't match a color mnemonic.
    */
    if (maxval <= 0x000F)
        retval = 0x000F;
    else if (maxval <= 0x00FF)
        retval = 0x00FF;
    else if (maxval <= 0x0FFF)
        retval = 0x0FFF;
    else if (maxval <= 0xFFFF)
        retval = 0xFFFF;
    else
        pm_error("Internal error - impossible maxval %x", maxval);

    return retval;
}



static unsigned int
charsPerPixelForSize(unsigned int const cmapSize) {
/*----------------------------------------------------------------------------
   The number of characters it will take to represent a pixel in an XPM that
   has a colormap of size 'cmapSize'.  Each pixel in an XPM represents an
   index into the colormap with a base-92 scheme where each character is one
   of 92 printable characters.  Ergo, if the colormap has at most 92
   characters, each pixel will be represented by a single character.  If it
   has more than 92, but at most 92*92, it will take 2, etc.

   If cmapSize is zero, there's no such thing as an XPM pixel, so we return an
   undefined value.
-----------------------------------------------------------------------------*/
    unsigned int charsPerPixel;

    if (cmapSize > 0) {
        unsigned int j;

        for (charsPerPixel = 0, j = cmapSize-1; j > 0; ++charsPerPixel)
            j /= MAXPRINTABLE;
    }
    return charsPerPixel;
}



static void
genCmap(colorhist_vector const chv,
        unsigned int     const ncolors,
        pixval           const maxval,
        ppm_ColorDict *  const colorDictP,
        bool             const includeTransparent,
        CixelMap **      const cmapP,
        unsigned int *   const transIndexP,
        unsigned int *   const cmapSizeP,
        unsigned int *   const charsPerPixelP) {
/*----------------------------------------------------------------------------
   Generate the XPM color map in CixelMap format (which is just a step
   away from the actual text that needs to be written the XPM file).  The
   color map is defined by 'chv', which contains 'ncolors' colors which
   have maxval 'maxval'.

   Output is in newly malloc'ed storage, with its address returned as
   *cmapP.  We return the number of entries in it as *cmapSizeP.

   This map includes an entry for transparency, whether the raster uses
   it or not.  We return its index as *transIndexP.

   In the map, identify colors by the names given by *colorDictP.  If a color
   is not in *colorDictP, use hexadecimal notation in the output colormap.

   But if *colorDictP is null, don't use color names at all.  Just use
   hexadecimal notation.

   Return as *charsPerPixel the number of characters, or digits, that
   will be needed in the XPM raster to form an index into this color map.
-----------------------------------------------------------------------------*/
    unsigned int const cmapSize = ncolors + (includeTransparent ? 1 : 0);

    CixelMap * cmap;  /* malloc'ed */
    unsigned int cmapIndex;
    unsigned int charsPerPixel;
    unsigned int xpmMaxval;

    MALLOCARRAY(cmap, cmapSize);
    if (cmapP == NULL)
        pm_error("Can't get memory for a %u-entry color map", cmapSize);

    xpmMaxval = xpmMaxvalFromMaxval(maxval);

    charsPerPixel = charsPerPixelForSize(cmapSize);

    /*
     * Generate the character-pixel string and the rgb name for each
     * colormap entry.
     */
    for (cmapIndex = 0; cmapIndex < ncolors; ++cmapIndex) {
        pixel const color = chv[cmapIndex].color;

        pixel color255;
            /* The color, scaled to maxval 255 */
        const char * colorname;  /* malloc'ed */
        /*
         * The character-pixel string is simply a printed number in base
         * MAXPRINTABLE where the digits of the number range from
         * printable[0] .. printable[MAXPRINTABLE-1] and the printed length
         * of the number is 'charsPerPixel'.
         */
        cmap[cmapIndex].cixel = numstr(cmapIndex, charsPerPixel);

        PPM_DEPTH(color255, color, maxval, 255);

        if (!colorDictP)
            colorname = NULL;
        else {
            int colornameIndex;
            colornameIndex = ppm_lookupcolor(colorDictP->cht, &color255);
            if (colornameIndex >= 0)
                colorname = strdup(colorDictP->name[colornameIndex]);
            else
                colorname = NULL;
        }
        if (colorname)
            cmap[cmapIndex].rgbname = colorname;
        else {
            /* Color has no name; represent it in hexadecimal */

            pixel scaledColor;
            const char * hexString;  /* malloc'ed */

            PPM_DEPTH(scaledColor, color, maxval, xpmMaxval);

            pm_asprintf(&hexString, xpmMaxval == 0x000F ? "#%X%X%X" :
                        xpmMaxval == 0x00FF ? "#%02X%02X%02X" :
                        xpmMaxval == 0x0FFF ? "#%03X%03X%03X" :
                        "#%04X%04X%04X",
                        PPM_GETR(scaledColor),
                        PPM_GETG(scaledColor),
                        PPM_GETB(scaledColor)
                );

            if (hexString == NULL)
                pm_error("Unable to allocate storage for hex string");
            cmap[cmapIndex].rgbname = hexString;
        }
    }

    if (includeTransparent) {
        /* Add the special transparency entry to the colormap */
        unsigned int const transIndex = ncolors;
        cmap[transIndex].rgbname = strdup("None");
        cmap[transIndex].cixel = numstr(transIndex, charsPerPixel);
        *transIndexP = transIndex;
    }
    *cmapP          = cmap;
    *cmapSizeP      = cmapSize;
    *charsPerPixelP = charsPerPixel;
}



static void
destroyCmap(CixelMap *   const cmap,
            unsigned int const cmapSize) {

    unsigned int i;

    /* Free the real color entries */
    for (i = 0; i < cmapSize; ++i) {
        pm_strfree(cmap[i].rgbname);
        free(cmap[i].cixel);
    }

    free(cmap);
}



static void
writeXpmFile(FILE *           const ofP,
             pixel **         const pixels,
             gray **          const alpha,
             pixval           const alphamaxval,
             char             const name[],
             unsigned int     const cols,
             unsigned int     const rows,
             unsigned int     const cmapSize,
             unsigned int     const charsPerPixel,
             const CixelMap * const cmap,
             colorhash_table  const cht,
             unsigned int     const transIndex) {
/*----------------------------------------------------------------------------
   Write the whole XPM file to the open stream 'ofP'.

   'cmap' is the colormap to be placed in the XPM.  'cmapSize' is the number
   of entries in it.  'cht' is a hash table that gives you an index into
   'cmap' given a color.  'transIndex' is the index into cmap of the
   transparent color, and is valid only if 'alpha' is non-null (otherwise,
   cmap might not contain a transparent color).
-----------------------------------------------------------------------------*/
    /* First the header */
    printf("/* XPM */\n");
    fprintf(ofP, "static char *%s[] = {\n", name);
    fprintf(ofP, "/* width height ncolors chars_per_pixel */\n");
    fprintf(ofP, "\"%u %u %u %u\",\n", cols, rows, cmapSize, charsPerPixel);

    {
        unsigned int i;
        /* Write out the colormap (part of header) */
        fprintf(ofP, "/* colors */\n");
        for (i = 0; i < cmapSize; i++) {
            fprintf(ofP, "\"%s c %s\",\n", cmap[i].cixel, cmap[i].rgbname);
        }
    }
    {
        unsigned int row;

        /* And now the raster */
        fprintf(ofP, "/* pixels */\n");
        for (row = 0; row < rows; ++row) {
            unsigned int col;
            fprintf(ofP, "\"");
            for (col = 0; col < cols; ++col) {
                if (alpha && alpha[row][col] <= alphamaxval/2)
                    /* It's a transparent pixel */
                    fprintf(ofP, "%s", cmap[transIndex].cixel);
                else
                    fprintf(ofP, "%s",
                            cmap[ppm_lookupcolor(cht,
                                                 &pixels[row][col])].cixel);
            }
            fprintf(ofP, "\"%s\n", (row == (rows - 1) ? "" : ","));
        }
    }
    /* And close up */
    fprintf(ofP, "};\n");
}



static void
readAlpha(const char * const fileNm,
          gray ***     const alphaP,
          unsigned int const cols,
          unsigned int const rows,
          pixval *     const alphamaxvalP) {

    FILE * alphaFileP;
    int alphacols, alpharows;

    alphaFileP = pm_openr(fileNm);
    *alphaP = pgm_readpgm(alphaFileP, &alphacols, &alpharows, alphamaxvalP);
    pm_close(alphaFileP);

    if (cols != alphacols || rows != alpharows)
        pm_error("Alpha mask is not the same dimensions as the "
                 "image.  Image is %u by %u, while mask is %d x %d.",
                 cols, rows, alphacols, alpharows);
}



static void
computecolorhash(pixel **          const pixels,
                 gray **           const alpha,
                 unsigned int      const cols,
                 unsigned int      const rows,
                 gray              const alphaMaxval,
                 colorhash_table * const chtP,
                 unsigned int *    const ncolorsP,
                 bool *            const transparentSomewhereP) {
/*----------------------------------------------------------------------------
   Compute a colorhash_table with one entry for each color in 'pixels' that
   is not mostly transparent according to alpha mask 'alpha' (which has
   maxval 'alphaMaxval').  alpha == NULL means all pixels are opaque.

   The value associated with the color in the hash we build is meaningless.

   Return the colorhash_table as *chtP, and the number of colors in it
   as *ncolorsP.  Return *transparentSomewhereP == true iff the image has
   at least one pixel that is mostly transparent.
-----------------------------------------------------------------------------*/
    colorhash_table cht;
    unsigned int row;
    bool foundTransparent;
    unsigned int ncolors;

    cht = ppm_alloccolorhash();

    /* Go through the entire image, building a hash table of colors. */
    for (row = 0, ncolors = 0, foundTransparent = false; row < rows; ++row) {
        unsigned int col;

        for (col = 0; col < cols; ++col) {
            if (!alpha || alpha[row][col] > alphaMaxval/2) {
                /* It's mostly opaque, so add this color to the hash
                   if it's not already there.
                */
                pixel const color = pixels[row][col];
                int const lookupRc = ppm_lookupcolor(cht, &color);

                if (lookupRc < 0) {
                    /* It's not in the hash yet, so add it */
                    if (ncolors > UINT_MAX - 10)
                        pm_error("Number of colors (> %u) "
                                 "is uncomputably large",
                                 ncolors);
                    ppm_addtocolorhash(cht, &color, 0);
                    ++ncolors;
                }
            } else
                *transparentSomewhereP = true;
        }
    }
    *chtP                  = cht;
    *ncolorsP              = ncolors;
    *transparentSomewhereP = foundTransparent;
}



static void
computeColormap(pixel **           const pixels,
                gray **            const alpha,
                unsigned int       const cols,
                unsigned int       const rows,
                gray               const alphaMaxval,
                colorhist_vector * const chvP,
                colorhash_table *  const chtP,
                unsigned int *     const ncolorsP,
                bool *             const transparentSomewhereP) {
/*----------------------------------------------------------------------------
   Compute the color map for the image 'pixels', which is 'cols' by 'rows',
   in Netpbm data structures (a colorhist_vector for index-to-color lookups
   and a colorhash_table for color-to-index lookups).

   Exclude pixels that alpha mask 'alpha' (which has maxval 'alphaMaxval')
   says are mostly transparent.  alpha == NULL means all pixels are opaque.

   We return as *chvP an array of the colors present in 'pixels', excluding
   those that are mostly transparent.  We return as *ncolorsP the number of
   such colors.  We return *transparentSomewhereP == true iff the image has at
   least one pixel that is mostly transparent.
-----------------------------------------------------------------------------*/
    colorhash_table histCht;

    pm_message("(Computing colormap...");
    computecolorhash(pixels, alpha, cols, rows, alphaMaxval,
                     &histCht, ncolorsP, transparentSomewhereP);
    pm_message("...Done.  %d colors found.)", *ncolorsP);

    *chvP = ppm_colorhashtocolorhist(histCht, *ncolorsP);
    ppm_freecolorhash(histCht);
    /* Despite the name, the following generates an index on *chvP,
       with which given a color you can quickly find the entry number
       in *chvP that contains that color.
    */
    *chtP = ppm_colorhisttocolorhash(*chvP, *ncolorsP);
}



int
main(int argc, const char * *argv) {

    FILE * ifP;
    int rows, cols;
    unsigned int ncolors;
    bool transparentSomewhere;
    pixval maxval, alphaMaxval;
    colorhash_table cht;
    colorhist_vector chv;

    ppm_ColorDict * colorDictP;  /* The color name dictionary we use */

    pixel ** pixels;
    gray ** alpha;

    /* Used for rgb value -> character-pixel string mapping */
    CixelMap * cmap;  /* malloc'ed */
        /* The XPM colormap */
    unsigned int cmapSize;
        /* Number of entries in 'cmap' */
    unsigned int transIndex;
        /* Index into 'cmap' of the transparent color, if there is one */

    unsigned int charsPerPixel;

    struct CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFilename);
    pixels = ppm_readppm(ifP, &cols, &rows, &maxval);
    pm_close(ifP);

    if (cmdline.alphaFilename)
        readAlpha(cmdline.alphaFilename, &alpha, cols, rows, &alphaMaxval);
    else
        alpha = NULL;

    computeColormap(pixels, alpha, cols, rows, alphaMaxval,
                    &chv, &cht, &ncolors, &transparentSomewhere);

    if (cmdline.hexonly)
        colorDictP = NULL;
    else if (cmdline.rgb)
        colorDictP = ppm_colorDict_new(cmdline.rgb, true);
    else
        colorDictP = ppm_colorDict_new(NULL, false);

    /* Now generate the character-pixel colormap table. */
    genCmap(chv, ncolors, maxval, colorDictP, transparentSomewhere,
            &cmap, &transIndex, &cmapSize, &charsPerPixel);

    writeXpmFile(stdout, pixels, alpha, alphaMaxval,
                 cmdline.name, cols, rows, cmapSize,
                 charsPerPixel, cmap, cht, transIndex);

    if (colorDictP)
        ppm_colorDict_destroy(colorDictP);
    destroyCmap(cmap, cmapSize);
    ppm_freearray(pixels, rows);
    if (alpha)
        pgm_freearray(alpha, rows);

    return 0;
}



