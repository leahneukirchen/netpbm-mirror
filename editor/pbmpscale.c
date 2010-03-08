/* pbmpscale.c - pixel scaling with jagged edge smoothing.
 * AJCD 13/8/90
 */

#include <stdio.h>
#include "pbm.h"
#include "mallocvar.h"

/* input bitmap size and storage */
int rows, columns, format ;
bit *inrow[3] ;

#define thisrow (1)

/* compass directions from west clockwise */
int xd_pscale[] = { -1, -1,  0,  1, 1, 1, 0, -1 } ;
int yd_pscale[] = {  0, -1, -1, -1, 0, 1, 1,  1 } ;

/* starting positions for corners */
#define NE(f) ((f) & 3)
#define SE(f) (((f) >> 2) & 3)
#define SW(f) (((f) >> 4) & 3)
#define NW(f) (((f) >> 6) & 3)

#define MAX(x,y) ((x) > (y) ? (x) : (y))

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


static int corner(uint16_t const pat) {
/* list of corner patterns; bit 7 is current color, bits 0-6 are squares
 * around (excluding square behind), going clockwise.
 * The high byte of the patterns is a mask, which determines which bits are
 * not ignored.
 */

uint16_t const patterns[] 
      = { 0x0000,   0xd555,           /* no corner */
  	  0x0001,   0xffc1, 0xd514,   /* normal corner */
	  0x0002,   0xd554, 0xd515, 0xbea2, 0xdfc0, 0xfd81, 0xfd80, 0xdf80,
                                      /* reduced corners */
 	  0x0003,   0xbfa1, 0xfec2 }; /* reduced if > 1 */

/* search for corner patterns, return type of corner found:
 *  0 = no corner, 
 *  1 = normal corner,
 *  2 = reduced corner,
 *  3 = reduced if cutoff > 1
 */

   int i, r=0;

   for (i = 0; i < sizeof(patterns)/sizeof(uint16_t); i++)
      if (patterns[i] < 0x100)
         r = patterns[i];
      else if ((pat & (patterns[i] >> 8)) ==
               (patterns[i] & (patterns[i] >> 8)))
         return r;
   return 0;
}

/* get a new row */
static void nextrow_pscale(FILE * const ifd, int const row) {
   bit *shuffle = inrow[0] ;
   inrow[0] = inrow[1];
   inrow[1] = inrow[2];
   inrow[2] = shuffle ;
   if (row < rows) {
      if (shuffle == NULL)
         inrow[2] = shuffle = pbm_allocrow(columns);
      pbm_readpbmrow(ifd, inrow[2], columns, format) ;
   } else inrow[2] = NULL; /* discard storage */
}

int
main(int argc, char ** argv) {
    FILE * ifP;
    bit * outrow;
    unsigned int row;
    int scale, outcols, outrows, cutoff, ucutoff ;
    unsigned char *flags;

    pbm_init( &argc, argv );

    if (argc < 2)
        pm_usage("scale [pbmfile]");

    scale = atoi(argv[1]);
    if (scale < 1)
        pm_error("Scale argument must be at least one.  You specified '%s'",
                 argv[1]);

    if (argc == 3)
        ifP = pm_openr(argv[2]);
    else
        ifP = stdin ;

    inrow[0] = inrow[1] = inrow[2] = NULL;
    pbm_readpbminit(ifP, &columns, &rows, &format) ;

    validateComputableDimensions(columns, rows, scale); 
    outcols= columns * scale;     outrows= rows * scale; 

    outrow = pbm_allocrow(outcols) ;
    MALLOCARRAY(flags, columns);
    if (flags == NULL) 
        pm_error("out of memory") ;

    pbm_writepbminit(stdout, outcols, outrows, 0) ;

    cutoff = scale / 2;
    ucutoff = scale - 1 - cutoff;
    nextrow_pscale(ifP, 0);
    for (row = 0; row < rows; ++row) {
        unsigned int col;
        unsigned int i;
        nextrow_pscale(ifP, row+1);
        for (col = 0; col < columns; ++col) {
	  unsigned int i, k;
	  uint16_t thispoint = (inrow[thisrow][col] != PBM_WHITE) << 7;
          unsigned char vec = 0;

	  for (k = 0; k < 8; ++k) {
	    int x = col + xd_pscale[k] ;
	    int y = thisrow + yd_pscale[k] ;
	    vec <<= 1;
	    if (x >=0 && x < columns && inrow[y])
	      vec |= (inrow[y][x] != PBM_WHITE) ;
	  }

          vec = (vec >>1 | vec <<7);

	  flags[col] = 0 ;
	  for (i = 0; i != 8; i += 2) {
	    flags[col] |= corner(thispoint | (vec & 0x7f) )<<i ;
	    vec = ( vec >>6 | vec <<2);
	  }

        }

        for (i = 0; i < scale; i++) {

            int const zone = (i > ucutoff) - (i < cutoff) ;
            int const cut = (zone < 0) ? (cutoff - i) :
                (zone > 0) ? (i - ucutoff) : 0 ;
            unsigned int outcol=0;

            for (col = 0; col < columns; ++col) {
                int const pix = inrow[thisrow][col] ;
                int const flag = flags[col] ;
                int cutl, cutr;

                switch (zone) {
                case -1:
                    switch (NW(flag)) {
                    case 0: cutl = 0; break;
                    case 1: cutl = cut; break;
                    case 2: cutl = cut ? cut-1 : 0; break;
                    case 3: cutl = (cut && cutoff > 1) ? cut-1 : cut; break;
                    default: cutl = 0;  /* Should never reach here */
                    }
                    switch (NE(flag)) {
                    case 0: cutr = 0; break;
                    case 1: cutr = cut; break;
                    case 2: cutr = cut ? cut-1 : 0; break;
                    case 3: cutr = (cut && cutoff > 1) ? cut-1 : cut; break;
                    default: cutr = 0;  /* Should never reach here */
                    }
                    break;
                case 0:
                    cutl = cutr = 0;
                    break ;
                case 1:
                    switch (SW(flag)) {
                    case 0: cutl = 0; break;
                    case 1: cutl = cut; break;
                    case 2: cutl = cut ? cut-1 : 0; break;
                    case 3: cutl = (cut && cutoff > 1) ? cut-1 : cut; break;
                    default: cutl = 0;  /* should never reach here */
                    }
                    switch (SE(flag)) {
                    case 0: cutr = 0; break;
                    case 1: cutr = cut; break;
                    case 2: cutr = cut ? cut-1 : 0; break;
                    case 3: cutr = (cut && cutoff > 1) ? cut-1 : cut; break;
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
            pbm_writepbmrow(stdout, outrow, scale*columns, 0) ;
        }
    }
    pm_close(ifP);
    return 0;
}
