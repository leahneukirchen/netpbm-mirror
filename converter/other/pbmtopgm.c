/* pbmtopgm.c - convert PBM to PGM by totalling pixels over sample area
 * AJCD 12/12/90
 */

#include <stdio.h>
#include <limits.h>

#include "pm_c_util.h"
#include "nstring.h"
#include "pgm.h"

int
main(int argc, char *argv[]) {

    gray *outrow, maxval;
    int right, left, down, up;
    bit **inbits;
    int rows, cols;
    FILE *ifd;
    int row;
    unsigned int width, height;
    const char * const usage = "<w> <h> [pbmfile]";
    const char * error; /* error message of pm_string_to_uint */

    pgm_init( &argc, argv );

    if (argc > 4 || argc < 3)
        pm_usage(usage);

    pm_string_to_uint(argv[1], &width, &error);
    if (error)
        pm_error("Invalid width argument: %s", error);
    pm_string_to_uint(argv[2], &height, &error);
    if (error)
        pm_error("Invalid height argument: %s", error);
    if (width < 1 || height < 1)
        pm_error("width and height must be > 0");

    if (argc == 4)
        ifd = pm_openr(argv[3]);
    else
        ifd = stdin ;

    inbits = pbm_readpbm(ifd, &cols, &rows) ;

    if (width > cols)
        pm_error("You specified a sample width (%u columns) which is greater "
                 "than the image width (%u columns)", width, cols);
    if (height > rows)
        pm_error("You specified a sample height (%u rows) which is greater "
                 "than the image height (%u rows)", height, rows);
    if (width > INT_MAX / height)
        /* prevent overflow of "value" below */
        pm_error("sample area (%u columns %u rows) too large",
                 width, height);

    left = width  / 2;  right = width  - left;
    up   = height / 2;  down  = height - up;



    outrow = pgm_allocrow(cols) ;
    maxval = MIN(PGM_OVERALLMAXVAL, width*height);
    pgm_writepgminit(stdout, cols, rows, maxval, 0) ;

    for (row = 0; row < rows; row++) {
        int const t = (row > up) ? (row-up) : 0;
        int const b = (row+down < rows) ? (row+down) : rows;
        int const onv = height - (t-row+up) - (row+down-b);
        unsigned int col;
        for (col = 0; col < cols; col++) {
            int const l = (col > left) ? (col-left) : 0;
            int const r = (col+right < cols) ? (col+right) : cols;
            int const onh = width - (l-col+left) - (col+right-r);
            int value;  /* See above */
            int x;

            value = 0;  /* initial value */

            for (x = l; x < r; ++x) {
                int y;
                for (y = t; y < b; ++y)
                    if (inbits[y][x] == PBM_WHITE)
                        ++value;
            }
            outrow[col] = (gray) ((double) maxval*value/(onh*onv));
        }
        pgm_writepgmrow(stdout, outrow, cols, maxval, 0) ;
    }
    pm_close(ifd);

    return 0;
}
