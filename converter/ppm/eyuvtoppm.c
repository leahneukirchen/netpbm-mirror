/* Bryan got this from mm.ftp-cs.berkeley.edu from the package
   mpeg-encode-1.5b-src under the name eyuvtoppm.c on March 30, 2000.  
   The file was dated April 14, 1995.  

   Bryan rewrote the program entirely to match Netpbm coding style,
   use the Netpbm libraries and also to output to stdout and ignore
   any specification of an output file on the command line and not
   segfault when called with no arguments.

   There was no attached documentation except for this:  Encoder/Berkeley
   YUV format is merely the concatenation of Y, U, and V data in order.
   Compare with Abekda YUV, which interlaces Y, U, and V data.  */

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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pm_c_util.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "ppm.h"

typedef unsigned char uint8;

#define CHOP(x)     ((x < 0) ? 0 : ((x > 255) ? 255 : x))



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* Name of input file */
    unsigned int width;
    unsigned int height;
};



static void
parseCommandLine(int argc, char ** argv,
                 struct CmdlineInfo * const cmdlineP) {

    optStruct3 opt;   /* Set by OPTENT3 */
    unsigned int option_def_index;
    optEntry * option_def;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3('w', "width",     OPT_UINT,  &cmdlineP->width,   NULL,         0);
    OPTENT3('h', "height",    OPT_UINT,  &cmdlineP->height,  NULL,         0);
    
    /* DEFAULTS */
    cmdlineP->width = 352;
    cmdlineP->height = 240;

    opt.opt_table = option_def;
    opt.short_allowed = TRUE;
    opt.allowNegNum = FALSE;

    pm_optParseOptions3(&argc, argv, opt, sizeof(opt), 0);

    if (cmdlineP->width == 0)
        pm_error("The width cannot be zero.");
    if (cmdlineP->width % 2 != 0)
        pm_error("The width of an eyuv image must be an even number.  "
                 "You specified %u.", cmdlineP->width);
    if (cmdlineP->height == 0)
        pm_error("The height cannot be zero.");
    if (cmdlineP->height % 2 != 0)
        pm_error("The height of an eyuv image must be an even number.  "
                 "You specified %u.", cmdlineP->height);


    if (argc-1 == 0) 
        cmdlineP->inputFileName = "-";
    else if (argc-1 != 1)
        pm_error("Program takes zero or one argument (filename).  You "
                 "specified %u", argc-1);
    else
        cmdlineP->inputFileName = argv[1];

    free(option_def);
}



static uint8 ** 
allocUint8Array(unsigned int const cols,
                unsigned int const rows) {

    uint8 ** retval;
    unsigned int row;

    MALLOCARRAY(retval, rows);
    if (retval == NULL)
        pm_error("Unable to allocate storage for %u x %u byte array.",
                 cols, rows);

    for (row = 0; row < rows; ++row) {
        MALLOCARRAY(retval[row], cols);
        if (retval[row] == NULL)
            pm_error("Unable to allocate storage for %u x %u byte array.",
                     cols, rows);
    }
    return retval;
}



static void 
freeUint8Array(uint8 **     const array,
               unsigned int const rows) {

    unsigned int row;

    for (row = 0; row < rows; ++row)
        free(array[row]);

    free(array);
}



static void
allocateStorage(unsigned int const cols,
                unsigned int const rows,
                uint8 ***    const orig_yP,
                uint8 ***    const orig_cbP,
                uint8 ***    const orig_crP) {

    *orig_yP  = allocUint8Array(cols, rows);
    *orig_cbP = allocUint8Array(cols, rows);
    *orig_crP = allocUint8Array(cols, rows);
}



static void
freeStorage(unsigned int const rows,
            uint8 **     const orig_y,
            uint8 **     const orig_cb,
            uint8 **     const orig_cr) {
    
    freeUint8Array(orig_y,  rows); 
    freeUint8Array(orig_cb, rows); 
    freeUint8Array(orig_cr, rows);

}



static void 
YUVtoPPM(FILE *       const ofP,
         unsigned int const cols,
         unsigned int const rows,
         uint8 **     const orig_y,
         uint8 **     const orig_cb,
         uint8 **     const orig_cr) { 
/*----------------------------------------------------------------------------
   Convert the YUV image in arrays orig_y[][], orig_cb[][], and orig_cr[][]
   to a PPM image and write it to file *ofP.
-----------------------------------------------------------------------------*/
    pixel * const pixrow = ppm_allocrow(cols);
    
    unsigned int row;

    ppm_writeppminit(ofP, cols, rows, 255, FALSE);

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        for (col = 0; col < cols; ++col) {
            int const y =  orig_y[row][col] - 16;
            int const u =  orig_cb[row/2][col/2] - 128;
            int const v =  orig_cr[row/2][col/2] - 128;
            long   tempR, tempG, tempB;
            int    r, g, b;
            /* look at yuvtoppm source for explanation */

            tempR = 104635*v + 76310*y;
            tempG = -25690*u + -53294*v + 76310*y;
            tempB = 132278*u + 76310*y;
            
            r = CHOP((int)(tempR >> 16));
            g = CHOP((int)(tempG >> 16));
            b = CHOP((int)(tempB >> 16));
            
            PPM_ASSIGN(pixrow[col], r, g, b);
        }
        ppm_writeppmrow(stdout, pixrow, cols, 255, FALSE);
    }
    ppm_freerow(pixrow);
}



static void 
ReadYUV(FILE *       const ifP,
        unsigned int const cols,
        unsigned int const rows,
        uint8 **     const orig_y, 
        uint8 **     const orig_cb, 
        uint8 **     const orig_cr,
        bool *       const eofP) {

    unsigned int row;
    unsigned int totalRead;
    bool eof;

    eof = false;  /* initial value */
    totalRead = 0;  /* initial value */

    for (row = 0; row < rows && !eof; ++row) {        /* Y */
        size_t bytesRead;

        bytesRead = fread(orig_y[row], 1, cols, ifP);
        totalRead += bytesRead;
        if (bytesRead != cols)
            eof = true;
    }
        
    for (row = 0; row < rows / 2 && !eof; ++row) {  /* U */
        size_t bytesRead;

        bytesRead = fread(orig_cb[row], 1, cols / 2, ifP);
        totalRead += bytesRead;
        if (bytesRead != cols / 2)
            eof = true;
    }
        
    for (row = 0; row < rows / 2 && !eof; ++row) { /* V */
        size_t bytesRead;

        bytesRead = fread(orig_cr[row], 1, cols / 2, ifP);
        totalRead += bytesRead;
        if (bytesRead != cols / 2)
            eof = true;
    }

    if (eof) {
        if (totalRead == 0)
            *eofP = TRUE;
        else
            pm_error("Premature end of file reading EYUV input file");
    } else
        *eofP = FALSE;
}



int
main(int argc, const char **argv) {

    FILE * ifP;
    struct CmdlineInfo cmdline;
    unsigned int frameSeq;

    /* The following are addresses of malloc'ed storage areas for use by
       subroutines.
    */
    uint8 ** orig_y;
    uint8 ** orig_cb;
    uint8 ** orig_cr;
    bool eof;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, (char **)argv, &cmdline);

    /* Allocate all the storage at once, to save time. */
    allocateStorage(cmdline.width, cmdline.height,
                    &orig_y, &orig_cb, &orig_cr);

    ifP = pm_openr(cmdline.inputFileName);

    for (frameSeq = 0, eof = false; !eof; ++frameSeq) {

        ReadYUV(ifP, cmdline.width, cmdline.height, 
                orig_y, orig_cb, orig_cr, &eof);

        if (!eof) {
            pm_message("Converting Frame %u", frameSeq);

            YUVtoPPM(stdout, cmdline.width, cmdline.height,
                     orig_y, orig_cb, orig_cr);
        } else if (frameSeq == 0)
            pm_error("Empty EYUV input file");
    }

    freeStorage(cmdline.height, orig_y, orig_cb, orig_cr);

    pm_close(ifP);

    return 0;
}


