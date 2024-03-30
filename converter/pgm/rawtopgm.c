/* rawtopgm.c - convert raw grayscale bytes into a portable graymap
**
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
#include <math.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pgm.h"

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;
    unsigned int headerskip;
    float rowskip;
    int bottomfirst;  /* the -bottomfirst/-bt option */
    int autosize;  /* User wants us to figure out the size */
    unsigned int width;
    unsigned int height;
    int bpp;
      /* bytes per pixel in input format.  1 or 2 */
    int littleendian;
      /* logical: samples in input are least significant byte first */
    int maxval;  /* -maxval option, or -1 if none */
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

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "bottomfirst",   OPT_FLAG,   &cmdlineP->bottomfirst,
            NULL,   0);
    OPTENT3(0,   "bt",            OPT_FLAG,   &cmdlineP->bottomfirst,
            NULL,   0);
    OPTENT3(0,   "topbottom",     OPT_FLAG,   &cmdlineP->bottomfirst,
            NULL,   0);
    OPTENT3(0,   "tb",            OPT_FLAG,   &cmdlineP->bottomfirst,
            NULL,   0);
    OPTENT3(0,   "headerskip",    OPT_UINT,   &cmdlineP->headerskip,
            NULL,   0);
    OPTENT3(0,   "rowskip",       OPT_FLOAT,  &cmdlineP->rowskip,
            NULL,   0);
    OPTENT3(0,   "bpp",           OPT_INT,    &cmdlineP->bpp,
            NULL,   0);
    OPTENT3(0,   "littleendian",  OPT_FLAG,   &cmdlineP->littleendian,
            NULL,   0);
    OPTENT3(0,   "maxval",        OPT_UINT,   &cmdlineP->maxval,
            NULL,   0);

    /* Set the defaults */
    cmdlineP->bottomfirst = false;
    cmdlineP->headerskip = 0;
    cmdlineP->rowskip = 0.0;
    cmdlineP->bpp = 1;
    cmdlineP->littleendian = 0;
    cmdlineP->maxval = -1;

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We may have parms that are negative numbers */

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 == 0) {
        cmdlineP->inputFileName = "-";
        cmdlineP->autosize = true;
    } else if (argc-1 == 1) {
        cmdlineP->inputFileName = argv[1];
        cmdlineP->autosize = true;
    } else if (argc-1 == 2) {
        cmdlineP->inputFileName = "-";
        cmdlineP->autosize = false;
        cmdlineP->width = pm_parse_width(argv[1]);
        cmdlineP->height = pm_parse_height(argv[2]);
    } else if (argc-1 == 3) {
        cmdlineP->inputFileName = argv[3];
        cmdlineP->autosize = false;
        cmdlineP->width = pm_parse_width(argv[1]);
        cmdlineP->height = pm_parse_height(argv[2]);
    } else
        pm_error("Program takes zero, one, two, or three arguments.  You "
                 "specified %d", argc-1);

    if (cmdlineP->bpp != 1 && cmdlineP->bpp != 2)
        pm_error("Bytes per pixel (-bpp) must be 1 or 2.  You specified %d.",
                 cmdlineP->bpp);

    if (cmdlineP->maxval == 0)
        pm_error("Maxval (-maxval) may not be zero.");

    if (cmdlineP->maxval > 255 && cmdlineP->bpp == 1)
        pm_error("You have specified one byte per pixel, but a maxval "
                 "too large to fit in one byte: %d", cmdlineP->maxval);
    if (cmdlineP->maxval > 65535)
        pm_error("Maxval must be less than 65536.  You specified %d.",
                 cmdlineP->maxval);

    if (cmdlineP->rowskip && cmdlineP->autosize)
        pm_error("If you specify -rowskip, you must also give the image "
                 "dimensions.");
    if (cmdlineP->rowskip && cmdlineP->bottomfirst)
        pm_error("You cannot specify both -rowskip and -bottomfirst.  This is "
                 "a limitation of this program.");

}



static void
computeImageSize(struct CmdlineInfo const cmdline,
                 long               const nRead,
                 unsigned int *     const rowsP,
                 unsigned int *     const colsP) {

    if (cmdline.autosize) {
        int sqrtTrunc =
            (int) sqrt((double) (nRead - cmdline.headerskip));
        if (sqrtTrunc * sqrtTrunc + cmdline.headerskip != nRead)
            pm_error( "You must specify the dimensions of the image unless "
                      "it is a quadratic image.  This one is not quadratic: "
                      "The number of "
                      "pixels in the input is %ld, which is not a perfect "
                      "square.", nRead - cmdline.headerskip);
        *rowsP = *colsP = sqrtTrunc;
        pm_message( "Image size: %u cols, %u rows", *colsP, *rowsP);
    } else {
        *rowsP = cmdline.height;
        *colsP = cmdline.width;
    }
}



static void
skipHeader(FILE *       const ifP,
           unsigned int const headerskip) {

    int i;

    for (i = 0; i < headerskip; ++i) {
        /* Read a byte out of the file */
        int val;
        val = getc(ifP);
        if (val == EOF)
            pm_error("EOF / read error reading Byte %u in the header", i );
    }
}



static gray
readFromFile(FILE *        const ifP,
             unsigned int  const bpp,
             unsigned int  const row,
             unsigned int  const col,
             bool          const littleEndian) {
/*----------------------------------------------------------------------------
   Return the next sample value from the input file *ifP, assuming the
   input stream is 'bpp' bytes per pixel (1 or 2).  In the case of two
   bytes, if 'littleEndian', assume least significant byte is first.
   Otherwise, assume MSB first.

   In error messages, say this is Column 'col', Row 'row'.  Exit program if
   error.
-----------------------------------------------------------------------------*/
    gray retval;

    if (bpp == 1) {
        int val;
        val = getc(ifP);
        if (val == EOF)
            pm_error( "EOF / read error at Row %u Column %u",
                      row, col);
        retval = (gray) val;
    } else {
        short val;
        int rc;
        rc = littleEndian ?
            pm_readlittleshort(ifP, &val) : pm_readbigshort(ifP, &val);
        if (rc != 0)
            pm_error( "EOF / read error at Row %u Column %u",
                      row, col);
        retval = (gray) val;
    }
    return retval;
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    gray * grayrow;
    unsigned int rows, cols;
    gray maxval;
    char * buf;
    /* pixels1 and pixels2 are the array of pixels in the input buffer
       (assuming we are using an input buffer).  pixels1 is the array
       as if the pixels are one byte each.  pixels2 is the array as if
       they are two bytes each.
       */
    unsigned char * pixels1;
    unsigned short * pixels2;
    long nRead;
    unsigned int row;
    float toskip;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    if (cmdline.autosize || cmdline.bottomfirst) {
        buf = pm_read_unknown_size(ifP, &nRead);
        pixels1 = (unsigned char *) buf;
        pixels2 = (unsigned short *) buf;
    } else
        buf = NULL;

    computeImageSize(cmdline, nRead, &rows, &cols);

    if (!buf)
        skipHeader(ifP, cmdline.headerskip);

    toskip = 0.00001;

    if (cmdline.maxval == -1)
        maxval = (cmdline.bpp == 1 ? (gray) 255 : (gray) 65535);
    else
        maxval = cmdline.maxval;

    pgm_writepgminit(stdout, cols, rows, maxval, 0);

    grayrow = pgm_allocrow(cols);

    for (row = 0; row < rows; ++row) {
        unsigned int col;
        unsigned int rowpos; /* index of this row in pixel array */

        if (cmdline.bottomfirst)
            rowpos = (rows-row-1) * cols;
        else
            rowpos = row * cols;

        for (col = 0; col < cols; ++col) {
            if (buf) {
                if (cmdline.bpp == 1)
                    grayrow[col] = pixels1[rowpos+col];
                else
                    grayrow[col] = pixels2[rowpos+col];
            } else {
                grayrow[col] = readFromFile(ifP, cmdline.bpp,
                                            row, col,
                                            cmdline.littleendian > 0);
            }
        }
        for (toskip += cmdline.rowskip; toskip >= 1.0; toskip -= 1.0) {
            /* Note that if we're using a buffer, cmdline.rowskip is zero */
            int val;
            val = getc(ifP);
            if (val == EOF)
                pm_error("EOF / read error skipping bytes at the end "
                         "of Row %u.", row);
        }
        pgm_writepgmrow(stdout, grayrow, cols, maxval, 0);
    }

    if (buf)
        free(buf);
    pm_close(ifP);
    pm_close(stdout);

    return 0;
}



