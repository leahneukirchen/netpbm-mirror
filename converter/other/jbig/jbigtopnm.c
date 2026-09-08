/*
    jbigtopnm - JBIG to PNM converter

    This program was derived from jbgtopbm.c in Markus Kuhn's
    JBIG-KIT package by Bryan Henderson on 2000.05.11

    The main difference is that this version uses the Netpbm libraries.

 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

#include <jbig.h>

#include "pnm.h"
#include "shhopt.h"
#include "mallocvar.h"

#define BUFSIZE 8192



typedef struct {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;
    const char * outputFileName;
    unsigned long xmax;
    unsigned long ymax;
    unsigned int binary;
    unsigned int diagnose;
    unsigned int planeSpec;
    unsigned int plane;
} CmdlineInfo;



static void
parseCommandLine(int                 argc,
                 const char ** const argv,
                 CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;

    optStruct3 opt;

    unsigned int xmaxSpec, ymaxSpec;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "binary",   OPT_FLAG, NULL,             &cmdlineP->binary,   0);
    OPTENT3(0, "diagnose", OPT_FLAG, NULL,             &cmdlineP->diagnose, 0);
    OPTENT3(0, "plane",    OPT_UINT, &cmdlineP->plane, &cmdlineP->planeSpec,0);
    OPTENT3(0, "xmax",     OPT_UINT, &cmdlineP->xmax,  &xmaxSpec,           0);
    OPTENT3(0, "ymax",     OPT_UINT, &cmdlineP->ymax,  &ymaxSpec,           0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char**)argv, opt, sizeof(opt), 0);
    /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!xmaxSpec)
        cmdlineP->xmax = UINT_MAX;
    if (!ymaxSpec)
        cmdlineP->ymax = UINT_MAX;

    cmdlineP->inputFileName  = (argc-1 >= 1) ? argv[1] : "-";
    cmdlineP->outputFileName = (argc-1 >= 2) ? argv[2] : "-";

    if (argc-1 > 2)
        pm_error("Too  many arguments: %u.  The only possible "
                 "non-option arguments are input file name and "
                 "output file name", argc-1);
}



static void
collectImage(unsigned char * const data,
             size_t          const len,
             void *          const image) {
/*----------------------------------------------------------------------------
   This is a data-out callback function for libjbig's
   'jbg_dec_merge_planes'.

   We add the 'len' byts at *data to *image.
-----------------------------------------------------------------------------*/
    /* Future improvement: Instead of keeping context in static global
       variables, we should arrange for our third argument to be a pointer to
       an object that contains both the output buffer address and its cursor.
    */
    static int cursor = 0;

    unsigned int i;

    for (i = 0; i < len; ++i) {
        ((unsigned char *)image)[cursor++] = data[i];
    }
}



static void
writePnm(FILE *                const ofP,
         const unsigned char * const image,
         unsigned int          const bpp,
         unsigned int          const rows,
         unsigned int          const cols,
         xelval                const maxval,
         int                   const format) {

    unsigned int row;
    xel * xelrow;

    pnm_writepnminit(ofP, cols, rows, maxval, format, 0);

    xelrow = pnm_allocrow(cols);

    for (row = 0; row < rows; ++row) {
        unsigned int col;
        for (col = 0; col < cols; ++col) {
            unsigned int j;
            for (j = 0; j < bpp; ++j)
                PNM_ASSIGN1(xelrow[col],
                            image[(((row*cols)+col) * bpp) + j]);
        }
        pnm_writepnmrow(ofP, xelrow, cols, maxval, format, 0);
    }

    pnm_freerow(xelrow);
}



static void
writeRawPbm(FILE *                const ofP,
            const unsigned char * const binaryImage,
            unsigned int          const cols,
            unsigned int          const rows) {

    unsigned int const bytesPerRow = pbm_packed_bytes(cols);

    unsigned int row;

    pbm_writepbminit(ofP, cols, rows, 0);

    for (row = 0; row < rows; ++row)
        pbm_writepbmrow_packed(ofP, &binaryImage[row * bytesPerRow], cols, 0);
}



/*
 *
 */
static void
diagnose_bie(FILE *f)
{
  unsigned char bih[20];
  int len;
  unsigned long xd, yd, l0;

  len = fread(bih, 1, 20, f);
  if (len < 20) {
    printf("Input file is %d < 20 bytes long and does therefore not "
       "contain an intact BIE header!\n", len);
    return;
  }

  printf("Decomposition of BIH:\n\n  DL = %d\n  D  = %d\n  P  = %d\n"
     "  -  = %d\n  XD = %lu\n  YD = %lu\n  L0 = %lu\n  MX = %d\n"
     "  MY = %d\n",
     bih[0], bih[1], bih[2], bih[3],
     xd = ((unsigned long) bih[ 4] << 24) | ((unsigned long)bih[ 5] << 16)|
     ((unsigned long) bih[ 6] <<  8) | ((unsigned long) bih[ 7]),
     yd = ((unsigned long) bih[ 8] << 24) | ((unsigned long)bih[ 9] << 16)|
     ((unsigned long) bih[10] <<  8) | ((unsigned long) bih[11]),
     l0 = ((unsigned long) bih[12] << 24) | ((unsigned long)bih[13] << 16)|
     ((unsigned long) bih[14] <<  8) | ((unsigned long) bih[15]),
     bih[16], bih[17]);
  printf("  order   = %d %s%s%s%s%s\n", bih[18],
     bih[18] & JBG_HITOLO ? " HITOLO" : "",
     bih[18] & JBG_SEQ ? " SEQ" : "",
     bih[18] & JBG_ILEAVE ? " ILEAVE" : "",
     bih[18] & JBG_SMID ? " SMID" : "",
     bih[18] & 0xf0 ? " other" : "");
  printf("  options = %d %s%s%s%s%s%s%s%s\n", bih[19],
     bih[19] & JBG_LRLTWO ? " LRLTWO" : "",
     bih[19] & JBG_VLENGTH ? " VLENGTH" : "",
     bih[19] & JBG_TPDON ? " TPDON" : "",
     bih[19] & JBG_TPBON ? " TPBON" : "",
     bih[19] & JBG_DPON ? " DPON" : "",
     bih[19] & JBG_DPPRIV ? " DPPRIV" : "",
     bih[19] & JBG_DPLAST ? " DPLAST" : "",
     bih[19] & 0x80 ? " other" : "");
  printf("\n  %lu stripes, %d layers, %d planes\n\n",
     ((yd >> bih[1]) +  ((((1UL << bih[1]) - 1) & xd) != 0) + l0 - 1) / l0,
     bih[1] - bih[0], bih[2]);

  return;
}



static void
decompress(FILE *                 const ifP,
           struct jbg_dec_state * const sP) {

    unsigned char * buffer;
    bool eof;
    bool decompressFailed;
    /* The input is bad -- libjbig was unable to decompress it */
    int decompressFailCode;
    /* Meaningful only when 'decompressFailed' is true.  Result code
       from libjbig detailing why input could not be decompressed.
    */

    MALLOCARRAY(buffer, BUFSIZE);
    if (!buffer)
        pm_error("Failed to get %u bytes of memory for buffer", BUFSIZE);

    /* send input file to decoder */

    for (eof = false, decompressFailed = false;
         !eof && !decompressFailed;
        ) {
        size_t bytesRemainingCt;

        bytesRemainingCt = fread(buffer, 1, BUFSIZE, ifP);
        if (bytesRemainingCt == 0)
            eof = true;
        else {
            unsigned int cursor;

            for (cursor = 0; bytesRemainingCt > 0 && !decompressFailed; ) {

                int result;
                size_t bytesProcessedCt;

                result = jbg_dec_in(sP, &buffer[cursor], bytesRemainingCt,
                                    &bytesProcessedCt);
                if (result != JBG_EOK && result != JBG_EAGAIN) {
                    decompressFailed = true;
                    decompressFailCode = result;
                } else {
                    cursor += bytesProcessedCt;
                    bytesRemainingCt -= bytesProcessedCt;
                }
            }
        }
    }
    if (ferror(ifP))
        pm_error("Error reading input file");
    if (decompressFailed)
        pm_error("Invalid contents of input file.  %s",
                 jbg_strerror(decompressFailCode));

    free(buffer);
}



static void
writeDecompressedImage(FILE *                 const ofP,
                       struct jbg_dec_state * const sP,
                       bool                   const planeSpec,
                       unsigned int           const plane,
                       bool                   const binary) {

    unsigned int rows, cols;
    xelval maxval;
    unsigned int bpp;
        /* Number of bytes (not bits) per pixel; i.e. gray depth */
    bool justOnePlane;
    unsigned int planeToWrite;

    cols = jbg_dec_getwidth(sP);
    rows = jbg_dec_getheight(sP);
    maxval = pm_bitstomaxval(jbg_dec_getplanes(sP));
    bpp = (jbg_dec_getplanes(sP)+7)/8;

    if (jbg_dec_getplanes(sP) == 1) {
        justOnePlane = true;
        planeToWrite = 0;
    } else {
        if (planeSpec) {
            justOnePlane = true;
            planeToWrite = plane;
        } else
            justOnePlane = false;
    }

    if (justOnePlane) {
        unsigned char * binaryImage;

        pm_message("WRITING PBM FILE");

        binaryImage = jbg_dec_getimage(sP, planeToWrite);

        writeRawPbm(ofP, binaryImage, cols, rows);
    } else {
        unsigned char * image;
            /* A malloc'ed array of cols * rows * bits-per-pixel bits, in
               row-major bitplane-minor order
            */

        pm_message("WRITING PGM FILE");

        /* Write out all the planes */
        /* What jbig.doc doesn't tell you is that jbg_dec_merge_planes
           delivers the image in chunks, in consecutive calls to
           the data-out callback function.  And a row can span two
           chunks.
        */
        if (UINT_MAX / rows / cols < bpp) {
            pm_error("Image size %u rows by %u cols, %u bytes per pixel "
                     "too large for computations",
                     rows, cols, bpp);
        }

        image = malloc(cols * rows * bpp);

        jbg_dec_merge_planes(sP, !binary, collectImage, image);

        writePnm(ofP, image, bpp, rows, cols, maxval, PGM_TYPE);

        free(image);
    }
}



int
main (int argc, const char **argv) {

    CmdlineInfo cmdline;
    FILE * ifP;
    FILE * ofP;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);
    ofP = pm_openw(cmdline.outputFileName);

    if (cmdline.diagnose)
        diagnose_bie(ifP);
    else {
        struct jbg_dec_state s;

        jbg_dec_init(&s);
        jbg_dec_maxsize(&s, cmdline.xmax, cmdline.ymax);

        decompress(ifP, &s);

        if (cmdline.planeSpec && jbg_dec_getplanes(&s) <= cmdline.plane)
            pm_error("Image has only %u planes", jbg_dec_getplanes(&s));

        writeDecompressedImage(ofP, &s, !!cmdline.planeSpec, cmdline.plane,
                               !!cmdline.binary);

        jbg_dec_free(&s);

        pm_close(ofP);
        pm_close(ifP);
    }
    return 0;
}



