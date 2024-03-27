/*
** Copyright (C) 1989 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "netpbm/pm_c_util.h"
#include "netpbm/mallocvar.h"
#include "netpbm/nstring.h"
#include "ppm.h"
#include "colorname.h"
#include "pam.h"


pixel
ppm_parsecolor2(const char * const colorname,
                pixval       const maxval,
                int          const closeOk) {

    tuple const color = pnm_parsecolor2(colorname, maxval, closeOk);

    pixel retval;

    PPM_PUTR(retval, color[PAM_RED_PLANE]);
    PPM_PUTG(retval, color[PAM_GRN_PLANE]);
    PPM_PUTB(retval, color[PAM_BLU_PLANE]);

    free(color);

    return retval;
}



pixel
ppm_parsecolor(const char * const colorname,
               pixval       const maxval) {

    return ppm_parsecolor2(colorname, maxval, true);
}



char *
ppm_colorname(const pixel * const colorP,
              pixval        const maxval,
              int           const hexok)   {

    int r, g, b;
    FILE * fileP;
    static char colorname[200];
        /* Null string means no suitable name so far */

    if (maxval == 255) {
        r = PPM_GETR(*colorP);
        g = PPM_GETG(*colorP);
        b = PPM_GETB(*colorP);
    } else {
        r = (int) PPM_GETR(*colorP) * 255 / (int) maxval;
        g = (int) PPM_GETG(*colorP) * 255 / (int) maxval;
        b = (int) PPM_GETB(*colorP) * 255 / (int) maxval;
    }

    fileP = pm_openColornameFile(NULL, !hexok);

    if (!fileP)
        STRSCPY(colorname, "");
    else {
        int bestDiff;
        bool eof;

        for (bestDiff = 32767, eof = false;
             !eof && bestDiff > 0; ) {
            struct colorfile_entry const ce = pm_colorget(fileP);
            if (ce.colorname)  {
                int const thisDiff =
                    abs(r - (int)ce.r) +
                    abs(g - (int)ce.g) +
                    abs(b - (int)ce.b);

                if (thisDiff < bestDiff) {
                    bestDiff = thisDiff;
                    STRSCPY(colorname, ce.colorname);
                }
            } else
                eof = true;
        }
        fclose(fileP);

        if (bestDiff == 32767) {
            /* Color file contain no entries, so we can't even pick a close
               one
            */
            STRSCPY(colorname, "");
        } else if (bestDiff > 0 && hexok) {
            /* We didn't find an exact match and user is willing to accept
               hex, so we don't have to use an approximate match.
            */
            STRSCPY(colorname, "");
        }
    }

    if (streq(colorname, "")) {
        if (hexok) {
            /* Color lookup failed, but caller is willing to take an X11-style
               hex specifier, so return that.
            */
            sprintf(colorname, "#%02x%02x%02x", r, g, b);
        } else {
            pm_error("Couldn't find any name colors at all");
        }
    }

    return colorname;
}



#define MAXCOLORNAMES 1000u

static const char **
allocColorNames() {

    const char ** colornames;

    MALLOCARRAY(colornames, MAXCOLORNAMES);

    if (colornames) {
        unsigned int i;
        for (i = 0; i < MAXCOLORNAMES; ++i)
            colornames[i] = NULL;
    }
    return colornames;
}



static colorhash_table
allocColorHash(void) {

    colorhash_table cht;
    jmp_buf jmpbuf;
    jmp_buf * origJmpbufP;

    if (setjmp(jmpbuf) != 0)
        cht = NULL;
    else {
        pm_setjmpbufsave(&jmpbuf, &origJmpbufP);
        cht = ppm_alloccolorhash();
    }
    pm_setjmpbuf(origJmpbufP);

    return cht;
}



static void
processColorfileEntry(struct colorfile_entry const ce,
                      colorhash_table        const cht,
                      const char **          const colornames,
                      pixel *                const colors,
                      unsigned int *         const colornameIndexP,
                      const char **          const errorP) {

    if (*colornameIndexP >= MAXCOLORNAMES)
        pm_asprintf(errorP, "Too many colors in colorname dictionary.  "
                    "Max allowed is %u", MAXCOLORNAMES);
    else {
        pixel color;

        PPM_ASSIGN(color, ce.r, ce.g, ce.b);

        if (ppm_lookupcolor(cht, &color) >= 0) {
            /* The color is already in the hash, which means we saw it
               earlier in the file.  We prefer the first name that the
               file gives for each color, so we just ignore the
               current entry.
            */
            *errorP = NULL;
        } else {
            ppm_addtocolorhash(cht, &color, *colornameIndexP);
            colornames[*colornameIndexP] = pm_strdup(ce.colorname);
            colors[*colornameIndexP] = color;
            if (colornames[*colornameIndexP] == pm_strsol)
                pm_asprintf(errorP, "Unable to allocate space for color name");
            else {
                *errorP = NULL;
                ++(*colornameIndexP);
            }
        }
    }
}



static void
openColornameFile(const char *  const fileName,
                  bool          const mustOpen,
                  FILE **       const filePP,
                  const char ** const errorP) {

    jmp_buf jmpbuf;
    jmp_buf * origJmpbufP;

    if (setjmp(jmpbuf) != 0) {
        pm_asprintf(errorP, "Failed to open color name file");
    } else {
        pm_setjmpbufsave(&jmpbuf, &origJmpbufP);

        *filePP = pm_openColornameFile(fileName, mustOpen);

        *errorP = NULL;  /* Would have longjmped if there were a problem */
    }
    pm_setjmpbuf(origJmpbufP);
}



static void
readOpenColorFile(FILE *          const colorFileP,
                  unsigned int *  const nColorsP,
                  const char **   const colornames,
                  pixel *         const colors,
                  colorhash_table const cht,
                  const char **   const errorP) {
/*----------------------------------------------------------------------------
   Read the color dictionary file *colorFileP and add the colors in it
   to colornames[], colors[], and 'cht'.

   colornames[] and colors[] must be allocated with MAXCOLORNAMES entries
   at entry.

   We may add colors to 'cht' even if we fail.
-----------------------------------------------------------------------------*/
    unsigned int nColorsDone;
    bool done;

    nColorsDone = 0;
    done = false;
    *errorP = NULL;

    while (!done && !*errorP) {
        struct colorfile_entry const ce = pm_colorget(colorFileP);

        if (!ce.colorname)
            done = true;
        else
            processColorfileEntry(ce, cht, colornames, colors,
                                  &nColorsDone, errorP);
    }
    *nColorsP = nColorsDone;

    if (*errorP) {
        unsigned int colorIndex;

        for (colorIndex = 0; colorIndex < nColorsDone; ++colorIndex)
            pm_strfree(colornames[colorIndex]);
    }
}



static void
readColorFile(const char *    const fileName,
              bool            const mustOpen,
              unsigned int *  const nColorsP,
              const char **   const colornames,
              pixel *         const colors,
              colorhash_table const cht,
              const char **   const errorP) {
/*----------------------------------------------------------------------------
   Read the color dictionary file named 'fileName' and add the colors in it
   to colornames[], colors[], and 'cht'.  Return as *nColorsP the number
   of colors in it.

   If the file is not openable (e.g. no file by that name exists), abort the
   program if 'mustOpen' is true; otherwise, return values indicating a
   dictionary with no colors.

   colornames[] and colors[] must be allocated with MAXCOLORNAMES entries
   at entry.

   We may add colors to 'cht' even if we fail.
-----------------------------------------------------------------------------*/
    FILE * colorFileP;

    openColornameFile(fileName, mustOpen, &colorFileP, errorP);
    if (!*errorP) {
        if (colorFileP == NULL) {
            /* Couldn't open it, but Caller says treat same as
               empty file
            */
            *nColorsP = 0;
            *errorP = NULL;
        } else {
            readOpenColorFile(colorFileP, nColorsP, colornames, colors, cht,
                              errorP);

            fclose(colorFileP);
        }
    }
}



static void
readcolordict(const char *      const fileName,
              bool              const mustOpen,
              unsigned int *    const nColorsP,
              const char ***    const colorNamesP,
              pixel **          const colorsP,
              colorhash_table * const chtP,
              const char **     const errorP) {
/*----------------------------------------------------------------------------
  Same as 'ppm_readcolordict' except that a) if we fail, we return a message
  as *errorP instead of issuing a message; and b) we always return all three
  return values.
-----------------------------------------------------------------------------*/
    const char ** colorNames;
        /* List of all the color names in the dictionary */

    colorNames = allocColorNames();

    if (colorNames == NULL)
        pm_asprintf(errorP, "Unable to allocate space for colorname table.");
    else {
        pixel * colors;
            /* colors[i] is the color that goes with name colorNames[i] */

        MALLOCARRAY(colors, MAXCOLORNAMES);

        if (colors == NULL)
            pm_asprintf(errorP, "Unable to allocate space for color table.");
        else {
            colorhash_table cht;
                /* Hash table mapping colorNames[] to colors[] */

            cht = allocColorHash();

            if (cht == NULL)
                pm_asprintf(errorP, "Unable to allocate space for color hash");
            else {
                readColorFile(fileName, mustOpen,
                              nColorsP, colorNames, colors, cht,
                              errorP);

                if (*errorP)
                    ppm_freecolorhash(cht);
                else
                    *chtP = cht;
            }
            if (*errorP)
                free(colors);
            else
                *colorsP = colors;
        }
        if (*errorP)
            free(colorNames);
        else
            *colorNamesP = colorNames;
    }
}



void
ppm_readcolordict(const char *      const fileName,
                  int               const mustOpen,
                  unsigned int *    const nColorsP,
                  const char ***    const colorNamesP,
                  pixel **          const colorsP,
                  colorhash_table * const chtP) {
/*----------------------------------------------------------------------------
   Read the color dictionary from the file named 'fileName'.  If we can't open
   the file (e.g. because it does not exist), and 'mustOpen' is false, return
   an empty dictionary (it contains no colors).  But if 'mustOpen' is true,
   abort the program instead of returning an empty dictionary.

   Return as *nColorsP the number of colors in the dictionary.

   Return as *colorNamesP the names of those colors.  *colorNamesP is a
   malloced array that Caller must free with ppm_freecolorNames().
   The first *nColorsP entries are valid; *chtP contains indices into this
   array.

   Return as *colorsP the colors.  *colorsP is a malloced array of size
   MAXCOLORS with the first elements filled in and the rest undefined.

   Return as *chtP a color hash table mapping each color in the dictionary
   to the index into *colorNamesP for the name of the color.

   Each of 'nColorsP, 'colorNamesP', and 'colorsP' may be null, in which case
   we do not return the corresponding information (or allocate memory for it).
-----------------------------------------------------------------------------*/
    colorhash_table cht;
    const char ** colorNames;
    pixel * colors;
    unsigned int nColors;
    const char * error;

    readcolordict(fileName, mustOpen, &nColors, &colorNames, &colors, &cht,
                  &error);

    if (error) {
        pm_errormsg("%s", error);
        pm_strfree(error);
        pm_longjmp();
    } else {
        if (chtP)
            *chtP = cht;
        else
            ppm_freecolorhash(cht);
        if (colorNamesP)
            *colorNamesP = colorNames;
        else
            ppm_freecolornames(colorNames);
        if (colorsP)
            *colorsP = colors;
        else
            ppm_freerow(colors);
        if (nColorsP)
            *nColorsP = nColors;
    }
}



void
ppm_readcolornamefile(const char *      const fileName,
                      int               const mustOpen,
                      colorhash_table * const chtP,
                      const char ***    const colorNamesP) {

    ppm_readcolordict(fileName, mustOpen, NULL, colorNamesP, NULL, chtP);
}



void
ppm_freecolornames(const char ** const colorNames) {

    unsigned int i;

    for (i = 0; i < MAXCOLORNAMES; ++i)
        if (colorNames[i])
            free((char *)colorNames[i]);

    free(colorNames);
}



static unsigned int
nonnegative(unsigned int const arg) {

    if ((int)(arg) < 0)
        return 0;
    else
        return arg;
}



pixel
ppm_color_from_ycbcr(unsigned int const y,
                     int          const cb,
                     int          const cr) {
/*----------------------------------------------------------------------------
   Return the color that has luminance 'y', blue chrominance 'cb', and
   red chrominance 'cr'.

   The 3 input values can be on any scale (as long as it's the same
   scale for all 3) and the maxval of the returned pixel value is the
   same as that for the input values.

   Rounding may cause an output value to be greater than the maxval.
-----------------------------------------------------------------------------*/
    pixel retval;

    PPM_ASSIGN(retval,
               y + 1.4022 * cr,
               nonnegative(y - 0.7145 * cr - 0.3456 * cb),
               y + 1.7710 * cb
        );

    return retval;
}



pixel
ppm_color_from_hsv(struct hsv const hsv,
                   pixval     const maxval) {

    pixel retval;
    double R, G, B;

    if (hsv.s == 0) {
        R = hsv.v;
        G = hsv.v;
        B = hsv.v;
    } else {
        unsigned int const sectorSize = 60;
            /* Color wheel is divided into six 60 degree sectors. */
        unsigned int const sector = (hsv.h/sectorSize);
            /* The sector in which our color resides.  Value is in 0..5 */
        double const f = (hsv.h - sector*sectorSize)/60;
            /* The fraction of the way the color is from one side of
               our sector to the other side, going clockwise.  Value is
               in [0, 1).
            */
        double const m = (hsv.v * (1 - hsv.s));
        double const n = (hsv.v * (1 - (hsv.s * f)));
        double const k = (hsv.v * (1 - (hsv.s * (1 - f))));

        switch (sector) {
        case 0:
            R = hsv.v;
            G = k;
            B = m;
            break;
        case 1:
            R = n;
            G = hsv.v;
            B = m;
            break;
        case 2:
            R = m;
            G = hsv.v;
            B = k;
            break;
        case 3:
            R = m;
            G = n;
            B = hsv.v;
            break;
        case 4:
            R = k;
            G = m;
            B = hsv.v;
            break;
        case 5:
            R = hsv.v;
            G = m;
            B = n;
            break;
        default:
            pm_error("Invalid H value passed to color_from_HSV: %f", hsv.h);
        }
    }
    PPM_ASSIGN(retval,
               ppm_unnormalize(R, maxval),
               ppm_unnormalize(G, maxval),
               ppm_unnormalize(B, maxval));

    return retval;
}



struct hsv
ppm_hsv_from_color(pixel  const color,
                   pixval const maxval) {

    double const epsilon = 1e-5;

    double const R = (double)PPM_GETR(color) / maxval;
    double const G = (double)PPM_GETG(color) / maxval;
    double const B = (double)PPM_GETB(color) / maxval;

    enum hueSector {SECTOR_RED, SECTOR_GRN, SECTOR_BLU};
    enum hueSector hueSector;

    struct hsv retval;
    double range;

    if (R >= G) {
        if (R >= B) {
            hueSector = SECTOR_RED;
            retval.v = R;
        } else {
            hueSector = SECTOR_BLU;
            retval.v = B;
        }
    } else {
        if (G >= B) {
            hueSector = SECTOR_GRN;
            retval.v = G;
        } else {
            hueSector = SECTOR_BLU;
            retval.v = B;
        }
    }

    range = retval.v - MIN(R, MIN(G, B));

    if (retval.v < epsilon)
        retval.s = 0.0;
    else
        retval.s = range/retval.v;

    if (range < epsilon)
        /* It's gray, so hue really has no meaning.  We arbitrarily pick 0 */
        retval.h = 0.0;
    else {
        double const cr = (retval.v - R) / range;
        double const cg = (retval.v - G) / range;
        double const cb = (retval.v - B) / range;

        double angle;  /* hue angle, in range -30 - +330 */

        switch(hueSector) {
        case SECTOR_RED: angle =   0.0 + 60.0 * (cb - cg); break;
        case SECTOR_GRN: angle = 120.0 + 60.0 * (cr - cb); break;
        case SECTOR_BLU: angle = 240.0 + 60.0 * (cg - cr); break;
        }
        retval.h = angle >= 0.0 ? angle : 360 + angle;
    }

    return retval;
}


