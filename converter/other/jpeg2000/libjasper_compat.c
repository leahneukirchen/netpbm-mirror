#include "netpbm/nstring.h"

#include "jasper/jasper.h"
#include "jasper/jas_image.h"

#ifndef JAS_HAVE_PMJAS_IMAGE_DECODE

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
