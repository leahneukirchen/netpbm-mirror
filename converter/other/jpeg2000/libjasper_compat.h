/* Here's some stuff to create backward compatibility with older Jasper
   libraries.  Unfortunately, new versions of the Jasper library are not
   backward compatible with old applications.

   This also makes the programs compatible with both distributed Jasper
   libraries and the Netpbm fork of Jasper distributed with Netpbm.
*/
/* The color space thing got more complex between Version 1.600 and
   1.701.  For example, it now allows for multiple kinds of RGB, whereas
   in 1.600 RGB meant SRGB.  As part of that change, names changed
   from "colorspace" to "clrspc".
*/
#include "jasper/jasper.h"
#include "jasper/jas_image.h"

#if defined(jas_image_setcolorspace)
/* Old style color space */
#define jas_image_setclrspc jas_image_setcolorspace
#define JAS_CLRSPC_GENRGB JAS_IMAGE_CS_RGB
#define JAS_CLRSPC_GENGRAY JAS_IMAGE_CS_GRAY
#define JAS_CLRSPC_UNKNOWN JAS_IMAGE_CS_UNKNOWN

#define jas_clrspc_fam(clrspc) (clrspc)
#define jas_image_clrspc jas_image_colorspace

#define JAS_CLRSPC_FAM_RGB JAS_IMAGE_CS_RGB
#define JAS_CLRSPC_FAM_GRAY JAS_IMAGE_CS_GRAY
#define JAS_CLRSPC_FAM_UNKNOWN JAS_IMAGE_CS_UNKNOWN

#endif


#ifndef JAS_HAVE_PMJAS_IMAGE_DECODE

/* The Netpbm version of jas_image_decode (pmjas_image_decode) returns a
   description of the problem when it fails and does not molest Standard
   Error.  Real libjasper just indicates that it failed, after writing some
   explanation (but not as much as the Netpbm version returns) to Standard
   Error.
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
