#include <assert.h>
#include <string.h>

#include "nstring.h"
#include "mallocvar.h"
#include "pbm.h"

static char const bit_table[2][3] = {
    {1, 4, 0x10},
    {2, 8, 0x40}
};

static unsigned int const vmapWidth  = 132;
static unsigned int const vmapHeight = 23;



static void
initMap(unsigned char * const vmap) {

    unsigned int col;

    for (col = 0; col < vmapWidth; ++col) {
        unsigned int row;

        for (row = 0; row < vmapHeight; ++row)
            vmap[row * vmapWidth + col] = 0x20;
    }
}



static void
setVmap(unsigned char * const vmap,
        unsigned int    const x,
        unsigned int    const y) {

    unsigned int const ix = x/2;
    unsigned int const iy = y/3;

    assert(ix < vmapWidth);
    assert(iy < vmapHeight);

    vmap[iy * vmapWidth + ix] |= bit_table[x % 2][y % 3];
}



static void
fillMap(FILE *          const pbmFileP,
        unsigned char * const vmap) {

    unsigned int const xres = vmapWidth  * 2;
    unsigned int const yres = vmapHeight * 3;

    bit ** pbmImage;
    int cols;
    int rows;
    unsigned int row;

    pbmImage = pbm_readpbm(pbmFileP, &cols, &rows);

    for (row = 0; row < rows && row < yres; ++row) {
        unsigned int col;

        for (col = 0; col < cols && col < xres; ++col) {
            if (pbmImage[row][col] == PBM_WHITE)
                setVmap(vmap, col, row);
        }
    }
}



static void
printMap(unsigned char * const vmap,
         FILE *          const ofP) {

    unsigned int row;

    fputs("\033[H\033[J", ofP);  /* clear screen */
    fputs("\033[?3h",     ofP);  /* 132 column mode */
    fputs("\033)}\016",   ofP);  /* mosaic mode */

    for (row = 0; row < vmapHeight; ++row) {
        unsigned int endCol;
            /* Column number just past the non-space data in the row;
               (i.e. spaces on the right are padding; not data
            */
        unsigned int col;

        for (endCol = vmapWidth;
             endCol > 0 && vmap[row * vmapWidth + (endCol-1)] == 0x20;
             --endCol)
            ;

        for (col = 0; col < endCol; ++col)
            fputc(vmap[row * vmapWidth + col], ofP);

        fputc('\n', ofP);
    }

    fputs("\033(B\017", ofP);
}



int
main(int argc, const char ** argv) {

    int argn;
    const char * inputFileNm;
    FILE * ifP;

    unsigned char * vmap;

    pm_proginit(&argc, argv);

    for (argn = 1;
         argn < argc && argv[argn][0] == '-' && strlen(argv[argn]) > 1;
         ++argn) {
        pm_error("Unrecognized option '%s'", argv[argn]);
    }

    if (argn >= argc) {
        inputFileNm = "-";
    } else if(argc - argn != 1) {
        pm_error("Too many arguments.  At most one argument is allowed: "
                 "Name of the input file");
    } else {
        inputFileNm = argv[argn];
    }

    ifP = pm_openr(inputFileNm);

    MALLOCARRAY(vmap, vmapWidth * vmapHeight);
    if (!vmap)
        pm_error("Cannot allocate memory for %u x %u pixels",
                 vmapWidth, vmapHeight);

    initMap(vmap);
    fillMap(ifP, vmap);
    printMap(vmap, stdout);

    /* If the program failed, it previously aborted with nonzero completion
       code, via various function calls.
    */
    return 0;
}



