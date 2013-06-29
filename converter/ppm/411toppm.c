/*
   I recently had a visit from my mom who owns a Sony Mavica camera.
   This camera produces standard MPEG and JPEG files, but it also
   creates 64x48 pixel thumbnails for preview/index on its own tiny
   LCD screen.  These files are named with an extension that is
   ".411".

   Sony appears not to want to document the ".411" file format, but it
   is clear from various web pages that it is a variant of the
   CCIR.601 standard YUV encoding used in MPEG.  The name indicates
   that the file content consists of chunks of 6 bytes: 4 bytes of
   image Y values, followed by 1 bytes of U and one byte of V values
   that apply to the previous 4 Y pixel values.

   There appear to be some commercial 411 file readers on the net, and
   there is the Java-based Javica program, but I prefer Open Source
   command-line utilities.  So, I grabbed a copy of netpbm-9.11 from
   SourceForge and hacked the eyuvtoppm.c file so that it looks like
   this.  While this may not be exactly the right thing to do, it
   produces results which are close enough for me.

   There are all sorts of user-interface gotchas possible in here that
   I'm not going to bother changing -- especially not without actual
   documentation from Sony about what they intend to do with ".411"
   files in the future.  I place my modifications into the public
   domain, but I ask that my name & e-mail be mentioned in the
   commentary of any derived version.

   Steve Allen <sla@alumni.caltech.edu>, 2001-03-01

   Bryan Henderson reworked the program to use the Netpbm libraries to
   create the PPM output and follow some other Netpbm conventions.
   2001-03-03.  Bryan's contribution is public domain.
*/
/*
 * Copyright (c) 1995 The Regents of the University of California.
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement is
 * hereby granted, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF
 * CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.  */

/*==============*
 * HEADER FILES *
 *==============*/
#include <stdio.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "ppm.h"

typedef unsigned char uint8;

#define CHOP(x)     ((x < 0) ? 0 : ((x > 255) ? 255 : x))

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;
    int width;
    int height;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo *cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/

    optEntry * option_def;
        /* Instructions to OptParseOptions2 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "width",      OPT_INT,    &cmdlineP->width,  NULL,   0);
    OPTENT3(0,   "height",     OPT_INT,    &cmdlineP->height, NULL,   0);

    /* Set the defaults */
    cmdlineP->width = 64;
    cmdlineP->height = 48;

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */
    
    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (cmdlineP->width <= 0)
        pm_error("-width must be positive.");
    if (cmdlineP->width %4 != 0)
        pm_error("-width must be a multiple of 4.");
    if (cmdlineP->height <= 0)
        pm_error("-height must be positive.");

    if (argc > 2)
        pm_error("There is at most 1 argument: the input file spec.  "
                 "You supplied %d", argc-1);
    else {
        if (argc > 1)
            cmdlineP->inputFileName = argv[1];
        else
            cmdlineP->inputFileName = "-";
    }
    free(option_def);
}



static void
ReadYUV(FILE  * const ifP,
        uint8 * const inbuff) {

    size_t bytesRead;

    bytesRead = fread(inbuff, 1, 6, ifP);

    if (bytesRead != 6 ) {
        if (feof(ifP))
            pm_error("Premature end of input.");
        else
            pm_error("Error reading input.");
     }
}



static void
YUVtoPPM(FILE  * const ifP,
         int     const width,
         int     const height,
         pixel * const pixrow ) {

    unsigned int col;

    for (col = 0; col < width; ++col) {

        uint8 inbuff[6];

        uint8 * const origY  = &inbuff[0];
        uint8 * const origCb = &inbuff[4];
        uint8 * const origCr = &inbuff[5];
        int   y, u, v;
        int32_t tempR, tempG, tempB;
        pixval r, g, b;

        if (col % 4 == 0) {
            ReadYUV(ifP, inbuff);
            u = origCb[0] - 128;
            v = origCr[0] - 128;
        }

        y = origY[col % 4] - 16;

        tempR = 104635 * v              + y * 76310;
        tempG = -25690 * u + -53294 * v + y * 76310;
        tempB = 132278 * u              + y * 76310;

        r = CHOP((int)(tempR >> 16));
        g = CHOP((int)(tempG >> 16));
        b = CHOP((int)(tempB >> 16));
        
        PPM_ASSIGN(pixrow[col], r, g, b);
    }
}



int
main(int argc, const char **argv) {

    pixval const maxval = 255;
    struct CmdlineInfo cmdline;
    FILE  * ifP;
    pixel * pixrow;
    unsigned int row;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    pixrow = ppm_allocrow(cmdline.width);

    pm_message("Reading (%ux%u): '%s'", cmdline.width, cmdline.height,
               cmdline.inputFileName);

    ifP = pm_openr(cmdline.inputFileName);

    ppm_writeppminit(stdout, cmdline.width, cmdline.height, maxval, 0);

    for (row = 0; row < cmdline.height; ++row) {
        YUVtoPPM(ifP, cmdline.width, cmdline.height, pixrow);
        ppm_writeppmrow(stdout, pixrow, cmdline.width, maxval, 0);
    }

    if (fgetc(ifP) != EOF)
        pm_message("Extraneous data at end of image.");

    pm_close(ifP);
    ppm_freerow(pixrow);

    return 0;
}

/*
   By default a .411 file is width=64, height=48, 4608 bytes.
   There is no header.
*/
