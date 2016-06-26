/*
 * This is derived from the file of the same name dated June 5, 1995,
 * copied from the Army High Performance Computing Research Center's
 * media-tools.tar.gz package, received from 
 * http://www.arc.umn.edu/gvl-software/media-tools.tar.gz on 2000.04.13.
 *
 * This software is copyrighted as noted below.  It may be freely copied,
 * modified, and redistributed, provided that the copyright notice is
 * preserved on all copies.
 *
 * There is no warranty or other guarantee of fitness for this software,
 * it is provided solely "as is".  Bug reports or fixes may be sent
 * to the author, who may or may not act on them as he desires.
 *
 * You may not include this software in a program or other software product
 * without supplying the source, or without informing the end-user that the
 * source is available for no extra charge.
 *
 * If you modify this software, you should include a notice giving the
 * name of the person performing the modification, the date of modification,
 * and the reason for such modification.
 */
/*
 * rletopnm - A conversion program to convert from Utah's "rle" image format
 *            to pbmplus ppm or pgm image formats.
 *
 * Author:      Wes Barris (wes@msc.edu)
 *              AHPCRC
 *              Minnesota Supercomputer Center, Inc.
 * Date:        March 30, 1994
 * Copyright (c) Minnesota Supercomputer Center 1994
 * 
 * 2000.04.13 adapted for Netpbm by Bryan Henderson.  Quieted compiler 
 *            warnings.  Added --alpha option.  Accept input on stdin
 *
 */

#define _BSD_SOURCE 1      /* Make sure strdup() is in string.h */
#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */

/*-----------------------------------------------------------------------------
 * System includes.
 */
#include <string.h>
#include <stdio.h>
#define NO_DECLARE_MALLOC
#include <rle.h>

#include "pm_c_util.h"
#include "pnm.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "nstring.h"

#define HMSG if (headerDump) pm_message
#define GRAYSCALE   001 /* 8 bits, no colormap */
#define PSEUDOCOLOR 010 /* 8 bits, colormap */
#define TRUECOLOR   011 /* 24 bits, colormap */
#define DIRECTCOLOR 100 /* 24 bits, no colormap */
#define RLE_MAXVAL 255
/*
 * Utah type declarations.
 */
static rle_hdr hdr;
static rle_map * colormap;


struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    char *       inputFilename;
    unsigned int headerdump;
    unsigned int verbose;
    char *       alphaout;
    bool         alphaStdout;
};



/*
 * Other declarations.
 */
static int visual, maplen;
static int width, height;



static void
parseCommandLine(int argc, char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that many of the strings that this function returns in the
   *cmdlineP structure are actually in the supplied argv array.  And
   sometimes, one of these strings is actually just a suffix of an entry
   in argv!
-----------------------------------------------------------------------------*/
    optEntry * option_def;  /* malloc'ed */
    optStruct3 opt;
    unsigned int option_def_index;

    unsigned int alphaoutSpec;

    MALLOCARRAY(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3('h', "headerdump", OPT_FLAG,   
            NULL,                      &cmdlineP->headerdump,     0);
    OPTENT3('v', "verbose",    OPT_FLAG,   
            NULL,                      &cmdlineP->verbose,        0);
    OPTENT3(0,   "alphaout",   OPT_STRING, 
            &cmdlineP->alphaout, &alphaoutSpec,                   0);

    opt.opt_table = option_def;
    opt.short_allowed = TRUE;  /* We have short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and all of *cmdlineP. */

    if (!alphaoutSpec)
        cmdlineP->alphaout = NULL;

    if (argc - 1 == 0)
        cmdlineP->inputFilename = NULL;  /* he wants stdin */
    else if (argc - 1 == 1) {
        if (streq(argv[1], "-"))
            cmdlineP->inputFilename = NULL;  /* he wants stdin */
        else 
            cmdlineP->inputFilename = strdup(argv[1]);
    } else 
        pm_error("Too many arguments.  The only argument accepted "
                 "is the input file specification");

    if (cmdlineP->alphaout && 
        streq(cmdlineP->alphaout, "-"))
        cmdlineP->alphaStdout = TRUE;
    else 
        cmdlineP->alphaStdout = FALSE;
}



static void
reportRleGetSetupError(int const rleGetSetupRc) {
    
    switch (rleGetSetupRc) {
    case -1:
        pm_error("According to the URT library, the input is not "
                 "an RLE file.  rle_get_setup() failed.");
        break;
    case -2:
        pm_error("Unable to get memory for the color map.  "
                 "rle_get_setup() failed.");
        break;
    case -3:
        pm_error("Input file is empty.  rle_get_setup() failed.");
        break;
    case -4:
        pm_error("End of file in the middle of where the RLE header should "
                 "be.  rle_get_setup() failed.");
        break;
    default:
        pm_error("rle_get_setup() failed for an unknown reason");
    }
}



static void 
readRleHeader(FILE * const ifP,
              bool   const headerDump) {

    int rc;
    int  i;
    hdr.rle_file = ifP;
    rc = rle_get_setup(&hdr);
    if (rc != 0)
        reportRleGetSetupError(rc);

    width = hdr.xmax - hdr.xmin + 1;
    height = hdr.ymax - hdr.ymin + 1;
    HMSG("Image size: %dx%d", width, height);
    if (hdr.ncolors == 1 && hdr.ncmap == 3) {
        visual = PSEUDOCOLOR;
        colormap = hdr.cmap;
        maplen = (1 << hdr.cmaplen);
        HMSG("Mapped color image with a map of length %d.",
                maplen);
    }
    else if (hdr.ncolors == 3 && hdr.ncmap == 0) {
        visual = DIRECTCOLOR;
        HMSG("24 bit color image, no colormap.");
    }
    else if (hdr.ncolors == 3 && hdr.ncmap == 3) {
        visual = TRUECOLOR;
        colormap = hdr.cmap;
        maplen = (1 << hdr.cmaplen);
        HMSG(
                "24 bit color image with color map of length %d" ,maplen);
    }
    else if (hdr.ncolors == 1 && hdr.ncmap == 0) {
        visual = GRAYSCALE;
        HMSG("Grayscale image.");
    }
    else {
        fprintf(stderr,
                "ncolors = %d, ncmap = %d, I don't know how to handle this!",
                hdr.ncolors, hdr.ncmap);
        exit(-1);
    }
    if (hdr.alpha == 0) {
        HMSG("No alpha channel.");
    } else if (hdr.alpha == 1) {
        HMSG("Alpha channel exists!");
    } else {
        fprintf(stderr, "alpha = %d, I don't know how to handle this!\n",
                hdr.alpha);
        exit(-1);
    }
    switch (hdr.background) {
    case 0:
        HMSG("Use all pixels, ignore background color.");
        break;
    case 1:
        HMSG("Use only non-background pixels, ignore background color.");
        break;
    case 2:
        HMSG("Use only non-background pixels, "
             "clear to background color (default).");
        break;
    default:
        HMSG("Unknown background flag!");
        break;
    }
    if (hdr.background == 2)
        for (i = 0; i < hdr.ncolors; i++)
            HMSG(" %d", hdr.bg_color[i]);
    if (hdr.ncolors == 1 && hdr.ncmap == 3) {
        HMSG(" (%d %d %d)",
                hdr.cmap[hdr.bg_color[0]]>>8,
                hdr.cmap[hdr.bg_color[0]+256]>>8,
                hdr.cmap[hdr.bg_color[0]+512]>>8);
    }
    else if (hdr.ncolors == 3 && hdr.ncmap == 3) {
        HMSG(" (%d %d %d)",
                hdr.cmap[hdr.bg_color[0]]>>8,
                hdr.cmap[hdr.bg_color[1]+256]>>8,
                hdr.cmap[hdr.bg_color[2]+512]>>8);
    }
    if (hdr.comments)
        for (i = 0; hdr.comments[i] != NULL; i++)
            HMSG("%s", hdr.comments[i]);
}



static void 
writePpmRaster(FILE * const imageoutFileP,
               FILE * const alphaFileP) {

    rle_pixel ***scanlines, **scanline;
    pixval r, g, b;
    pixel *pixelrow;
    gray *alpharow;
   
    int scan;
    int x;
    /*
     *  Allocate some stuff.
     */
    pixelrow = ppm_allocrow(width);
    alpharow = pgm_allocrow(width);

    MALLOCARRAY(scanlines, height);
    RLE_CHECK_ALLOC( hdr.cmd, scanlines, "scanline pointers" );

    for ( scan = 0; scan < height; scan++ )
        RLE_CHECK_ALLOC( hdr.cmd, (rle_row_alloc(&hdr, &scanlines[scan]) >= 0),
                         "pixel memory" );
    /*
     * Loop through those scan lines.
     */
    for (scan = 0; scan < height; ++scan)
        rle_getrow(&hdr, scanlines[height - scan - 1]);
    for (scan = 0; scan < height; ++scan) {
        scanline = scanlines[scan];
        switch (visual) {
        case GRAYSCALE:    /* 8 bits without colormap */
            for (x = 0; x < width; x++) {
                r = scanline[0][x];
                g = scanline[0][x];
                b = scanline[0][x];
                PPM_ASSIGN(pixelrow[x], r, g, b);
                if (hdr.alpha)
                    alpharow[x] = scanline[-1][x];
                else 
                    alpharow[x] = 0;
            }
            break;
        case TRUECOLOR:    /* 24 bits with colormap */
            for (x = 0; x < width; x++) {
                r = colormap[scanline[0][x]]>>8;
                g = colormap[scanline[1][x]+256]>>8;
                b = colormap[scanline[2][x]+512]>>8;
                PPM_ASSIGN(pixelrow[x], r, g, b);
                if (hdr.alpha) 
                    alpharow[x] = colormap[scanline[-1][x]];
                else
                    alpharow[x] = 0;
            }
            break;
        case DIRECTCOLOR:  /* 24 bits without colormap */
            for (x = 0; x < width; x++) {
                r = scanline[0][x];
                g = scanline[1][x];
                b = scanline[2][x];
                PPM_ASSIGN(pixelrow[x], r, g, b);
                if (hdr.alpha)
                    alpharow[x] = scanline[-1][x];
                else
                    alpharow[x] = 0;
            }
            break;
        case PSEUDOCOLOR:  /* 8 bits with colormap */
            for (x = 0; x < width; x++) {
                r = colormap[scanline[0][x]]>>8;
                g = colormap[scanline[0][x]+256]>>8;
                b = colormap[scanline[0][x]+512]>>8;
                PPM_ASSIGN(pixelrow[x], r, g, b);
                if (hdr.alpha) 
                    alpharow[x] = colormap[scanline[-1][x]];
                else
                    alpharow[x] = 0;
            }
            break;
        default:
            break;
        }
        /*
         * Write the scan line.
         */
        if (imageoutFileP) 
            ppm_writeppmrow(imageoutFileP, pixelrow, width, RLE_MAXVAL, 0);
        if (alphaFileP)
            pgm_writepgmrow(alphaFileP, alpharow, width, RLE_MAXVAL, 0);

    } /* end of for scan = 0 to height */

    /* Free scanline memory. */
    for (scan = 0; scan < height; ++scan)
        rle_row_free(&hdr, scanlines[scan]);
    free (scanlines);
    ppm_freerow(pixelrow);
    pgm_freerow(alpharow);
}



static void 
writePgmRaster(FILE * const imageoutFileP,
               FILE * const alphaFileP) {
/*----------------------------------------------------------------------------
   Write the PGM image data
-----------------------------------------------------------------------------*/
    rle_pixel ***scanlines, **scanline;
    gray * pixelrow;
    gray * alpharow;
    int scan;
    /*
     *  Allocate some stuff.
     */
    pixelrow = pgm_allocrow(width);
    alpharow = pgm_allocrow(width);

    MALLOCARRAY(scanlines, height);
    RLE_CHECK_ALLOC( hdr.cmd, scanlines, "scanline pointers" );

    for (scan = 0; scan < height; ++scan)
        RLE_CHECK_ALLOC(hdr.cmd, (rle_row_alloc(&hdr, &scanlines[scan]) >= 0),
                        "pixel memory" );
    /*
     * Loop through those scan lines.
     */
    for (scan = 0; scan < height; ++scan)
        rle_getrow(&hdr, scanlines[height - scan - 1]);

    for (scan = 0; scan < height; ++scan) {
        int x;
        scanline = scanlines[scan];
        for (x = 0; x < width; ++x) {
            pixelrow[x] = scanline[0][x];
            if (hdr.alpha) 
                alpharow[x] = scanline[1][x];
            else
                alpharow[x] = 0;
        }
        if (imageoutFileP) 
            pgm_writepgmrow(imageoutFileP, pixelrow, width, RLE_MAXVAL, 0);
        if (alphaFileP)
            pgm_writepgmrow(alphaFileP, alpharow, width, RLE_MAXVAL, 0);

        }   /* end of for scan = 0 to height */

    /* Free scanline memory. */
    for (scan = 0; scan < height; ++scan)
        rle_row_free(&hdr, scanlines[scan]);
    free(scanlines);
    pgm_freerow(pixelrow);
    pgm_freerow(alpharow);
}



int
main(int argc, char ** argv) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    FILE * imageoutFileP;
    FILE * alphaFileP;
    char * fname;

    pnm_init( &argc, argv );

    parseCommandLine(argc, argv, &cmdline);

    fname = NULL;  /* initial value */

    if (cmdline.inputFilename != NULL ) 
        ifP = pm_openr(cmdline.inputFilename);
    else
        ifP = stdin;

    if (cmdline.alphaStdout)
        alphaFileP = stdout;
    else if (cmdline.alphaout == NULL) 
        alphaFileP = NULL;
    else {
        alphaFileP = pm_openw(cmdline.alphaout);
    }

    if (cmdline.alphaStdout) 
        imageoutFileP = NULL;
    else
        imageoutFileP = stdout;


    /*
     * Open the file.
     */
    /* Initialize header. */
    hdr = *rle_hdr_init(NULL);
    rle_names(&hdr, cmd_name( argv ), fname, 0);

    /*
     * Read the rle file header.
     */
    readRleHeader(ifP, cmdline.headerdump || cmdline.verbose);
    if (cmdline.headerdump)
        exit(0);

    /* 
     * Write the alpha file header
     */
    if (alphaFileP)
        pgm_writepgminit(alphaFileP, width, height, RLE_MAXVAL, 0);

    /*
     * Write the pnm file header.
     */
    switch (visual) {
    case GRAYSCALE:   /* 8 bits without colormap -> pgm */
        if (cmdline.verbose)
            pm_message("Writing pgm file.");
        if (imageoutFileP)
            pgm_writepgminit(imageoutFileP, width, height, RLE_MAXVAL, 0);
        writePgmRaster(imageoutFileP, alphaFileP);
        break;
    default:      /* anything else -> ppm */
        if (cmdline.verbose)
            pm_message("Writing ppm file.");
        if (imageoutFileP)
            ppm_writeppminit(imageoutFileP, width, height, RLE_MAXVAL, 0);
        writePpmRaster(imageoutFileP, alphaFileP);
        break;
    }
   
    pm_close(ifP);
    if (imageoutFileP) 
        pm_close(imageoutFileP);
    if (alphaFileP)
        pm_close(alphaFileP);

    return 0;
}
