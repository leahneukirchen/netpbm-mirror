
/*********************************************************************/
/* pgmnoise -  create a portable graymap with white noise            */
/* Frank Neumann, October 1993                                       */
/* V1.1 16.11.1993                                                   */
/*                                                                   */
/* version history:                                                  */
/* V1.0 12.10.1993  first version                                    */
/* V1.1 16.11.1993  Rewritten to be NetPBM.programming conforming    */
/*********************************************************************/

#include "pgm.h"


int main(int    argc,
         char * argv[]) {

    int argn, rows, cols;
    unsigned int row;
    gray * destrow;

    const char * const usage = "width height\n        width and height are picture dimensions in pixels\n";

    /* parse in 'default' parameters */
    pgm_init(&argc, argv);

    argn = 1;

    /* parse in dim factor */
    if (argn == argc)
        pm_usage(usage);
    if (sscanf(argv[argn], "%d", &cols) != 1)
        pm_usage(usage);
    argn++;
    if (argn == argc)
        pm_usage(usage);
    if (sscanf(argv[argn], "%d", &rows) != 1)
        pm_usage(usage);

    if (cols <= 0 || rows <= 0)
        pm_error("picture dimensions should be positive numbers");
    ++argn;

    if (argn != argc)
        pm_usage(usage);

    destrow = pgm_allocrow(cols);

    pgm_writepgminit(stdout, cols, rows, PGM_MAXMAXVAL, 0);

    srand(pm_randseed());

    /* create the (gray) noise */

    for (row = 0; row < rows; ++row) {
        unsigned int col;
        for (col = 0; col < cols; ++col)
            destrow[col] = rand() % (PGM_MAXMAXVAL + 1);

        pgm_writepgmrow(stdout, destrow, cols, PGM_MAXMAXVAL, 0);
    }

    pgm_freerow(destrow);

    return 0;
}
