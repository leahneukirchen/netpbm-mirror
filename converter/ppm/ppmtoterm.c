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



#define ESC         "\x1B\x5B"
#define NUM_COLORS      128
#define MAX_ANSI_STR_LEN    16



static void
generatePalette(unsigned char        rgb[NUM_COLORS][3], 
                char                 ansiCode[NUM_COLORS][MAX_ANSI_STR_LEN],
                unsigned int * const paletteSizeP) {
/*----------------------------------------------------------------------------
  Generate some sort of color palette mixing the available colors as different
  values of background, foreground & brightness.
-----------------------------------------------------------------------------*/
    unsigned int code;
    unsigned int col;
    unsigned int cd2;
    
    memset((void *)rgb, 0, NUM_COLORS*3);
    memset((void *)ansiCode, 0, NUM_COLORS*MAX_ANSI_STR_LEN);

    for( col = cd2 =0; cd2 < 8; ++cd2) {
        unsigned int b;
        for (b = 0; b < 2; ++b) {
            for (code = 0; code < 8; ++code) {
                unsigned int c;
                for (c = 0; c < 3; ++c) {
                    if ((code & (0x1 << c)) != 0)
                        rgb[col][c] = (192 | (b ? 63 : 0));
                    if ((cd2 & (0x1 << c)) != 0)
                        rgb[col][c] |= 0x80;
                }
                sprintf(ansiCode[col],
                        ESC"%dm"ESC"3%dm"ESC"4%dm",
                        b, code, cd2);
                ++col;
            }
        }
    }
    *paletteSizeP = col;
}



int
main(int argc, const char ** argv) {

    FILE            * ifP;
    pixel           ** pixels;
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
            pixval const r=(int)PPM_GETR(pixels[row][col])*255/maxval;
            pixval const g=(int)PPM_GETG(pixels[row][col])*255/maxval;
            pixval const b=(int)PPM_GETB(pixels[row][col])*255/maxval;
            int val, dist;
            unsigned int i;
            
            /* The following loop calculates the index that corresponds to the
               minimum color distance between the given RGB values and the
               values available in the palette.
            */
            for (i = 0, dist = SQR(255)*3, val = 0; i < palLen; ++i) {
                pixval const pr=rgb[i][0];
                pixval const pg=rgb[i][1];
                pixval const pb=rgb[i][2];
                unsigned int const j = SQR(r-pr) + SQR(b-pb) + SQR(g-pg);
                if (j  < dist) {
                    dist = j;
                    val = i;
                }
            }
            printf("%s%c", ansiCode[val], 0xB1);
        }
        printf(ESC"\x30m\n");
    }
    printf(ESC"\x30m");

    ppm_freearray(pixels, rows);
    
    return 0;
}
