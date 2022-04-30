/*=============================================================================
                               pamshuffle
===============================================================================
  Part of the Netpbm package.

  Relocate pixels in row, randomly, using Fisher-Yates shuffling.

  By Akira F. Urushibata

  Contributed to the public domain by its author.
=============================================================================*/

#include <assert.h>
#include "pm_c_util.h"
#include "pam.h"
#include "rand.h"
#include "shhopt.h"

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;
    unsigned int column;
    unsigned int randomseedSpec;
    unsigned int randomseed;
};

static void
parseCommandLine(int argc, const char ** const argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options. */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY(option_def, 100);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = true;  /* We have no parms that are negative numbers */

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "column",     OPT_FLAG,   NULL,
                               &cmdlineP->column,                    0);
    OPTENT3(0,   "randomseed", OPT_UINT,   &cmdlineP->randomseed,
                               &cmdlineP->randomseedSpec,            0);

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    free(option_def);

    if (argc-1 > 1)
        pm_error("Too many arguments (%u). "
                 "The only possible argument is the input file name.", argc-1);
    else if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else
        cmdlineP->inputFileName = argv[1];

}



static void
shuffleRow(tuple *            const tuplerow,
           unsigned int       const cols,
           struct pm_randSt * const randStP) {

    unsigned int col;

    for (col = 0; col + 1 < cols; ++col) {
        tuple        const temp    = tuplerow[col];
        unsigned int const randcol = col + pm_rand(randStP) % (cols - col);

        assert(randcol >= col );
        assert(randcol < cols);

        /* swap */
        tuplerow[col]     = tuplerow[randcol];
        tuplerow[randcol] = temp;
    }
}



int
main(int argc, const char * argv[]) {

    FILE * ifP;
    int    eof;     /* no more images in input stream */

    struct CmdlineInfo cmdline;
    struct pam inpam;   /* Input PAM image */
    struct pam outpam;  /* Output PAM image */
    struct pm_randSt randSt;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    pm_randinit(&randSt);
    pm_srand2(&randSt, cmdline.randomseedSpec, cmdline.randomseed);

    for (eof = FALSE; !eof;) {
        tuple * inrow;   /* Input row buffer */
        tuple * outrow;  /* Pointers into the input row buffer to reorder it */
        unsigned int row, col;

        pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

        outpam = inpam;
        outpam.file = stdout;

        pnm_writepaminit(&outpam);

        inrow = pnm_allocpamrow(&inpam);

        MALLOCARRAY(outrow, inpam.width);

        if (!outrow)
            pm_error("Unable to allocate memory for %u-column output buffer",
                     inpam.width);

        for (col = 0; col < inpam.width; ++col)
            outrow[col] = inrow[col];

        for (row = 0; row < inpam.height; ++row) {
            pnm_readpamrow(&inpam, inrow);

            if (cmdline.column && row > 0) {
                /* Use the same shuffle ('outrow') as the previous row */
            } else
                shuffleRow(outrow, inpam.width, &randSt);

            pnm_writepamrow(&outpam, outrow);
        }

        pnm_freepamrow(inrow);
        free(outrow);
        pnm_nextimage(ifP, &eof);
    }

    pm_randterm(&randSt);

    return 0;
}
