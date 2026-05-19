#include "netpbm/nstring.h"

#include "jasper/jasper.h"
#include "jasper/jas_image.h"

#ifndef JAS_HAVE_PMJAS_IMAGE_DECODE

/* The Netpbm build has been modified to use original libjasper instead of the
   fork of it in Netpbm.  That means libjasper has 'jas_image_decode' instead
   of 'pmjas_image_decode' and Netpbm programs that use libjasper have to use
   the former, via this compatibility wrapper.
*/

void
pmjas_image_decode(jas_stream_t * const in,
                   int            const fmtArg,
                   const char *   const optstr,
                   jas_image_t ** const imagePP,
                   const char **  const errorP) {

    jas_image_t * const jasperP = jas_image_decode(in, fmtArg, optstr);

    if (jasperP) {
        *imagePP = jasperP;
        *errorP  = errorP;
    } else {
        pm_asprintf(errorP, "Failed.  Details may have been written to "
                    "Standard Error");
    }
}

#endif
