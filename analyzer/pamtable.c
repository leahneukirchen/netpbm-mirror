/*=============================================================================
                               pamtable
===============================================================================
  Print the raster as a table of numbers.

  By Bryan Henderson, San Jose CA 2017.04.15.

  Contributed to the public domain

=============================================================================*/
#include <math.h>
#include "pm_c_util.h"
#include "pam.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "nstring.h"

enum Style {STYLE_BASIC, STYLE_TUPLE};

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* Name of input file */
    enum Style   outputStyle;
    unsigned int hex;
    unsigned int verbose;
};


static void
parseCommandLine(int argc, const char ** const argv,
                 struct CmdlineInfo * const cmdlineP) {

    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;
    unsigned int tuple;

    MALLOCARRAY(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */

    OPTENT3(0,   "tuple",     OPT_FLAG,  NULL, &tuple,               0);
    OPTENT3(0,   "hex",       OPT_FLAG,  NULL, &cmdlineP->hex,       0);
    OPTENT3(0,   "verbose",   OPT_FLAG,  NULL, &cmdlineP->verbose,   0);
        /* For future expansion */

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (tuple && hex)
        pm_error("-hex is invalid with -tuple");

    if (tuple)
        cmdlineP->outputStyle = STYLE_TUPLE;
    else
        cmdlineP->outputStyle = STYLE_BASIC;

    if (argc-1 > 1)
        pm_error("Too many arguments (%d).  File name is the only argument.",
                 argc-1);

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else
        cmdlineP->inputFileName = argv[1];

    free(option_def);
}



typedef struct {

    const char * sampleFmt;
       /* Printf format of a sample, e.g. %3u */

    const char * interSampleGutter;
       /* What we print between samples within a tuple */

    const char * interTupleGutter;
       /* What we print between tuples within a row */

    const char * rowStartString;
       /* What we print at the beginning of each row */

    const char * rowEndString;
       /* What we print at the end of each row */

} Format;



static double const
log16(double const arg) {

    return log(arg)/log(16);
}



static const char *
basicSampleFormat(const struct pam * const pamP,
                  bool               const wantHex) {
/*----------------------------------------------------------------------------
   The printf format string for a single sample in the output table.

   E.g "%03u".

   This format does not include any spacing between samples.
-----------------------------------------------------------------------------*/
    unsigned int cipherWidth;
    char         formatSpecifier;
    const char * flag;
    const char * retval;

    if (wantHex) {
        formatSpecifier = 'x';
        cipherWidth     = ROUNDU(ceil(log16(pamP->maxval + 1)));
        flag            = "0";
    } else {
        formatSpecifier = 'u';
        cipherWidth     = ROUNDU(ceil(log10(pamP->maxval + 1)));
        flag            = "";
    }
    pm_asprintf(&retval, "%%%s%u%c", flag, cipherWidth, formatSpecifier);

    return retval;
}



static void
makeFormat(const struct pam * const pamP,
           enum Style         const outputStyle,
           bool               const wantHex,
           Format *           const formatP) {

    switch (outputStyle) {
      case STYLE_BASIC:
          formatP->sampleFmt         = basicSampleFormat(pamP, wantHex);
          formatP->interSampleGutter = " ";
          formatP->interTupleGutter  = pamP->depth > 1 ? "|" : " ";
          formatP->rowStartString    = "";
          formatP->rowEndString      = "\n";
          break;
      case STYLE_TUPLE:
          formatP->sampleFmt         = pm_strdup("%u");
          formatP->interSampleGutter = ",";
          formatP->interTupleGutter  = ") (";
          formatP->rowStartString    = "(";
          formatP->rowEndString      = ")\n";
          break;
    }
}



static void
unmakeFormat(Format * const formatP) {

    pm_strfree(formatP->sampleFmt);
}



static void
printRow(const struct pam * const pamP,
         tuple *            const tupleRow,
         Format             const format,
         FILE *             const ofP) {

    unsigned int col;

    fputs (format.rowStartString, ofP);

    for (col = 0; col < pamP->width; ++col) {
        unsigned int plane;

        if (col > 0)
            fputs(format.interTupleGutter, ofP);

        for (plane = 0; plane < pamP->depth; ++plane) {

            if (plane > 0)
                fputs(format.interSampleGutter, ofP);

            fprintf(ofP, format.sampleFmt, tupleRow[col][plane]);
        }
    }

    fputs (format.rowEndString, ofP);
}



static void
printRaster(FILE *             const ifP,
            const struct pam * const pamP,
            FILE *             const ofP,
            enum Style         const outputStyle,
            bool               const wantHex) {

    Format format;

    tuple * inputRow;   /* Row from input image */
    unsigned int row;

    makeFormat(pamP, outputStyle, wantHex, &format);

    inputRow = pnm_allocpamrow(pamP);

    for (row = 0; row < pamP->height; ++row) {
        pnm_readpamrow(pamP, inputRow);

        printRow(pamP, inputRow, format, ofP);
    }

    pnm_freepamrow(inputRow);

    unmakeFormat(&format);
}



int
main(int argc, const char *argv[]) {

    FILE * ifP;
    struct CmdlineInfo cmdline;
    struct pam inpam;   /* Input PAM image */

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    printRaster(ifP, &inpam, stdout, cmdline.outputStyle, cmdline.hex);

    pm_close(inpam.file);

    return 0;
}



