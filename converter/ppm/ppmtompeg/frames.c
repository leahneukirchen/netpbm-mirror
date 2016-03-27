#include "mallocvar.h"
#include "fsize.h"

#include "frames.h"


Block **dct=NULL, **dctr=NULL, **dctb=NULL;
dct_data_type   **dct_data; /* used in p/bframe.c */


/*===========================================================================*
 *
 * AllocDctBlocks
 *
 *  allocate memory for dct blocks
 *
 * RETURNS: nothing
 *
 * SIDE EFFECTS:    creates dct, dctr, dctb
 *
 *===========================================================================*/
void
AllocDctBlocks(void) {

    int dctx, dcty;
    int i;

    dctx = Fsize_x / DCTSIZE;
    dcty = Fsize_y / DCTSIZE;

    MALLOCARRAY(dct, dcty);
    ERRCHK(dct, "malloc");
    for (i = 0; i < dcty; ++i) {
        dct[i] = (Block *) malloc(sizeof(Block) * dctx);
        ERRCHK(dct[i], "malloc");
    }

    MALLOCARRAY(dct_data, dcty);
    ERRCHK(dct_data, "malloc");
    for (i = 0; i < dcty; ++i) {
        MALLOCARRAY(dct_data[i], dctx);
        ERRCHK(dct[i], "malloc");
    }

    MALLOCARRAY(dctr, dcty/2);
    ERRCHK(dctr, "malloc");
    MALLOCARRAY(dctb, dcty/2);
    ERRCHK(dctb, "malloc");
    for (i = 0; i < dcty/2; ++i) {
        MALLOCARRAY(dctr[i], dctx/2);
        ERRCHK(dctr[i], "malloc");
        MALLOCARRAY(dctb[i], dctx/2);
        ERRCHK(dctb[i], "malloc");
    }
}



