#include <stdlib.h>
#include <string.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pgm.h"



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    double grayLevel;
    unsigned int cols;
    unsigned int rows;
    gray maxval;
};



static double
grayLevelFromArg(const char * const arg) {

    double retval;

    if (strlen(arg) < 1)
        pm_error("Gray level argument is a null string");
    else {
        char * endPtr;

        retval = strtod(arg, &endPtr);

        if (*endPtr != '\0')
            pm_error("Gray level argument '%s' is not a floating point number",
                     arg);

        if (retval < 0.0)
            pm_error("You can't have a negative gray level (%f)", retval);
        if (retval > 1.0)
            pm_error("Gray level must be in the range [0.0, 1.0].  "
                     "You specified %f", retval);

    }
    return retval;
}



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
  Convert program invocation arguments (argc,argv) into a format the 
  program can use easily, struct cmdlineInfo.  Validate arguments along
  the way and exit program with message if invalid.

  Note that some string information we return as *cmdlineP is in the storage 
  argv[] points to.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int maxvalSpec;
    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0,   "maxval",    OPT_UINT, &cmdlineP->maxval, &maxvalSpec,    0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!maxvalSpec)
        cmdlineP->maxval = PGM_MAXMAXVAL;
    else {
        if (cmdlineP->maxval > PGM_OVERALLMAXVAL)
            pm_error("The value you specified for -maxval (%u) is too big.  "
                     "Max allowed is %u", cmdlineP->maxval, PGM_OVERALLMAXVAL);

        if (cmdlineP->maxval < 1)
            pm_error("You cannot specify 0 for -maxval");
    }

    if (argc-1 < 3)
        pm_error("Need 3 arguments: gray level, width, height.");
    else if (argc-1 > 3)
        pm_error("Only 3 arguments allowed: gray level, width, height.  "
                 "You specified %d", argc-1);
    else {
        cmdlineP->grayLevel = grayLevelFromArg(argv[1]);
        cmdlineP->cols = pm_parse_width(argv[2]);
        cmdlineP->rows = pm_parse_height(argv[3]);
    }
    free(option_def);
}



int
main(int argc, const char ** const argv) {

    struct CmdlineInfo cmdline;
    gray * grayrow;
    unsigned int col, row;
    gray grayLevel;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    grayLevel = pgm_unnormalize(cmdline.grayLevel, cmdline.maxval);

    pgm_writepgminit(stdout, cmdline.cols, cmdline.rows, cmdline.maxval, 0);

    grayrow = pgm_allocrow(cmdline.cols);

    /* All rows are identical.  Fill once. */
    for (col = 0; col < cmdline.cols; ++col)
        grayrow[col] = grayLevel;

    for (row = 0; row < cmdline.rows; ++row)
        pgm_writepgmrow(stdout, grayrow, cmdline.cols, cmdline.maxval, 0);

    pgm_freerow(grayrow);
    pm_close(stdout);

    return 0;
}


