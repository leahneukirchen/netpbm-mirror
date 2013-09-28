#include <stdio.h>
#include <stdlib.h>
/* Because of poor design of libpng, you must not #include <setjmp.h> before
<png.h>.  Compile failure results.
*/
#include <png.h>
#include <setjmp.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "pam.h"
#include "pngx.h"



struct cmdlineInfo {
    const char * inputFileName;
};



static void
processCommandLine(int                  const argc,
                   char *               const argv[],
                   struct cmdlineInfo * const cmdlineP) {
        
    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else {
        cmdlineP->inputFileName = argv[1];

        if (argc-1 > 1)
            pm_error("Too many arguments.  "
                     "The only argument is the input file name.");
    }
}



static void
convertPamToPng(const struct pam * const pamP,
                const tuple *      const tuplerow,
                png_byte *         const pngRow) {
    
    unsigned int col;
    
    for (col = 0; col < pamP->width; ++col) {
        unsigned int plane;
        
        for (plane = 0; plane < 4; ++plane)
            pngRow[4 * col + plane] = tuplerow[col][plane];
    }
}



static void
writeRaster(const struct pam * const pamP,
            struct pngx *      const pngxP) {
    
    tuple * tupleRow;
    png_byte * pngRow;
    
    tupleRow = pnm_allocpamrow(pamP);
    MALLOCARRAY(pngRow, pamP->width * 4);

    if (pngRow == NULL)
        pm_error("Unable to allocate space for PNG pixel row.");
    else {
        unsigned int row;
        for (row = 0; row < pamP->height; ++row) {
            pnm_readpamrow(pamP, tupleRow);
            
            convertPamToPng(pamP, tupleRow, pngRow);
            
            png_write_row(pngxP->png_ptr, pngRow);
        }
        free(pngRow);
    }
    pnm_freepamrow(tupleRow);
}



static void
writePng(const struct pam * const pamP,
         FILE *             const ofP) {

    struct pngx * pngxP;

    pngx_create(&pngxP, PNGX_WRITE, NULL);
    
    pngx_setIhdr(pngxP, pamP->width, pamP->height,
                 8, PNG_COLOR_TYPE_RGB_ALPHA, 0, 0, 0);
        
    png_init_io(pngxP->png_ptr, ofP);

    pngx_writeInfo(pngxP);
        
    writeRaster(pamP, pngxP);

    pngx_writeEnd(pngxP);
        
    pngx_destroy(pngxP);
}
    


int
main(int argc, char * argv[]) {

    FILE * ifP;
    struct cmdlineInfo cmdline;
    struct pam pam;

    pnm_init(&argc, argv);

    processCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    pnm_readpaminit(ifP, &pam, PAM_STRUCT_SIZE(tuple_type));
    
    if (pam.depth < 4)
        pm_error("PAM must have depth at least 4 (red, green, blue, alpha).  "
                 "This one has depth %u", pam.depth);
        
    if (pam.maxval != 255)
        pm_error("PAM must have maxval 255.  This one has %lu", pam.maxval);

    writePng(&pam, stdout);

    pm_close(ifP);

    return 0;
}
