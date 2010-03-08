/* pbmpscale.c - pixel scaling with jagged edge smoothing.
 * AJCD 13/8/90
 */

#include <stdio.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "pbm.h"

/* input bitmap size and storage */
static int rows, cols, format;
static bit * inrow[3];

#define THISROW (1)

/* compass directions from west clockwise */
static int const xd_pscale[] = { -1, -1,  0,  1, 1, 1, 0, -1 };
static int const yd_pscale[] = {  0, -1, -1, -1, 0, 1, 1,  1 };

/* starting positions for corners */
#define NE(f) ((f) & 3)
#define SE(f) (((f) >> 2) & 3)
#define SW(f) (((f) >> 4) & 3)
#define NW(f) (((f) >> 6) & 3)



static void
validateComputableDimensions(unsigned int const width,
                             unsigned int const height,
                             unsigned int const scaleFactor) {
/*----------------------------------------------------------------------------
   Make sure that multiplication for output image width and height do not
   overflow.
   See validateComputetableSize() in libpam.c
   and pbm_readpbminitrest() in libpbm2.c
-----------------------------------------------------------------------------*/
    unsigned int const maxWidthHeight = INT_MAX - 2;
    unsigned int const maxScaleFactor = maxWidthHeight / MAX(height, width);

    if (scaleFactor > maxScaleFactor)
       pm_error("Scale factor '%u' too large.  "
                "The maximum for this %u x %u input image is %u.",
                scaleFactor, width, height, maxScaleFactor);
}



static int
corner(uint16_t const pat) {
/* list of corner patterns; bit 7 is current color, bits 0-6 are squares
 * around (excluding square behind), going clockwise.
 * The high byte of the patterns is a mask, which determines which bits are
 * not ignored.
 */

    uint16_t const patterns[] = {
        0x0000,   0xd555,           /* no corner */
        0x0001,   0xffc1, 0xd514,   /* normal corner */
        0x0002,   0xd554, 0xd515, 0xbea2, 0xdfc0, 0xfd81, 0xfd80, 0xdf80,
            /* reduced corners */
        0x0003,   0xbfa1, 0xfec2    /* reduced if > 1 */
    };

    /* search for corner patterns, return type of corner found:
     *  0 = no corner, 
     *  1 = normal corner,
     *  2 = reduced corner,
     *  3 = reduced if cutoff > 1
     */

    unsigned int i;
    unsigned int r;

    r = 0;  /* initial value */

    for (i = 0; i < sizeof(patterns)/sizeof(uint16_t); ++i) {
        if (patterns[i] < 0x100)
            r = patterns[i];
        else if ((pat & (patterns[i] >> 8)) ==
                 (patterns[i] & (patterns[i] >> 8)))
            return r;
    }
    return 0;
}



static void
nextrow_pscale(FILE * const ifP,
               int    const row) {
/*----------------------------------------------------------------------------
   Get a new row.
-----------------------------------------------------------------------------*/
    bit * shuffle;

    shuffle = inrow[0];
    inrow[0] = inrow[1];
    inrow[1] = inrow[2];
    inrow[2] = shuffle ;

    if (row < rows) {
        if (shuffle == NULL)
            inrow[2] = shuffle = pbm_allocrow(cols);
        pbm_readpbmrow(ifP, inrow[2], cols, format);
    } else
        inrow[2] = NULL; /* discard storage */
}



static void
setFlags(unsigned char * flags) {
             
    unsigned int col;

    for (col = 0; col < cols; ++col) {
        uint16_t const thispoint = (inrow[THISROW][col] != PBM_WHITE) << 7;

        unsigned int i, k;
        unsigned char vec;
            
        vec = 0;  /* initial value */

        for (k = 0; k < 8; ++k) {
            int const x = col + xd_pscale[k];
            int const y = THISROW + yd_pscale[k];
            vec <<= 1;
            if (x >= 0 && x < cols && inrow[y])
                vec |= (inrow[y][x] != PBM_WHITE) ;
        }
            
        vec = (vec >> 1 | vec << 7);
            
        flags[col] = 0 ;
        for (i = 0; i != 8; i += 2) {
            flags[col] |= corner(thispoint | (vec & 0x7f) ) << i ;
            vec = (vec >> 6 | vec << 2);
        }
    }
}



int
main(int argc, char ** argv) {

    FILE * ifP;
    bit * outrow;
    unsigned int row;
    int scale;
    unsigned int outcols;
    unsigned int outrows;
    int cutoff;
    int ucutoff;
    unsigned char * flags;

    pbm_init(&argc, argv);

    if (argc-1 < 1)
        pm_error("You must specify the scale factor as an argument");

    scale = atoi(argv[1]);
    if (scale < 1)
        pm_error("Scale argument must be at least one.  You specified '%s'",
                 argv[1]);

    if (argc-1 == 2)
        ifP = pm_openr(argv[2]);
    else
        ifP = stdin ;

    inrow[0] = inrow[1] = inrow[2] = NULL;

    pbm_readpbminit(ifP, &cols, &rows, &format) ;

    validateComputableDimensions(cols, rows, scale); 

    outcols = cols * scale;
    outrows = rows * scale; 

    outrow = pbm_allocrow(outcols);
    MALLOCARRAY(flags, cols);
    if (flags == NULL) 
        pm_error("out of memory") ;

    pbm_writepbminit(stdout, outcols, outrows, 0) ;

    cutoff = scale / 2;
    ucutoff = scale - 1 - cutoff;
    nextrow_pscale(ifP, 0);
    for (row = 0; row < rows; ++row) {
        unsigned int i;
        nextrow_pscale(ifP, row + 1);

        setFlags(flags);

        for (i = 0; i < scale; ++i) {
            int const zone = (i > ucutoff) - (i < cutoff) ;
            int const cut = (zone < 0) ? (cutoff - i) :
                (zone > 0) ? (i - ucutoff) : 0 ;

            unsigned int col;
            unsigned int outcol;

            outcol = 0;  /* initial value */

            for (col = 0; col < cols; ++col) {
                int const pix = inrow[THISROW][col] ;
                int const flag = flags[col] ;
                int cutl, cutr;

                switch (zone) {
                case -1:
                    switch (NW(flag)) {
                    case 0:  cutl = 0; break;
                    case 1:  cutl = cut; break;
                    case 2:  cutl = cut ? cut-1 : 0; break;
                    case 3:  cutl = (cut && cutoff > 1) ? cut-1 : cut; break;
                    default: cutl = 0;  /* Should never reach here */
                    }
                    switch (NE(flag)) {
                    case 0:  cutr = 0; break;
                    case 1:  cutr = cut; break;
                    case 2:  cutr = cut ? cut-1 : 0; break;
                    case 3:  cutr = (cut && cutoff > 1) ? cut-1 : cut; break;
                    default: cutr = 0;  /* Should never reach here */
                    }
                    break;
                case 0:
                    cutl = cutr = 0;
                    break ;
                case 1:
                    switch (SW(flag)) {
                    case 0:  cutl = 0; break;
                    case 1:  cutl = cut; break;
                    case 2:  cutl = cut ? cut-1 : 0; break;
                    case 3:  cutl = (cut && cutoff > 1) ? cut-1 : cut; break;
                    default: cutl = 0;  /* should never reach here */
                    }
                    switch (SE(flag)) {
                    case 0:  cutr = 0; break;
                    case 1:  cutr = cut; break;
                    case 2:  cutr = cut ? cut-1 : 0; break;
                    case 3:  cutr = (cut && cutoff > 1) ? cut-1 : cut; break;
                    default: cutr = 0;  /* should never reach here */
                    }
                    break;
                default: cutl = 0; cutr = 0;  /* Should never reach here */
                }

                {
                    unsigned int k;
                    for (k = 0; k < cutl; ++k) /* left part */
                        outrow[outcol++] = !pix ;
                    for (k = 0; k < scale-cutl-cutr; ++k)  /* center part */
                        outrow[outcol++] = pix ;
                    for (k = 0; k < cutr; ++k) /* right part */
                        outrow[outcol++] = !pix ;
                }
            }
            pbm_writepbmrow(stdout, outrow, scale * cols, 0) ;
        }
    }
    pm_close(ifP);
    return 0;
}
