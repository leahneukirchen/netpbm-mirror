/* ppmtoicr.c - convert a portable pixmap to NCSA ICR protocol
**
** Copyright (C) 1990 by Kanthan Pillay (svpillay@Princeton.EDU)
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
#include <string.h>
#include "ppm.h"
#include "pm_c_util.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "nstring.h"

#define MAXCOLORCT 256
#define CLUTCOLORCT 768

/* The following are arbitrary limits.  We could not find an official
   format specification for this.
*/
#define MAXSIZE 32767
#define MAXDISP 1024
#define MAXNAMELEN 80


struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFilename;
    const char * windowname;  /* NULL means not specified */
    unsigned int expand;
    unsigned int display;
};




static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry *option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;
    unsigned int windowNameSpec;
    unsigned int rleSpec;
    unsigned int expandSpec;
    unsigned int displaySpec;

    MALLOCARRAY(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */

    OPTENT3(0,   "windowname",  OPT_STRING, &cmdlineP->windowname,
            &windowNameSpec, 0);
    OPTENT3(0,   "expand",      OPT_UINT,   &cmdlineP->expand,
            &expandSpec,     0);
    OPTENT3(0,   "display",     OPT_UINT,   &cmdlineP->display,
            &displaySpec,    0);
    OPTENT3(0,   "rle",         OPT_FLAG,   NULL,
            &rleSpec,        0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We may have parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!expandSpec)
        cmdlineP->expand = 1;

    if (!displaySpec)
        cmdlineP->display = 0;

    if (rleSpec)
        pm_error("The -rle command line option no longer exists.");

    if (cmdlineP->expand == 0)
        pm_error("-expand value must be positive.");

    if (cmdlineP->display > MAXDISP)
        pm_error("-display value is too large.  Maximum is %u", MAXDISP);

    if (argc-1 < 1) {
        cmdlineP->inputFilename = "-";
    } else
        cmdlineP->inputFilename = argv[1];

    if (argc-1 > 1)
        pm_error("Program takes zero or one argument (filename).  You "
                 "specified %u", argc-1);

    if (windowNameSpec) {
        if (strlen(cmdlineP->windowname) > MAXNAMELEN)
            pm_error("-windowname value is too long.  (max %u chars)",
                      MAXNAMELEN);
        {
            unsigned int i;
            for (i = 0; cmdlineP->windowname[i]; ++i) {
                if (!isprint (cmdlineP->windowname[i]))
                    pm_error("-window option contains nonprintable character");
                if (cmdlineP->windowname[i] =='^') {
                    /* '^' terminates the window name string in ICR */
                    pm_error("-windowname option value '%s' contains "
                             "disallowed '^' character.",
                             cmdlineP->windowname);
                }
            }
        }
    } else
        cmdlineP->windowname = NULL;
}



static void
validateComputableSize(unsigned int const cols,
                       unsigned int const rows,
                       unsigned int const expand) {
/*----------------------------------------------------------------------------
  We don't have any information on what the limit for these values should be.

  The ICR protocol was used around 1990 when PC main memory was measured in
  megabytes.
-----------------------------------------------------------------------------*/

    if (cols > MAXSIZE / expand)
        pm_error("image width (%f) too large to be processed",
                 (float) cols * expand);
    if (rows > MAXSIZE / expand)
        pm_error("image height (%f) too large to be processed",
                 (float) rows * expand);
}



static void
makeIcrColormap(colorhist_vector const chv,
                unsigned int     const colorCt,
                pixval           const maxval,
                char *           const rgb) {

    unsigned int i;

    if (maxval > 255)
        pm_message("Maxval is not 255 - automatically rescaling colors" );

    for (i = 0; i < CLUTCOLORCT; ++i)
        rgb[i] = 0;

    for (i = 0; i < colorCt; ++i) {
        unsigned int j;

        j = (3 * i);

        if (maxval == 255) {
            rgb[j++] = PPM_GETR(chv[i].color) ;
            rgb[j++] = PPM_GETG(chv[i].color) ;
            rgb[j++] = PPM_GETB(chv[i].color) ;
        } else {
            rgb[j++] = (unsigned int) PPM_GETR(chv[i].color) * 255 / maxval;
            rgb[j++] = (unsigned int) PPM_GETG(chv[i].color) * 255 / maxval;
            rgb[j++] = (unsigned int) PPM_GETB(chv[i].color) * 255 / maxval;
        }
    }
}



static int
colorIndexAtPosition(unsigned int    const x,
                     unsigned int    const y,
                     pixel **        const pixels,
                     colorhash_table const cht) {

    int rc;

    rc = ppm_lookupcolor(cht, &pixels[y][x]);

    /* Every color in the image is in the palette */
    assert(rc >= 0);

    return rc;
}



static void
downloadColormap(char         const rgb[CLUTCOLORCT],
                 const char * const windowName) {

    unsigned int i;

    pm_message("Downloading colormap for %s ...", windowName);

    printf("\033^M;%d;%d;%d;%s^",
           0, MAXCOLORCT, CLUTCOLORCT, windowName);

    for (i = 0; i < CLUTCOLORCT; ++i) {
        unsigned char const c = rgb[i];

        if (c > 31 && c < 123) {
            /* printable ASCII */
            putchar(c);
        } else {
            /* non-printable, so encode it */
            putchar((c >> 6) + 123);
            putchar((c & 0x3f) + 32);
        }
    }
    fflush(stdout);
}



static void
sendOutPicture(pixel **        const pixels,
               unsigned int    const rows,
               unsigned int    const cols,
               colorhash_table const cht,
               int             const expand,
               const char *    const windowName) {

    unsigned int row;

    pm_message("Sending picture data ..." );

    for (row = 0; row < rows; ++row) {
        unsigned int col;
        printf("\033^P;%d;%d;%d;%d;%s^",
               0, row * expand, expand, cols, windowName);
        for (col = 0; col < cols; ++col) {
            unsigned char const c =
                colorIndexAtPosition(col, row, pixels, cht);
            if (c > 31 && c < 123) {
                putchar(c);
            } else {
                putchar((c >> 6) + 123);
                putchar((c & 0x3f) + 32);
            }
        }
    }
    fflush(stdout);
}



static const char *
windowNameFmFileName(const char * const fileName) {

    /* Use the input file name, with unprintable characters and '^'
       replaced with '.'.  '^' terminates the window name string in
       the output file.  Truncate if necessary.
    */
    char * windowName;  /* malloced */
    unsigned int i;

    windowName = malloc(MAXNAMELEN+1);
    if (!windowName)
        pm_error("Failed to get %u bytes of memory for window name "
                 "buffer", MAXNAMELEN+1);

    for (i = 0; i < MAXNAMELEN && fileName[i]; ++i) {
        const char thisChar = fileName[i];

        if (!isprint(thisChar) || thisChar =='^')
            windowName[i] = '.';
        else
            windowName[i] = thisChar;
    }
    windowName[i] = '\0';

    return windowName;
}



            int
main(int argc, const char ** const argv) {

    FILE * ifP;
    int rows, cols;
    int colorCt;
    pixval maxval;
    colorhist_vector chv;
    char rgb[CLUTCOLORCT];
    pixel ** pixels;
    colorhash_table cht;
    struct CmdlineInfo cmdline;
    const char * windowName;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFilename);

    pixels = ppm_readppm(ifP, &cols, &rows, &maxval);

    validateComputableSize(cols, rows, cmdline.expand);

    pm_close(ifP);

    /* Figure out the colormap. */
    pm_message("Computing colormap..." );
    chv = ppm_computecolorhist(pixels, cols, rows, MAXCOLORCT, &colorCt);
    if (!chv)
        pm_error("Too many colors - try doing a 'pnmquant %u'", MAXCOLORCT);
    pm_message("%u colors found", colorCt );

    makeIcrColormap(chv, colorCt, maxval, rgb);

    /* And make a hash table for fast lookup. */
    cht = ppm_colorhisttocolorhash(chv, colorCt);

    ppm_freecolorhist(chv);

    /************** Create a new window using ICR protocol *********/
    /* Format is "ESC^W;left;top;width;height;display;windowname"  */

    if (cmdline.windowname)
        windowName = pm_strdup(cmdline.windowname);
    else {
        if (streq(cmdline.inputFilename, "-"))
            windowName = pm_strdup("untitled");
        else
            windowName = windowNameFmFileName(cmdline.inputFilename);
    }
    pm_message("Creating window '%s' ...", windowName);

    printf("\033^W;%d;%d;%d;%d;%d;%s^",
           0, 0, cols * cmdline.expand, rows * cmdline.expand,
                 cmdline.display, windowName);
    fflush(stdout);

    /****************** Download the colormap.  ********************/

    downloadColormap(rgb, windowName);

    sendOutPicture(pixels, rows, cols, cht, cmdline.expand, windowName);

    pm_strfree(windowName);

    return 0;
}



