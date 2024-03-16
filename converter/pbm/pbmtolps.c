/*=============================================================================
                             pbmtolps
===============================================================================

  Convert a PBM image to Postscript.  The output Postscript uses lines instead
  of the image operator to generate a (device dependent) picture which will be
  imaged much faster.

  The Postscript path length is constrained to be at most 1000 vertices so that
  no limits are overrun on the Apple Laserwriter and (presumably) no other
  printers.  The typical limit is 1500.  See "4.4 Path Construction" and
  "Appendix B: Implementation Limits" in: PostScript Language Reference Manual
  https://www.adobe.com/content/dam/acom/en/devnet/actionscript/
  articles/psrefman.pdf

  To do:
       make sure encapsulated format is correct
       repetition of black-white strips
       make it more device independent (is this possible?)

  Author:
       George Phillips <phillips@cs.ubc.ca>
       Department of Computer Science
       University of British Columbia
=============================================================================*/
#include <stdbool.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pbm.h"


static float        const MAX_DPI           = 5000;
static float        const MIN_DPI           = 10;
static unsigned int const MAX_PATH_VERTICES = 1000;


struct CmdlineInfo {
    /* All the information the user supplied in the command line, in a form
       easy for the program to use.
    */
    const char * inputFileName;  /* File name of input file */
    unsigned int inputFileSpec;  /* Input file name specified */
    float        lineWidth;      /* Line width, if specified */
    unsigned int lineWidthSpec;  /* Line width specified */
    float        dpi;            /* Resolution in DPI, if specified */
    unsigned int dpiSpec;        /* Resolution specified */
};



static void
validateDpi(float const dpi) {

    if (dpi > MAX_DPI || dpi < MIN_DPI)
        pm_error("Specified DPI value out of range (%f)", dpi);
}



static void
parseCommandLine(int                        argc,
                 const char **        const argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.
-----------------------------------------------------------------------------*/
    optEntry * option_def;  /* malloc'ed */
    optStruct3 opt;
    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "linewidth", OPT_FLOAT, &cmdlineP->lineWidth,
                            &cmdlineP->lineWidthSpec,    0);
    OPTENT3(0, "dpi",       OPT_FLOAT,  &cmdlineP->dpi,
                            &cmdlineP->dpiSpec,          0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (cmdlineP->dpiSpec)
        validateDpi(cmdlineP->dpi);
    else
        cmdlineP->dpi = 300;

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else {
        if (argc-1 > 1)
            pm_error("Program takes zero or one argument (filename).  You "
                     "specified %u", argc-1);
        else
            cmdlineP->inputFileName = argv[1];
    }

    if (cmdlineP->inputFileName[0] == '-' &&
        cmdlineP->inputFileName[1] == '\0')
        cmdlineP->inputFileSpec = false;
    else
        cmdlineP->inputFileSpec = true;

    free(option_def);
}



static void
validateLineWidth(float const scCols,
                  float const scRows,
                  float const lineWidth) {

    if (lineWidth >= scCols || lineWidth >= scRows)
        pm_error("Absurdly large -linewidth value (%f)", lineWidth);
}



static void
doRaster(FILE *       const ifP,
         unsigned int const cols,
         unsigned int const rows,
         int          const format,
         FILE *       const ofP) {

    bit *        bitrow;
    unsigned int row;
    unsigned int vertexCt;
        /* Number of vertices drawn since last stroke command */

    bitrow = pbm_allocrow(cols);

    for (row = 0, vertexCt = 0; row < rows; ++row) {
        unsigned int col;
        bool firstRun;

        firstRun = true;  /* initial value */

        pbm_readpbmrow(ifP, bitrow, cols, format);

        /* output white-strip + black-strip sequences */

        for (col = 0; col < cols; ) {
            unsigned int whiteCt;
            unsigned int blackCt;

            for (whiteCt = 0; col < cols && bitrow[col] == PBM_WHITE; ++col)
                ++whiteCt;
            for (blackCt = 0; col < cols && bitrow[col] == PBM_BLACK; ++col)
                ++blackCt;

            if (blackCt > 0) {
                if (vertexCt > MAX_PATH_VERTICES) {
                    printf("m ");
                    vertexCt = 0;
                }

                if (firstRun) {
                    printf("%u %u moveto %u 0 rlineto\n",
                           whiteCt, row, blackCt);
                    firstRun = false;
                } else
                    printf("%u %u a\n", blackCt, whiteCt);

                vertexCt += 2;
            }
        }
    }
    pbm_freerow(bitrow);
}



static void
pbmtolps(FILE *             const ifP,
         FILE *             const ofP,
         struct CmdlineInfo const cmdline) {

    const char * const psName =
        cmdline.inputFileSpec ? cmdline.inputFileName : "noname";

    int          rows;
    int          cols;
    int          format;
    float        scRows, scCols;
        /* Dimensions of the printed image in points */

    pbm_readpbminit(ifP, &cols, &rows, &format);

    scRows = (float) rows / (cmdline.dpi / 72.0);
    scCols = (float) cols / (cmdline.dpi / 72.0);

    if (cmdline.lineWidthSpec)
        validateLineWidth(scCols, scRows, cmdline.lineWidth);

    fputs("%!PS-Adobe-2.0 EPSF-2.0\n", ofP);
    fputs("%%Creator: pbmtolps\n", ofP);
    fprintf(ofP, "%%%%Title: %s\n", psName);
    fprintf(ofP, "%%%%BoundingBox: %d %d %d %d\n",
           (int)(305.5 - scCols / 2.0),
           (int)(395.5 - scRows / 2.0),
           (int)(306.5 + scCols / 2.0),
           (int)(396.5 + scRows / 2.0));
    fputs("%%EndComments\n", ofP);
    fputs("%%EndProlog\n", ofP);
    fputs("gsave\n", ofP);

    fprintf(ofP, "%f %f translate\n",
            306.0 - scCols / 2.0, 396.0 + scRows / 2.0);
    fprintf(ofP, "72 %f div dup neg scale\n", cmdline.dpi);

    if (cmdline.lineWidthSpec)
        fprintf(ofP, "%f setlinewidth\n", cmdline.lineWidth);

    fputs("/a { 0 rmoveto 0 rlineto } def\n", ofP);
    fputs("/m { currentpoint stroke newpath moveto } def\n", ofP);
    fputs("newpath 0 0 moveto\n", ofP);

    doRaster(ifP, cols, rows, format, ofP);

    fputs("stroke grestore showpage\n", ofP);
    fputs("%%Trailer\n", ofP);
}



int
main(int argc, const char *argv[]) {
    FILE *  ifP;
    struct CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    pbmtolps(ifP, stdout, cmdline);

    pm_close(ifP);

    return 0;
}



