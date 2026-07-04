/*
    pnmtojbig - PNM to JBIG converter

    This program was derived from pbmtojbg.c in Markus Kuhn's
    JBIG-KIT package by Bryan Henderson on 2000.05.11

    The main difference is that this version uses the Netpbm libraries.

 */

/*
     The JBIG standard doesn't say which end of the scale is white and
     which end is black in a BIE.  It has a recommendation in terms of
     foreground and background (a concept which does not exist in the
     Netpbm formats) for single-plane images, and is silent for
     multi-plane images.

     Kuhn's implementation of the JBIG standard says if the BIE has a
     single plane, then in that plane a zero bit means white and a one
     bit means black.  But if it has multiple planes, a composite zero
     value means black and a composite maximal value means white.

     Actually, Kuhn's pbmtojbg doesn't even implement this, but rather
     bases the distinction on whether the input file was PBM or PGM.
     This means that if you convert a PGM file with maxval 1 to a JBIG
     file and then back, the result (which is a PBM file) is the
     inverse of what you started with.  Same if the PGM file has
     maxval > 1 but you use a -t option to write only one plane.  We
     assume this is just a bug in pbmtojpg and that hardly anybody does
     this.  So we adopt the implementation described above.

     This means that after jbg_split_planes() hands us a set of bitmap
     planes, if there is only one of them, we have to invert all the
     bits in it.

*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include <jbig.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pnm.h"

static unsigned long total_length = 0;
  /* used for determining output file length */

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFilename;  /* Filename of input file, or "-" */
    const char * outputFilename;  /* filename of output file, or "-" */
    unsigned int singlelayer;
    unsigned int width;
    unsigned int height;
    unsigned int lowestlayerSpec;
    unsigned int lowestlayer;
    unsigned int highestlayerSpec;
    unsigned int highestlayer;
    unsigned int binary;
    unsigned int differentialSpec;
    unsigned int differential;
    unsigned int stripesSpec;
    unsigned int stripes;
    unsigned int maxoffsetSpec;
    unsigned int maxoffset;
    unsigned int planesSpec;
    unsigned int planes;
    unsigned int orderSpec;
    unsigned int order;
    unsigned int algorithmSpec;
    unsigned int algorithm;
    unsigned int annexc;
    unsigned int verbose;
};




static void
parseCommandLine(int argc,
                 const char ** argv,
                 struct CmdlineInfo  * const cmdlineP) {
/* --------------------------------------------------------------------------
   Parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
--------------------------------------------------------------------------*/
    optEntry *option_def;
    /* Instructions to pm_optParseOptions3 on how to parse our options. */
    optStruct3 opt;

    unsigned int option_def_index;
    unsigned int widthSpec, heightSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3('q', "singlelayer",  OPT_FLAG,  NULL,
            &cmdlineP->singlelayer,              0);
    OPTENT3('x', "width",        OPT_UINT,  &cmdlineP->width,
            &widthSpec,                          0);
    OPTENT3('y', "height",       OPT_UINT,  &cmdlineP->height,
            &heightSpec,                         0);
    OPTENT3('l', "lowestlayer",  OPT_UINT,  &cmdlineP->lowestlayer,
            &cmdlineP->lowestlayerSpec,          0);
    OPTENT3('h', "highestlayer", OPT_UINT, &cmdlineP->highestlayer,
            &cmdlineP->highestlayerSpec,         0);
    OPTENT3('b', "binary",       OPT_FLAG,    NULL,
            &cmdlineP->binary,                   0);
    OPTENT3('d', "differential", OPT_UINT, &cmdlineP->differential,
            &cmdlineP->differentialSpec,         0);
    OPTENT3('s', "stripes",      OPT_UINT, &cmdlineP->stripes,
            &cmdlineP->stripesSpec,              0);
    OPTENT3('m', "maxoffset",    OPT_UINT, &cmdlineP->maxoffset,
            &cmdlineP->maxoffsetSpec,            0);
    OPTENT3('t', "planes",       OPT_UINT, &cmdlineP->planes,
            &cmdlineP->planesSpec,               0);
    OPTENT3('o', "order",        OPT_UINT, &cmdlineP->order,
            &cmdlineP->orderSpec,                  0);
    OPTENT3('p', "algorithm",    OPT_UINT, &cmdlineP->algorithm,
            &cmdlineP->algorithmSpec,            0);
    OPTENT3('c', "annexc",       OPT_FLAG, NULL,
            &cmdlineP->annexc,                   0);
    OPTENT3('v', "verbose",      OPT_FLAG, NULL,
            &cmdlineP->verbose,                  0);

    opt.opt_table = option_def;
    opt.short_allowed = TRUE;  /* We have short (old-fashioned) options */
    opt.allowNegNum = FALSE;   /* We have no parms that are negative numbers */

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!widthSpec)
        cmdlineP->width = 640;

    if (!heightSpec)
        cmdlineP->height = 480;

    if (cmdlineP->orderSpec) {
        if (cmdlineP->order > 0x0f) {
            pm_error("Invalid --order value %u.  Maximum possible is 15",
                     cmdlineP->order);
        }
    }

    if (cmdlineP->maxoffsetSpec) {
        if (cmdlineP->maxoffset > 127) {
            pm_error("Invalid --maxoffst value %u.  Maximum is 127",
                cmdlineP->maxoffset);
        }
    }

    if (cmdlineP->stripesSpec) {
        if (cmdlineP->stripes < 1)
            pm_error("--stripes must be at least 1");
    }

    if (argc-1 < 2) {
        cmdlineP->outputFilename = "-";
        if (argc-1 < 1)
            cmdlineP->inputFilename = "-";
        else
            cmdlineP->inputFilename = argv[1];
    } else {
        cmdlineP->outputFilename = argv[2];
        if (argc-1 > 2) {
            pm_error("Too many arguments (%u).  The only possible non-option "
                     "arguments are input file name and output file name",
                     argc-1);
        }
    }
}



static void
dataOut(unsigned char * const start,
        size_t          const len,
        void *          const fileP) {
/*----------------------------------------------------------------------------
  Callback procedure which is used by JBIG encoder to deliver the
  encoded data. It simply sends the bytes to the output file.
-----------------------------------------------------------------------------*/
    fwrite(start, len, 1, (FILE *) fileP);

    total_length += len;
}



static void
readPbm(FILE *            const ifP,
        unsigned int      const cols,
        unsigned int      const rows,
        unsigned char *** const bitmapP) {

    unsigned int const bytesPerLine = pbm_packed_bytes(cols);

    unsigned char ** bitmap;

    /* Read the input image into bitmap[] */
    /* Shortcut for PBM */
    unsigned int row;

    MALLOCVAR_NOFAIL(bitmap);

    if (UINT_MAX / bytesPerLine < rows)
        pm_error("Image is uncomputably large");

    MALLOCARRAY(bitmap[0], bytesPerLine * rows);

    if (!bitmap[0]) {
        pm_error("Failed to allocate a buffer for %u rows of %u bytes",
                 rows, bytesPerLine);
    }

    for (row = 0; row < rows; ++row) {
        pbm_readpbmrow_packed(ifP, &bitmap[0][row*bytesPerLine],
                              cols, RPBM_FORMAT);
    }

    *bitmapP = bitmap;
}



static void
readImage(FILE *           const ifP,
          unsigned int     const cols,
          unsigned int     const rows,
          xelval           const maxval,
          int              const format,
          unsigned int     const bpp,
          unsigned char ** const imageP) {
/*----------------------------------------------------------------------------
  Read the input image and put it into *imageP;

  Although the PBM case is separated, this logic works also for
  PBM, bpp=1.
-----------------------------------------------------------------------------*/
    unsigned char * image;  /* malloc'ed */
        /* This is a representation of the entire image with 'bpp' bytes per
           pixel.  The 'bpp' bytes for each pixel are arranged MSB first
           and its numerical value is the value from the PNM input.
           The pixels are laid out in row-major format in this rectangle.

           The point of this data structure is it is what jbg_split_planes()
           wants for input.
        */
    xel* pnm_row;
    unsigned int row;

    pnm_row = pnm_allocrow(cols);  /* row buffer */
    MALLOCARRAY_NOFAIL(image, cols * rows * bpp);

    for (row = 0; row < rows; ++row) {
        unsigned int col;
        pnm_readpnmrow(ifP, pnm_row, cols, maxval, format);
        for (col = 0; col < cols; col++) {
            unsigned int j;
            /* Move each byte of the sample into image[], MSB first */
            for (j = 0; j < bpp; ++j)
                image[(((row*cols)+col) * bpp) + j] = (unsigned char)
                    PNM_GET1(pnm_row[col]) >> ((bpp-1-j) * 8);
        }
    }
    pnm_freerow(pnm_row);
    *imageP = image;
}



static void
convertImageToBitmap(unsigned char *   const image,
                     unsigned char *** const bitmapP,
                     unsigned int      const encodePlanes,
                     unsigned int      const bytesPerLine,
                     unsigned int      const lines) {

    /* Convert image[] into bitmap[]  */

    unsigned char ** bitmap;
    unsigned int i;

    MALLOCARRAY_NOFAIL(bitmap, encodePlanes);
    for (i = 0; i < encodePlanes; ++i)
        MALLOCARRAY_NOFAIL(bitmap[i], bytesPerLine * lines);

    *bitmapP = bitmap;
}



static void
readPnm(FILE *            const ifP,
        unsigned int      const cols,
        unsigned int      const rows,
        xelval            const maxval,
        int               const format,
        unsigned int      const bpp,
        unsigned int      const planes,
        unsigned int      const encodePlanes,
        bool              const useGraycode,
        unsigned char *** const bitmapP) {

    unsigned int const bytesPerLine = pbm_packed_bytes(cols);

    unsigned char * image;
    unsigned char ** bitmap;

    readImage(ifP, cols, rows, maxval, format, bpp, &image);

    convertImageToBitmap(image, &bitmap, encodePlanes, bytesPerLine, rows);

    jbg_split_planes(cols, rows, planes, encodePlanes, image, bitmap,
                     useGraycode);
    free(image);

    /* Invert the image if it is just one plane.  See top of this file
       for an explanation why.  Because of the separate handling of PBM,
       this is for exceptional PGM files.
    */

    if (encodePlanes == 1) {
        unsigned int row;
        for (row = 0; row < rows; ++row) {
            unsigned int i;

            for (i = 0; i < bytesPerLine; ++i)
                bitmap[0][(row*bytesPerLine) + i] ^= 0xff;

            if (cols % 8 > 0) {
                bitmap[0][ (row+1)*bytesPerLine  -1] >>= 8-cols%8;
                bitmap[0][ (row+1)*bytesPerLine  -1] <<= 8-cols%8;
            }
        }
    }
    *bitmapP = bitmap;
}



static void
reportVerbose(struct jbg_enc_state const s,
              int                  const useGraycode) {

    fprintf(stderr, "Information about the created JBIG bi-level image entity "
            "(BIE):\n\n");
    fprintf(stderr, "              input image size: %ld x %ld pixel\n",
            s.xd, s.yd);
    fprintf(stderr, "                    bit planes: %d\n", s.planes);
    if (s.planes > 1)
        fprintf(stderr, "                      encoding: %s code, MSB first\n",
                useGraycode ? "Gray" : "binary");
    fprintf(stderr, "                       stripes: %ld\n", s.stripes);
    fprintf(stderr, "   lines per stripe in layer 0: %ld\n", s.l0);
    fprintf(stderr, "  total number of diff. layers: %d\n", s.d);
    fprintf(stderr, "           lowest layer in BIE: %d\n", s.dl);
    fprintf(stderr, "          highest layer in BIE: %d\n", s.dh);
    fprintf(stderr, "             lowest layer size: %lu x %lu pixel\n",
            jbg_ceil_half(s.xd, s.d - s.dl), jbg_ceil_half(s.yd, s.d - s.dl));
    fprintf(stderr, "            highest layer size: %lu x %lu pixel\n",
            jbg_ceil_half(s.xd, s.d - s.dh), jbg_ceil_half(s.yd, s.d - s.dh));
    fprintf(stderr, "                   option bits:%s%s%s%s%s%s%s\n",
            s.options & JBG_LRLTWO  ? " LRLTWO" : "",
            s.options & JBG_VLENGTH ? " VLENGTH" : "",
            s.options & JBG_TPDON   ? " TPDON" : "",
            s.options & JBG_TPBON   ? " TPBON" : "",
            s.options & JBG_DPON    ? " DPON" : "",
            s.options & JBG_DPPRIV  ? " DPPRIV" : "",
            s.options & JBG_DPLAST  ? " DPLAST" : "");
    fprintf(stderr, "                    order bits:%s%s%s%s\n",
            s.order & JBG_HITOLO ? " HITOLO" : "",
            s.order & JBG_SEQ    ? " SEQ" : "",
            s.order & JBG_ILEAVE ? " ILEAVE" : "",
            s.order & JBG_SMID   ? " SMID" : "");
    fprintf(stderr, "           AT maximum x-offset: %d\n"
            "           AT maximum y-offset: %d\n", s.mx, s.my);
    fprintf(stderr, "         length of output file: %lu byte\n\n",
            total_length);
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    FILE * ofP;
    int bpp, planes, encodePlanes;
    int cols, rows;
    xelval maxval;
    int format;
    unsigned char **bitmap;
    /* This is an array of the planes of the image.  Each plane is a
       two-dimensional array of pixels laid out in row-major format.
       format with each pixel being one bit.  A byte in the array
       contains 8 pixels left to right, msb to lsb.
    */

    struct jbg_enc_state s;
    int options;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFilename);
    ofP = pm_openw(cmdline.outputFilename);

    pnm_readpnminit(ifP, &cols, &rows, &maxval, &format);

    if (PNM_FORMAT_TYPE(format) != PGM_TYPE &&
        PNM_FORMAT_TYPE(format) != PBM_TYPE) {
        pm_error("This program accepts PBM and PGM input only.  "
                 "Try Ppmtopgm.");
    }

    planes = pm_maxvaltobits(maxval);

    /* In a JBIG file, maxvals are determined only by the number of planes,
       so must be a power of 2 minus 1
    */

    if ((1UL << planes)-1 != maxval)
        pm_error("Input image has unacceptable maxval: %d.  JBIG files must "
                 "have a maxval which is a power of 2 minus 1.  Use "
                 "Ppmdepth to adjust the image's maxval", maxval);

    bpp = (planes + 7) / 8;

    encodePlanes = cmdline.planesSpec ? MIN(cmdline.planes, planes) : planes;

    if (bpp == 1 && PNM_FORMAT_TYPE(format) == PBM_TYPE)
        readPbm(ifP, cols, rows, &bitmap);
    else
        readPnm(ifP, cols, rows, maxval, format, bpp,
                planes, encodePlanes, !cmdline.binary,
                &bitmap);

    /* Apply JBIG algorithm and write BIE to output file */

    /* initialize parameter struct for JBIG encoder*/
    jbg_enc_init(&s, cols, rows, encodePlanes, bitmap, dataOut, ofP);

    /* Select number of resolution layers either directly or based on a given
       maximum size for the lowest resolution layer
    */
    if (cmdline.singlelayer)
        jbg_enc_layers(&s, 0);
    else if (cmdline.differentialSpec)
        jbg_enc_layers(&s, cmdline.differential);
    else
        jbg_enc_lrlmax(&s, cmdline.width, cmdline.height);

    if (cmdline.algorithmSpec)
        options = cmdline.algorithm;
    else
        options = JBG_TPDON | JBG_TPBON | JBG_DPON;

    if (cmdline.annexc)
        options |= JBG_DELAY_AT;

    jbg_enc_lrange(&s, cmdline.lowestlayerSpec ? cmdline.lowestlayer : -1,
                   cmdline.highestlayerSpec ? cmdline.highestlayer : -1);

    if (cmdline.orderSpec)
        jbg_enc_set_order(&s, cmdline.order);

    if (cmdline.algorithmSpec)
        jbg_enc_set_algorithm(&s, cmdline.algorithm);

    if (cmdline.stripesSpec)
        jbg_enc_set_stripes(&s, cmdline.stripes);

    if (cmdline.maxoffsetSpec)
        jbg_enc_set_maxoffset(&s, cmdline.maxoffset);

    jbg_enc_out(&s);
        /* Encode everything and send it to dataOut() */

    jbg_enc_free(&s);

    if (ferror(ofP)) {
        pm_error("Problem while writing output file '%s'.  %s",
                 cmdline.outputFilename, strerror(errno));
    }
    pm_close(ofP);

    if (cmdline.verbose)
        reportVerbose(s, !cmdline.binary);

    return 0;
}



