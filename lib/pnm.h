/* pnm.h - header file for libpnm portable anymap library
*/

#ifndef _PNM_H_
#define _PNM_H_

#include <netpbm/pm.h>
#include <netpbm/pbm.h>
#include <netpbm/pgm.h>
#include <netpbm/ppm.h>

#ifdef __cplusplus
extern "C" {
#endif
#if 0
} /* to fake out automatic code indenters */
#endif


typedef pixel xel;
    /* Xels come in three types: PBM, PGM, and PPM; the user of an Xel has to
       know which as a matter of context (and like a pixel, the user also has
       to interpret an xel in the context of a certain maxval).  Though the
       structure is identical to 'pixel', the values are the same only for PPM
       xels.  For a PGM xel, the 'r' and 'g' components of the 'xel' structure
       are zero and the 'b' component is the gray level.  For a PBM xel, the
       'r' and 'g' components are zero and the 'b' component is 0 for black
       or maxval for white.
    */

typedef pixval xelval;

#define PNM_OVERALLMAXVAL PPM_OVERALLMAXVAL
#define PNM_MAXMAXVAL PPM_MAXMAXVAL
#define pnm_unnormalize ppm_unnormalize
#define PNM_GET1(x) PPM_GETB(x)
#define PNM_GETR(x) PPM_GETR(x)
#define PNM_GETG(x) PPM_GETG(x)
#define PNM_GETB(x) PPM_GETB(x)
#define PNM_ASSIGN1(x,v) PPM_ASSIGN(x,0,0,v)
#define PNM_ASSIGN(x,r,g,b) PPM_ASSIGN(x,r,g,b)
#define PNM_EQUAL(x,y) PPM_EQUAL(x,y)
#define PNM_FORMAT_TYPE(f) PPM_FORMAT_TYPE(f)
#define PNM_DEPTH(newp,p,oldmaxval,newmaxval) \
    PPM_DEPTH(newp,p,oldmaxval,newmaxval)

/* Declarations of routines. */

void
pnm_init(int *   const argcP,
         char ** const argv);

void
pnm_nextimage(FILE * const file, int * const eofP);

xel *
pnm_allocrow(unsigned int const cols);

#define pnm_freerow(xelrow) pm_freerow(xelrow)

#define pnm_allocarray( cols, rows ) \
  ((xel**) pm_allocarray( cols, rows, sizeof(xel) ))
#define pnm_freearray( xels, rows ) pm_freearray( (char**) xels, rows )


void
pnm_readpnminit(FILE *   const fileP,
                int *    const colsP,
                int *    const rowsP,
                xelval * const maxvalP,
                int *    const formatP);

void
pnm_readpnmrow(FILE * const fileP,
               xel *  const xelrow,
               int    const cols,
               xelval const maxval,
               int    const format);

xel **
pnm_readpnm(FILE *   const fileP,
            int *    const colsP,
            int *    const rowsP,
            xelval * const maxvalP,
            int *    const formatP);

void
pnm_check(FILE *               const fileP,
          enum pm_check_type   const check_type,
          int                  const format,
          int                  const cols,
          int                  const rows,
          int                  const maxval,
          enum pm_check_code * const retvalP);


void
pnm_writepnminit(FILE * const fileP,
                 int    const cols,
                 int    const rows,
                 xelval const maxval,
                 int    const format,
                 int    const forceplain);

void
pnm_writepnmrow(FILE *      const fileP,
                const xel * const xelrow,
                int         const cols,
                xelval      const maxval,
                int         const format,
                int         const forceplain);

void
pnm_writepnm(FILE * const fileP,
             xel ** const xels,
             int    const cols,
             int    const rows,
             xelval const maxval,
             int    const format,
             int    const forceplain);

xel
pnm_backgroundxel(xel** xels, int cols, int rows, xelval maxval, int format);

xel
pnm_backgroundxelrow(xel* xelrow, int cols, xelval maxval, int format);

xel
pnm_whitexel(xelval maxval, int format);

xel
pnm_blackxel(xelval maxval, int format);

void
pnm_invertxel(xel *  const x,
              xelval const maxval,
              int    const format);

const char *
pnm_formattypenm(int const format);

void
pnm_promoteformat(xel** xels, int cols, int rows, xelval maxval, int format,
                  xelval newmaxval, int newformat);
void
pnm_promoteformatrow(xel* xelrow, int cols, xelval maxval, int format,
                     xelval newmaxval, int newformat);

pixel
pnm_xeltopixel(xel const inputxel,
               int const format);

xel
pnm_pixeltoxel(pixel const inputPixel);

xel
pnm_graytoxel(gray const inputGray);

xel
pnm_bittoxel(bit    const inputBit,
             xelval const maxval);

xel
pnm_parsecolorxel(const char * const colorName,
                  xelval       const maxval,
                  int          const format);

#ifdef __cplusplus
}
#endif


#endif /*_PNM_H_*/
