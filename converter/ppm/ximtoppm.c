/* ximtoppm.c - read an Xim file and produce a portable pixmap
**
** Copyright (C) 1991 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#define _DEFAULT_SOURCE 1  /* New name for SVID & BSD source defines */
#define _BSD_SOURCE 1      /* Make sure strdup() is in string.h */
#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */

#include <string.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "ppm.h"
#include "xim.h"
#include "shhopt.h"
#include "nstring.h"



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFilename;
    const char * alphaFilename;
    bool         alphaStdout;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo *cmdlineP) {
/*----------------------------------------------------------------------------
   Note that many of the strings that this function returns in the
   *cmdlineP structure are actually in the supplied argv array.  And
   sometimes, one of these strings is actually just a suffix of an entry
   in argv!
-----------------------------------------------------------------------------*/
    optEntry option_def[100];
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int alphaoutSpec;

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "alphaout",   OPT_STRING,
            &cmdlineP->alphaFilename, &alphaoutSpec, 0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and all of *cmdlineP. */

    if (!alphaoutSpec)
        cmdlineP->alphaFilename = NULL;

    if (argc - 1 == 0)
        cmdlineP->inputFilename = "-";  /* he wants stdin */
    else if (argc - 1 == 1)
        cmdlineP->inputFilename = pm_strdup(argv[1]);
    else
        pm_error("Too many arguments.  The only argument accepted "
                 "is the input file specification");

    if (cmdlineP->alphaFilename &&
        streq(cmdlineP->alphaFilename, "-"))
        cmdlineP->alphaStdout = true;
    else
        cmdlineP->alphaStdout = false;
}



/* The subroutines are excerpted and slightly modified from the
   X.V11R4 version of xim_io.c.
*/

static int
readXimHeader(FILE *     const ifP,
              XimImage * const headerP) {

    unsigned int  i;
    XimAsciiHeader  aHead;

    {
        unsigned char * cp;
        cp = (unsigned char *)headerP;
        for (i = 0; i < sizeof(XimImage); ++i)
            *cp++ = 0;
    }
    /* Read header and verify image file formats */
    if (fread((char *)&aHead, sizeof(ImageHeader), 1, ifP) != 1) {
        pm_message("ReadXimHeader: unable to read file header" );
        return 0;
    }
    /* Force broken ASCIIZ strings to at least be valid ASCIIZ */
    aHead.author [sizeof(aHead.author)  - 1] = '\0';
    aHead.date   [sizeof(aHead.date)    - 1] = '\0';
    aHead.program[sizeof(aHead.program) - 1] = '\0';

    if (atoi(aHead.header_size) != sizeof(ImageHeader)) {
        pm_message("ReadXimHeader: header size mismatch" );
        return(0);
    }
    if (atoi(aHead.file_version) != IMAGE_VERSION) {
        pm_message("ReadXimHeader: incorrect Image_file version" );
        return(0);
    }
    headerP->width          = atoi(aHead.image_width);
    headerP->height         = atoi(aHead.image_height);
    headerP->ncolors        = atoi(aHead.num_colors);
    headerP->nchannels      = atoi(aHead.num_channels);
    headerP->bytes_per_line = atoi(aHead.bytes_per_line);
#if 0
    headerP->npics          = atoi(aHead.num_pictures);
#endif
    headerP->bits_channel   = atoi(aHead.bits_per_channel);
    headerP->alpha_flag     = atoi(aHead.alpha_channel);
    headerP->author         = pm_strdup(aHead.author);
    headerP->date           = pm_strdup(aHead.date);
    headerP->program        = pm_strdup(aHead.program);

    /* Do double checking for backwards compatibility */
    if (headerP->npics == 0)
        headerP->npics = 1;
    if (headerP->bits_channel == 0)
        headerP->bits_channel = 8;
    else if (headerP->bits_channel == 24) {
        headerP->nchannels = 3;
        headerP->bits_channel = 8;
    }
    if (headerP->bytes_per_line == 0)
        headerP->bytes_per_line =
            (headerP->bits_channel == 1 && headerP->nchannels == 1) ?
                (headerP->width + 7) / 8 :
                headerP->width;
    headerP->datasize =
        (unsigned int)headerP->bytes_per_line * headerP->height;
    if (headerP->nchannels == 3 && headerP->bits_channel == 8)
        headerP->ncolors = 0;
    else if (headerP->nchannels == 1 && headerP->bits_channel == 8) {
        unsigned int i;

        MALLOCARRAY_NOFAIL(headerP->colors, headerP->ncolors);

        for (i=0; i < headerP->ncolors; ++i) {
            headerP->colors[i].red = aHead.c_map[i][0];
            headerP->colors[i].grn = aHead.c_map[i][1];
            headerP->colors[i].blu = aHead.c_map[i][2];
        }
    }
    return 1;
}



static int
readImageChannel(FILE *         const ifP,
                 byte *         const buf,
                 unsigned int * const bufsizeP,
                 bool           const encoded) {

    unsigned int j;
    long  marker;

    if (!encoded)
        j = fread((char *)buf, 1, (int)*bufsizeP, ifP);
    else {
        byte * line;

        MALLOCARRAY(line, BUFSIZ);
        if (!line) {
            pm_message("ReadImageChannel: can't malloc() fread string" );
            return 0;
        } else {
            /* Unrunlength encode data */
            unsigned int byteCt;

            marker = ftell(ifP);
            j = 0;
            while (((byteCt = fread((char *)line, 1, BUFSIZ, ifP)) > 0) &&
                   (j < *bufsizeP)) {
                unsigned int i;
                for (i=0; (i < byteCt) && (j < *bufsizeP); ++i) {
                    unsigned int runlen;
                    runlen = (unsigned int)line[i] + 1;
                    ++i;
                    while (runlen--)
                        buf[j++] = line[i];
                }
                marker += i;
            }
            /* return to the beginning of the next image's buffer */
            if (fseek(ifP, marker, 0) == -1) {
                pm_message("ReadImageChannel: can't fseek to location "
                           "in image buffer");
                return 0;
            }
            free(line);
        }
    }
    if (j != *bufsizeP) {
        pm_message("unable to complete channel: %u / %u (%f%%)",
                   j, *bufsizeP, j * 100.0 / *bufsizeP);
        *bufsizeP = j;
    }
    return 1;
}



static int
readXimImage(FILE *     const ifP,
             XimImage * const ximP) {

    if (ximP->data) {
        free(ximP->data);
        ximP->data = NULL;
    }
    if (ximP->grn_data) {
        free(ximP->grn_data);
        ximP->grn_data = NULL;
    }
    if (ximP->blu_data) {
        free(ximP->blu_data);
        ximP->blu_data = NULL;
    }
    if (ximP->other) {
        free(ximP->other);
        ximP->other = NULL;
    }
    ximP->npics = 0;
    MALLOCARRAY(ximP->data, ximP->datasize);
    if (!ximP->data) {
        pm_message("ReadXimImage: can't malloc pixmap data");
        return 0;
    }
    if (!readImageChannel(ifP, ximP->data, &ximP->datasize, false)) {
        pm_message("ReadXimImage: end of the images");
        return 0;
    }
    if (ximP->nchannels == 3) {
        MALLOCARRAY(ximP->grn_data, ximP->datasize);
        MALLOCARRAY(ximP->blu_data, ximP->datasize);
        if (!ximP->grn_data || !ximP->blu_data) {
            pm_message("ReadXimImage: can't malloc rgb channel data");
            free(ximP->data);
            if (ximP->grn_data)
                free(ximP->grn_data);
            if (ximP->blu_data)
                free(ximP->blu_data);
            ximP->data = ximP->grn_data = ximP->blu_data = NULL;
            return 0;
        }
        if (!readImageChannel(ifP, ximP->grn_data, &ximP->datasize, false))
            return 0;
        if (!readImageChannel(ifP, ximP->blu_data, &ximP->datasize, false))
            return 0;
    } else if (ximP->nchannels > 3) {
        /* In theory, this can be any fourth channel, but the only one we know
           about is an Alpha channel, so we'll call it that, even though we
           process it generically.
        */
        MALLOCARRAY(ximP->other, ximP->datasize);
        if (!ximP->other) {
            pm_message("ReadXimImage: can't malloc alpha data");
            return 0;
        }
        if (!readImageChannel(ifP, ximP->other, &ximP->datasize, false))
            return(0);
    }
    ximP->npics = 1;

    return 1;
}



/***********************************************************************
*  File:   xlib.c
*  Author: Philip Thompson
*  $Date: 89/11/01 10:14:23 $
*  $Revision: 1.14 $
*  Purpose: General xim library of utililities
*  Copyright (c) 1988  Philip R. Thompson
*                Computer Resource Laboratory (CRL)
*                Dept. of Architecture and Planning
*                M.I.T., Rm 9-526
*                Cambridge, MA  02139
*   This  software and its documentation may be used, copied, modified,
*   and distributed for any purpose without fee, provided:
*       --  The above copyright notice appears in all copies.
*       --  This disclaimer appears in all source code copies.
*       --  The names of M.I.T. and the CRL are not used in advertising
*           or publicity pertaining to distribution of the software
*           without prior specific written permission from me or CRL.
*   I provide this software freely as a public service.  It is NOT a
*   commercial product, and therefore is not subject to an an implied
*   warranty of merchantability or fitness for a particular purpose.  I
*   provide it as is, without warranty.
*   This software is furnished  only on the basis that any party who
*   receives it indemnifies and holds harmless the parties who furnish
*   it against any claims, demands, or liabilities connected with using
*   it, furnishing it to others, or providing it to a third party.
*
*   Philip R. Thompson (phils@athena.mit.edu)
***********************************************************************/

static int
readXim(FILE *     const ifP,
        XimImage * const ximP) {

    int retval;

    if (!readXimHeader(ifP, ximP)) {
        pm_message("can't read xim header");
        retval = 0;
    } else if (!readXimImage(ifP, ximP)) {
        pm_message("can't read xim data");
        retval = 0;
    } else
        retval = 1;

    return retval;
}



int
main(int          argc,
     const char **argv) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    FILE * imageoutFileP;
    FILE * alphaFileP;
    XimImage xim;
    pixel * pixelrow;
    pixel colormap[256];
    gray * alpharow;
        /* The alpha channel of the row we're currently converting, in PGM fmt
        */
    unsigned int rows, cols;
    unsigned int row;
    bool mapped;
    pixval maxval;
    bool succeeded;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFilename);

    if (cmdline.alphaStdout)
        alphaFileP = stdout;
    else if (cmdline.alphaFilename == NULL)
        alphaFileP = NULL;
    else
        alphaFileP = pm_openw(cmdline.alphaFilename);

    if (cmdline.alphaStdout)
        imageoutFileP = NULL;
    else
        imageoutFileP = stdout;

    succeeded = readXim(ifP, &xim);
    if (!succeeded)
        pm_error("can't read Xim file");

    rows = xim.height;
    cols = xim.width;

    if (xim.nchannels == 1 && xim.bits_channel == 8) {
        unsigned int i;

        mapped = true;
        maxval = 255;
        for (i = 0; i < xim.ncolors; ++i) {
            PPM_ASSIGN(
                colormap[i], xim.colors[i].red,
                xim.colors[i].grn, xim.colors[i].blu );
            /* Should be colormap[xim.colors[i].pixel], but Xim is broken. */
        }
    } else if (xim.nchannels == 3 || xim.nchannels == 4) {
        mapped = false;
        maxval = pm_bitstomaxval(xim.bits_channel);
    } else
        pm_error(
            "unknown Xim file type, nchannels == %d, bits_channel == %d",
            xim.nchannels, xim.bits_channel);

    if (imageoutFileP)
        ppm_writeppminit(imageoutFileP, cols, rows, maxval, 0);
    if (alphaFileP)
        pgm_writepgminit(alphaFileP, cols, rows, maxval, 0);

    pixelrow = ppm_allocrow(cols);
    alpharow = pgm_allocrow(cols);

    for (row = 0; row < rows; ++row) {
        if (mapped) {
            byte * const ximrow = &xim.data[row * xim.bytes_per_line];

            unsigned int col;

            for (col = 0; col < cols; ++col)
                pixelrow[col] = colormap[ximrow[col]];

            alpharow[col] = 0;
        } else {
            byte * const redrow = &xim.data     [row * xim.bytes_per_line];
            byte * const grnrow = &xim.grn_data [row * xim.bytes_per_line];
            byte * const blurow = &xim.blu_data [row * xim.bytes_per_line];
            byte * const othrow = &xim.other    [row * xim.bytes_per_line];

            unsigned int col;

            for (col = 0; col < cols; ++col) {
                PPM_ASSIGN(pixelrow[col],
                           redrow[col], grnrow[col], blurow[col]);
                if (xim.nchannels > 3)
                    alpharow[col] = othrow[col];
                else
                    alpharow[col] = 0;
            }
        }
        if (imageoutFileP)
            ppm_writeppmrow(imageoutFileP, pixelrow, cols, maxval, 0);
        if (alphaFileP)
            pgm_writepgmrow(alphaFileP, alpharow, cols, maxval, 0);
    }
    pm_close(ifP);
    if (imageoutFileP)
        pm_close(imageoutFileP);
    if (alphaFileP)
        pm_close(alphaFileP);

    return 0;
}



