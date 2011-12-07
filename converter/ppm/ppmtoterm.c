/* ppmtoterm.c - convert a portable pixmap into an ISO 6429 (ANSI) color ascii
** text.
**
**  Copyright (C) 2002 by Ero Carrera (ecarrera@lightforge.com)
**  Partially based on,
**      ppmtoyuv.c by Marc Boucher,
**      ppmtolj.c by Jonathan Melvin and
**      ppmtogif.c by Jef Poskanzer
**
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
** 14/Aug/2002: First version.
**
*/

#include <assert.h>
#include <string.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "ppm.h"


struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* Name of input file */
    unsigned int verbose;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct cmdlineInfo * const cmdlineP) {

    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "verbose", OPT_FLAG, NULL, &cmdlineP->verbose, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else {
        cmdlineP->inputFileName = argv[1];

        if (argc-1 > 1)
            pm_error("Too many arguments: %u.  The only possible argument "
                     "is the input file name", argc-1);
    }
}



#define ESC "\x1B\x5B"
#define ANSI_BRIGHT_CMD_PAT ESC "%dm"
#define ANSI_FGCOLOR_CMD_PAT ESC "3%dm"
#define ANSI_BGCOLOR_CMD_PAT ESC "4%dm"
#define MAX_ANSI_STR_LEN 16
#define NUM_COLORS 128
    /* 1 bit each RGB = 8 colors.
       8 BG colors * 8 FG colors * 2 brightnesses
    */



static void
generatePalette(unsigned char        rgb[NUM_COLORS][3], 
                char                 ansiCode[NUM_COLORS][MAX_ANSI_STR_LEN],
                unsigned int * const paletteSizeP) {
/*----------------------------------------------------------------------------
  Generate some sort of color palette mixing the available colors as different
  values of background, foreground & brightness.

  We return as rgb[I] the RGB triple for the color with palette index I.
  Component intensities are in the range 0..255.  rgb[I][0] is red;
  rgb[I][1] is green; rgb[I][2] is blue.

  We return as ansiCode[I] the sequence you send to a terminal to generate
  the color with palette index I.
-----------------------------------------------------------------------------*/
    unsigned int idx;
        /* palette index of the color being considered */
    unsigned int bgColorCode;
        /* This is the ANSI color code for the background.  An ANSI color code
           is a 3 bit code in which LSB means red; middle bit means green, and
           MSB means blue.
        */

    /* We develop the palette backwards: consider every permutation of the
       three terminal controls -- background, foreground, and brightness --
       and for each figure out what RGB color it represents and fill in
       that element of RGB[][]
    */

    for (bgColorCode = 0, idx = 0; bgColorCode < 8; ++bgColorCode) {
        unsigned int brightness;  /* 0 = dim; 1 = bright */
        for (brightness = 0; brightness < 2; ++brightness) {
            unsigned int fgColorCode;
                /* ANSI color code for the foreground.  See bgColorCode. */
            for (fgColorCode = 0; fgColorCode < 8; ++fgColorCode) {
                unsigned int rgbComp;
                    /* 0 = red; 1 = green; 2 = blue */
                for (rgbComp = 0; rgbComp < 3; ++rgbComp) {
                    assert (idx < NUM_COLORS);
                    rgb[idx][rgbComp] = 0x00;  /* initial value */
                    if ((fgColorCode & (0x1 << rgbComp)) != 0) {
                        rgb[idx][rgbComp] |= 0xC0;
                        if (brightness == 1)
                            rgb[idx][rgbComp] |= 0x3F;
                    }
                    if ((bgColorCode & (0x1 << rgbComp)) != 0)
                        rgb[idx][rgbComp] |= 0x80;
                }
                sprintf(ansiCode[idx],
                        ANSI_BRIGHT_CMD_PAT
                        ANSI_FGCOLOR_CMD_PAT
                        ANSI_BGCOLOR_CMD_PAT,
                        brightness, fgColorCode, bgColorCode);
                ++idx;
            }
        }
    }
    *paletteSizeP = idx;
}



static void
lookupInPalette(pixel          const pixel,
                pixval         const maxval,
                unsigned char        rgb[NUM_COLORS][3], 
                unsigned int   const palLen,
                unsigned int * const paletteIdxP) {
/*----------------------------------------------------------------------------
   Look up the color 'pixel' (which has maxval 'maxval') in the palette
   palette[], which has 'palLen' elements and uses maxval 255.  Return the
   index into palette[] of the color that is closes to 'pixel' as
   *paletteIdxP.
-----------------------------------------------------------------------------*/
    pixval const r = PPM_GETR(pixel) * 255 / maxval;
    pixval const g = PPM_GETG(pixel) * 255 / maxval;
    pixval const b = PPM_GETB(pixel) * 255 / maxval;

    unsigned int paletteIdxSoFar;
    unsigned int dist;
    unsigned int i;
            
    /* The following loop calculates the index that corresponds to the
       minimum color distance between the given RGB values and the
       values available in the palette.
    */
    for (i = 0, dist = SQR(255)*3, paletteIdxSoFar = 0; i < palLen; ++i) {
        pixval const pr=rgb[i][0];
        pixval const pg=rgb[i][1];
        pixval const pb=rgb[i][2];
        unsigned int const j = SQR(r-pr) + SQR(b-pb) + SQR(g-pg);

        if (j  < dist) {
            dist = j;
            paletteIdxSoFar = i;
        }
    }
    *paletteIdxP = paletteIdxSoFar;
}



int
main(int argc, const char ** argv) {

    FILE *          ifP;
    pixel **        pixels;
    int             rows, cols;
    unsigned int    row;
    unsigned int    palLen;
    pixval          maxval;
    struct cmdlineInfo cmdline;
    unsigned char   rgb[NUM_COLORS][3];
    char            ansiCode[NUM_COLORS][MAX_ANSI_STR_LEN];

    pm_proginit(&argc, argv);    

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);
    
    pixels = ppm_readppm(ifP, &cols, &rows, &maxval);

    pm_close(ifP);
        
    generatePalette(rgb, ansiCode, &palLen);
    
    for (row = 0; row < rows; ++row) {
        unsigned int col;
        for (col = 0; col < cols; ++col) {
            unsigned int paletteIdx;

            lookupInPalette(pixels[row][col], maxval, rgb, palLen,
                            &paletteIdx);

            printf("%s\xB1", ansiCode[paletteIdx]);
        }
        printf(ESC "\x30m\n");
    }
    printf(ESC "\x30m");

    ppm_freearray(pixels, rows);
    
    return 0;
}
