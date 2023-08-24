/* pbmtopgm.c - convert PBM to PGM by totalling pixels over sample area
 * AJCD 12/12/90
 */

#include <stdio.h>
#include <limits.h>

#include "pm_c_util.h"
#include "nstring.h"
#include "pgm.h"



struct CmdlineInfo {
    unsigned int convCols;
    unsigned int convRows;
    const char * inputFileName;
};



static void
parseCommandLine(int                  const argc,
                 const char **        const argv,
                 struct CmdlineInfo * const cmdlineP) {

    const char * error; /* error message from pm_string_to_uint */

    if (argc-1 < 2)
        pm_error("Insufficient arguments (%d).  Need width and height "
                 "of convolution kernel, in pixels", argc-1);
    else {
        pm_string_to_uint(argv[1], &cmdlineP->convCols, &error);
        if (error)
            pm_error("Invalid convolution kernel width argument.  %s", error);

        pm_string_to_uint(argv[2], &cmdlineP->convRows, &error);
        if (error)
            pm_error("Invalid convolution kernel height argument. %s", error);
        if (cmdlineP->convCols < 1 || cmdlineP->convRows < 1)
            pm_error("convolution kernel width and height must be > 0");

        if (argc-1 >= 3)
            cmdlineP->inputFileName = argv[3];
        else {
            cmdlineP->inputFileName = "'";

            if (argc-1 > 3)
                pm_error("Too many arguments (%d).  The most possible are "
                         "convolution kernel width and height "
                         "and input file name", argc-1);
        }
    }
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;
    gray * outrow;
    gray maxval;
    unsigned int right, left, down, up;
    bit ** inbits;
    int rows, cols;
    FILE * ifP;
    unsigned int row;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    inbits = pbm_readpbm(ifP, &cols, &rows);

    if (cmdline.convCols > cols)
        pm_error("You specified a convolution kernel width (%u columns) "
                 "which is greater than the image width (%u columns)",
                 cmdline.convCols, cols);
    if (cmdline.convRows > rows)
        pm_error("You specified a convolution kernel height (%u rows) "
                 "which is greater than the image height (%u rows)",
                 cmdline.convRows, rows);

    left = cmdline.convCols / 2;  right = cmdline.convCols - left;
    up   = cmdline.convRows / 2;  down  = cmdline.convRows - up;

    outrow = pgm_allocrow(cols) ;
    maxval = MIN(PGM_OVERALLMAXVAL, cmdline.convCols * cmdline.convRows);
    pgm_writepgminit(stdout, cols, rows, maxval, 0) ;

    for (row = 0; row < rows; ++row) {
        unsigned int const t = (row > up) ? (row - up) : 0;
        unsigned int const b = (row + down < rows) ? (row + down) : rows;
        unsigned int const actualConvRows =
            cmdline.convRows - (t - row + up) - (row + down - b);
        unsigned int col;
        for (col = 0; col < cols; ++col) {
            unsigned int const l = (col > left) ? (col - left) : 0;
            unsigned int const r = (col + right < cols) ? (col + right) : cols;
            unsigned int const actualConvCols =
                cmdline.convCols - (l - col + left) - (col + right - r);
            unsigned int value;
            unsigned int x;

            for (x = l, value = 0; x < r; ++x) {
                unsigned int y;
                for (y = t; y < b; ++y)
                    if (inbits[y][x] == PBM_WHITE)
                        ++value;
            }
            {
                unsigned int const convKernelArea =
                    actualConvRows * actualConvCols;
                outrow[col] = (gray) (((double) value/convKernelArea)*maxval);
            }
        }
        pgm_writepgmrow(stdout, outrow, cols, maxval, 0) ;
    }
    pbm_freearray(inbits, rows);
    pm_close(ifP);

    return 0;
}
