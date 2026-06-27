#include <stdbool.h>
#include <stdio.h>

#include "mallocvar.h"
#include "shhopt.h"
#include "pbm.h"



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* Filespec of input file */
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
  Convert program invocation arguments (argc,argv) into a format the
  program can use easily, struct CmdlineInfo.  Validate arguments along
  the way and exit program with message if invalid.

  Note that some string information we return as *cmdlineP is in the storage
  argv[] points to.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    /* Following is just to avoid an unused variable warning */
    OPTENT3(0,   "verbose",    OPT_FLAG, NULL, NULL,    0);

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    free(option_def);

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else
        cmdlineP->inputFileName = argv[1];

    if (argc-1 > 1)
        pm_error("Too many arguments (%u).  Only possible argument is "
                 "input file name", argc-1);
}



static unsigned int const headerSize = 16;
    /* Just a guess at the header size */



static void
writeBrushHeader(unsigned int const cols,
                 unsigned int const rows,
                 FILE *       const ofP) {

    unsigned char header[headerSize];
    size_t bytesWrittenCt;
    unsigned int i;

    header[0] = 1;
    header[1] = 0;
    header[2] = (unsigned char)(cols>>8 & 0xFF);
    header[3] = (unsigned char)(cols>>0 & 0xFF);
    header[4] = (unsigned char)(rows>>8 & 0xFF);
    header[5] = (unsigned char)(rows>>0 & 0xFF);

    for (i = 6; i < headerSize; ++i)
        header[i] = 0;

    bytesWrittenCt =  fwrite(header, 1, headerSize, ofP);
    if (bytesWrittenCt != headerSize)
        pm_error("Failed to write the brush header");
}



static void
convertRaster(FILE *       const ifP,
              unsigned int const cols,
              unsigned int const rows,
              int          const format,
              FILE *       const ofP) {

    unsigned char * bitrow;  /* malloc'ed */
    unsigned int row;

    bitrow = pbm_allocrow_packed(cols + 16);

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        pbm_readpbmrow_packed(ifP, bitrow, cols, format);

        for (col = 0; col < cols; ++col)
            bitrow[col] = ~bitrow[col];

        pbm_cleanrowend_packed(bitrow, cols);

        fwrite(bitrow, 1, 2*((cols + 15)/16), ofP);
    }

    pbm_freerow_packed(bitrow);
}



int
main(int argc, const char **argv) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    int cols, rows;
    int format;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    pbm_readpbminit(ifP, &cols, &rows, &format);

    if (cols > 65535)
        pm_error("Image too wide (%u columns).  Max is 65535", cols);
    if (rows > 65535)
        pm_error("Image too high (%u rows).  Max is 65535", rows);

    writeBrushHeader(cols, rows, stdout);

    convertRaster(ifP, cols, rows, format, stdout);

    pm_close(ifP);

    return 0;
}


