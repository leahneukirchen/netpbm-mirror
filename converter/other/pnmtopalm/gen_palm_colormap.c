/* gen_palm_colormap.c - generate a ppm file containing the default Palm colormap
 *
 * Bill Janssen  <bill@janssen.org>
 */

#include "pnm.h"

#include "palm.h"

int
main(int     argc,
     char ** argv) {

    Colormap const defaultMap = palmcolor_build_default_8bit_colormap();

    unsigned int i;
    
    printf("P3\n%d 1\n255\n", defaultMap->ncolors);

    for (i = 0; i < defaultMap->ncolors; ++i) {
        Color_s const current = defaultMap->color_entries[i];

        printf("%u %u %u\n",
               (unsigned char)(current >> 16),
               (unsigned char)(current >>  8),
               (unsigned char)(current >>  0));
    }

    return 0;
}

