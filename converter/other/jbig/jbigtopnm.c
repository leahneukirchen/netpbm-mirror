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
collect_image (unsigned char *data, size_t len, void *image) {
    static int cursor = 0;
    int i;

    for (i = 0; i < len; i++) {
        ((unsigned char *)image)[cursor++] = data[i];
    }
}



static void
write_pnm (FILE *fout, const unsigned char * const image, const int bpp,
           const int rows, const int cols, const int maxval,
           const int format) {

    int row;
    xel *pnm_row;

    pnm_writepnminit(fout, cols, rows, maxval, format, 0);

    pnm_row = pnm_allocrow(cols);

    for (row = 0; row < rows; row++) {
        int col;
        for (col = 0; col < cols; col++) {
            int j;
            for (j = 0; j < bpp; j++)
                PNM_ASSIGN1(pnm_row[col],
                            image[(((row*cols)+col) * bpp) + j]);
        }
        pnm_writepnmrow(fout, pnm_row, cols, maxval, format, 0);
    }

    pnm_freerow(pnm_row);
}



static void
write_raw_pbm(FILE * const fout,
              const unsigned char * const binary_image,
              int                   const cols,
              int                   const rows) {

    unsigned int const bytes_per_row = pbm_packed_bytes(cols);

    int row;

    pbm_writepbminit(fout, cols, rows, 0);

    for (row = 0; row < rows; ++row)
        pbm_writepbmrow_packed(fout, &binary_image[row*bytes_per_row], cols,
                               0);
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


int main (int argc, const char **argv)
{
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
        unsigned char * buffer;
        int result;

        MALLOCARRAY(buffer, BUFSIZE);
        if (!buffer)
            pm_error("Failed to get %u bytes of memory for buffer", BUFSIZE);

        /* send input file to decoder */
        jbg_dec_init(&s);
        jbg_dec_maxsize(&s, cmdline.xmax, cmdline.ymax);
        result = JBG_EAGAIN;
        do {
            size_t len;
            size_t cnt;
            unsigned char * p;

            len = fread(buffer, 1, BUFSIZE, ifP);
            if (len == 0)
                break;
            cnt = 0;
            p = &buffer[0];
            while (len > 0 && (result == JBG_EAGAIN || result == JBG_EOK)) {
                result = jbg_dec_in(&s, p, len, &cnt);
                p += cnt;
                len -= cnt;
            }
        } while (result == JBG_EAGAIN || result == JBG_EOK);
        if (ferror(ifP))
            pm_error("Error reading input file");
        if (result != JBG_EOK && result != JBG_EOK_INTR)
            pm_error("Invalid contents of input file.  %s",
                     jbg_strerror(result));
        if (cmdline.planeSpec && jbg_dec_getplanes(&s) <= cmdline.plane)
            pm_error("Image has only %u planes", jbg_dec_getplanes(&s));

        {
            /* Write it out */

            int rows, cols;
            int maxval;
            int bpp;
            bool justOnePlane;
            unsigned int plane_to_write;

            cols = jbg_dec_getwidth(&s);
            rows = jbg_dec_getheight(&s);
            maxval = pm_bitstomaxval(jbg_dec_getplanes(&s));
            bpp = (jbg_dec_getplanes(&s)+7)/8;

            if (jbg_dec_getplanes(&s) == 1) {
                justOnePlane = true;
                plane_to_write = 0;
            } else {
                if (cmdline.planeSpec) {
                    justOnePlane = true;
                    plane_to_write = cmdline.plane;
                } else
                    justOnePlane = false;
            }

            if (justOnePlane) {
                unsigned char * binary_image;

                pm_message("WRITING PBM FILE");

                binary_image=jbg_dec_getimage(&s, plane_to_write);
                write_raw_pbm(ofP, binary_image, cols, rows);
            } else {
                unsigned char *image;
                pm_message("WRITING PGM FILE");

                /* Write out all the planes */
                /* What jbig.doc doesn't tell you is that jbg_dec_merge_planes
                   delivers the image in chunks, in consecutive calls to
                   the data-out callback function.  And a row can span two
                   chunks.
                */
                image = malloc(cols*rows*bpp);
                jbg_dec_merge_planes(&s, !cmdline.binary, collect_image,
                                     image);
                write_pnm(ofP, image, bpp, rows, cols, maxval, PGM_TYPE);
                free(image);
            }
            jbg_dec_free(&s);
        }

        pm_close(ofP);
        pm_close(ifP);
        free(buffer);
    }
    return 0;
}



