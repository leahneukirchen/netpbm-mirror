/* gen_palm_colormap.c - generate a ppm file containing the default Palm colormap
 *
 * Based on an earlier version by Bill Janssen  <bill@janssen.org>
 */

#include "netpbm/ppm.h"
#include "netpbm/pm_c_util.h"

#include "palm.h"

int
main(int     argc,
     char ** argv) {

    Colormap defaultMap;
    unsigned int i;
    pixel pix;
    
    defaultMap = palmcolor_build_default_8bit_colormap();
    qsort (defaultMap->color_entries, defaultMap->ncolors,
           sizeof(Color_s), palmcolor_compare_indices);

    ppm_writeppminit(stdout, 256, 1, 255, TRUE);

    for (i = 0; i < defaultMap->ncolors; ++i) {
        Color_s const current = defaultMap->color_entries[i];

        PPM_ASSIGN(pix,
                   (current >> 16) & 0xff,
                   (current >>  8) & 0xff,
                   (current >>  0) & 0xff);

        ppm_writeppmrow(stdout, &pix, 1, 255, TRUE);
    }

    /* palmcolor_build_default_8bit_colormap() builds a map of the 231 default
     * palm colors and 1 extra black pixel. Add another 24 extra black pixels
     * as per spec. */
    PPM_ASSIGN(pix, 0, 0, 0);
    for (i = 0; i < 256 - defaultMap->ncolors; ++i) {
        ppm_writeppmrow(stdout, &pix, 1, 255, TRUE);
    }

    return 0;
}

