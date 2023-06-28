/*********************************************************************/
/* ppmspread -  randomly displace a PPM's pixels by a certain amount */
/* Frank Neumann, October 1993                                       */
/* V1.1 16.11.1993                                                   */
/*                                                                   */
/* version history:                                                  */
/* V1.0 12.10.1993    first version                                  */
/* V1.1 16.11.1993    Rewritten to be NetPBM.programming conforming  */
/*********************************************************************/

#include <string.h>

#include "nstring.h"
#include "rand.h"
#include "shhopt.h"
#include "ppm.h"


struct CmdlineInfo {
    /* This structure represents all of the information the user
       supplied in the command line but in a form that's easy for the
       program to use.
    */
    const char * inputFilename;  /* '-' if stdin */
    unsigned int spread;
    unsigned int randomseedSpec;
    unsigned int randomseed;
};



static void
parseCommandLine(int argc, const char ** const argv,
                 struct CmdlineInfo * const cmdlineP ) {

    optEntry     * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options */
    optStruct3     opt;
    unsigned int   option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);
    option_def_index = 0;          /* Incremented by OPTENTRY */

    OPTENT3(0, "randomseed", OPT_UINT,   &cmdlineP->randomseed,
            &cmdlineP->randomseedSpec, 0);

    opt.opt_table = option_def;
    opt.short_allowed = 0;
    opt.allowNegNum = 1;

    pm_optParseOptions3( &argc, (char **)argv, opt, sizeof(opt), 0 );

    if (argc-1 < 1)
        pm_error("You must specify the spread factor as an argument");
    else {
        const char * error;
        pm_string_to_uint(argv[1], &cmdlineP->spread, &error);

        if (error)
            pm_error("Spread factor '%s' is not an unsigned integer.  %s",
                     argv[1], error);

        if (argc-1 < 2)
            cmdlineP->inputFilename = "-";
        else {
            cmdlineP->inputFilename = argv[2];
            if (argc-1 >2)
                pm_error("Too many arguments: %u.  "
                         "The only possible arguments are "
                         "the spread factor and the optional input file name",
                         argc-1);
        }
    }
}



static void
spreadRow(pixel **           const srcarray,
          unsigned int       const cols,
          unsigned int       const rows,
          unsigned int       const spread,
          unsigned int       const row,
          pixel **           const destarray,
          struct pm_randSt * const randStP) {

    unsigned int col;

    for (col = 0; col < cols; ++col) {
        pixel const p = srcarray[row][col];

        int const xdis = (pm_rand(randStP) % (spread + 1) )
            - ((spread + 1) / 2);
        int const ydis = (pm_rand(randStP) % (spread + 1))
            - ((spread + 1) / 2);

        int const xnew = col + xdis;
        int const ynew = row + ydis;

        /* only set the displaced pixel if it's within the bounds
           of the image
        */
        if (xnew >= 0 && xnew < cols && ynew >= 0 && ynew < rows) {
            /* Displacing a pixel is accomplished by swapping it
               with another pixel in its vicinity.
            */
            pixel const p2 = srcarray[ynew][xnew];
                /* Original value of second pixel */

            /* Set second pixel to new value */
            PPM_ASSIGN(destarray[ynew][xnew],
                       PPM_GETR(p), PPM_GETG(p), PPM_GETB(p));

            /* Set first pixel to (old) value of second */
            PPM_ASSIGN(destarray[row][col],
                       PPM_GETR(p2), PPM_GETG(p2), PPM_GETB(p2));
        } else {
            /* Displaced pixel is out of bounds; leave the old pixel there.
            */
            PPM_ASSIGN(destarray[row][col],
                       PPM_GETR(p), PPM_GETG(p), PPM_GETB(p));
        }
    }
}



int
main(int          argc,
     const char * argv[]) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    int rows, cols;
    unsigned int row;
    pixel ** destarray;
    pixel ** srcarray;
    pixval maxval;
    struct pm_randSt randSt;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFilename);

    srcarray = ppm_readppm(ifP, &cols, &rows, &maxval);

    destarray = ppm_allocarray(cols, rows);

    pm_randinit(&randSt);
    pm_srand2(&randSt, cmdline.randomseedSpec, cmdline.randomseed);

    /* clear out the buffer */
    for (row = 0; row < rows; ++row)
        memset(destarray[row], 0, cols * sizeof(pixel));

    /* Displace pixels */
    for (row = 0; row < rows; ++row) {
        spreadRow(srcarray, cols, rows, cmdline.spread, row,
                  destarray, &randSt);

    }
    pm_randterm(&randSt);

    ppm_writeppm(stdout, destarray, cols, rows, maxval, 0);

    pm_close(ifP);
    ppm_freearray(srcarray, rows);
    ppm_freearray(destarray, rows);

    return 0;
}


