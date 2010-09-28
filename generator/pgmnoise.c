/*********************************************************************
   pgmnoise -  create a PGM with white noise
   Frank Neumann, October 1993
*********************************************************************/

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pgm.h"


struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    unsigned int width;
    unsigned int height;
    unsigned int randomseed;
    unsigned int randomseedSpec;
};




static void
parseCommandLine(int argc, const char ** const argv,
                 struct cmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;
    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "randomseed",   OPT_INT,    &cmdlineP->randomseed,
            &cmdlineP->randomseedSpec,      0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We may have parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 != 2)
        pm_error("Wrong number of arguments: %u.  "
                 "Arguments are width and height of image, in pixels",
                 argc-1);
    else {
        int const width  = atoi(argv[1]);
        int const height = atoi(argv[2]);
        
        if (width <= 0)
            pm_error("Width must be positive, not %d", width);
        else
            cmdlineP->width = width;

        if (height <= 0)
            pm_error("Height must be positive, not %d", width);
        else
            cmdlineP->height = height;
    }
}




static void
pgmnoise(FILE * const ofP,
         unsigned int const cols,
         unsigned int const rows,
         gray         const maxval) {

    unsigned int row;
    gray * destrow;

    destrow = pgm_allocrow(cols);

    pgm_writepgminit(ofP, cols, rows, maxval, 0);

    for (row = 0; row < rows; ++row) {
        unsigned int col;
        for (col = 0; col < cols; ++col)
            destrow[col] = rand() % (maxval + 1);

        pgm_writepgmrow(ofP, destrow, cols, maxval, 0);
    }

    pgm_freerow(destrow);
}



int main(int          argc,
         const char * argv[]) {
    
    struct cmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    srand(cmdline.randomseedSpec ? cmdline.randomseed : pm_randseed());

    pgmnoise(stdout, cmdline.width, cmdline.height, PGM_MAXMAXVAL);

    return 0;
}

