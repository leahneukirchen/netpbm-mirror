/*********************************************************************/
/* ppmshift -  shift lines of a picture left or right by x pixels    */
/* Frank Neumann, October 1993                                       */
/* V1.1 16.11.1993                                                   */
/*                                                                   */
/* version history:                                                  */
/* V1.0    11.10.1993  first version                                 */
/* V1.1    16.11.1993  Rewritten to be NetPBM.programming conforming */
/*********************************************************************/

#include <stdbool.h>

#include "mallocvar.h"
#include "rand.h"
#include "shhopt.h"
#include "ppm.h"


struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;

    unsigned int shift;
    unsigned int seedSpec;
    unsigned int seed;
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
    OPTENT3(0,   "seed",            OPT_UINT,     &cmdlineP->seed,
            &cmdlineP->seedSpec,         0);

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 < 1)
        pm_error("You must specify the shift factor as an argument");
    else {
        int const arg1 = atoi(argv[1]);
        if (arg1 < 0)
            pm_error("shift factor must be 0 or more");
        cmdlineP->shift = arg1;

        if (argc-1 < 2)
            cmdlineP->inputFileName = "-";
        else {
            cmdlineP->inputFileName = argv[2];

            if (argc-1 > 2)
                pm_error("Too many arguments (%u).  "
                         "Shift factor and input file name are the only "
                         "possible arguments", argc-1);
        }
    }
    free(option_def);
}



static void
shiftRow(pixel *            const srcrow,
         unsigned int       const cols,
         unsigned int       const shift,
         pixel *            const destrow,
         struct pm_randSt * const randStP) {

    /* the range by which a line is shifted lays in the range from */
    /* -shift/2 .. +shift/2 pixels; however, within this range it is */
    /* randomly chosen */

    pixel * pP;
    pixel * pP2;
    int nowshift;

    if (shift != 0)
        nowshift = (pm_rand(randStP) % (shift+1)) - ((shift+1) / 2);
    else
        nowshift = 0;

    pP  = &srcrow[0];
    pP2 = &destrow[0];

    /* if the shift value is less than zero, we take the original
       pixel line and copy it into the destination line translated
       to the left by x pixels. The empty pixels on the right end
       of the destination line are filled up with the pixel that
       is the right-most in the original pixel line.
    */
    if (nowshift < 0) {
        unsigned int col;
        pP += abs(nowshift);
        for (col = 0; col < cols; ++col) {
            PPM_ASSIGN(*pP2, PPM_GETR(*pP), PPM_GETG(*pP), PPM_GETB(*pP));
            ++pP2;
            if (col < (cols + nowshift) - 1)
                ++pP;
        }
    } else {
        unsigned int col;
        /* The shift value is 0 or positive, so fill the first
           <nowshift> pixels of the destination line with the
           first pixel from the source line, and copy the rest of
           the source line to the dest line
        */
        for (col = 0; col < cols; ++col) {
            PPM_ASSIGN(*pP2, PPM_GETR(*pP), PPM_GETG(*pP), PPM_GETB(*pP));
            ++pP2;
            if (col >= nowshift)
                ++pP;
        }
    }
}



int
main(int argc, const char ** argv) {

    FILE * ifP;
    struct CmdlineInfo cmdline;
    int rows, cols, format;
    pixval maxval;
    pixel * srcrow;
    pixel * destrow;
    unsigned int row;
    unsigned int shift;
    struct pm_randSt randSt;

    /* parse in 'default' parameters */
    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    pm_randinit(&randSt);
    pm_srand2(&randSt, cmdline.seedSpec, cmdline.seed);

    ifP = pm_openr(cmdline.inputFileName);

    /* read first data from file */
    ppm_readppminit(ifP, &cols, &rows, &maxval, &format);

    if (cmdline.shift > cols) {
        shift = cols;
        pm_message("shift amount is larger than picture width - reset to %u",
                   shift);
    } else
        shift = cmdline.shift;

    srcrow  = ppm_allocrow(cols);
    destrow = ppm_allocrow(cols);

    ppm_writeppminit(stdout, cols, rows, maxval, 0);

    for (row = 0; row < rows; ++row) {
        ppm_readppmrow(ifP, srcrow, cols, maxval, format);

        shiftRow(srcrow, cols, shift, destrow, &randSt);

        ppm_writeppmrow(stdout, destrow, cols, maxval, 0);
    }

    ppm_freerow(destrow);
    ppm_freerow(srcrow);
    pm_close(ifP);
    pm_randterm(&randSt);

    return 0;
}
