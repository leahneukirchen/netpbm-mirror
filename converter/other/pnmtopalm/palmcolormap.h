#ifndef PALMCOLORMAP_H_INCLUDED
#define PALMCOLORMAP_H_INCLUDED

#include <stdio.h>
#include "ppm.h"
#include "palm.h"

ColormapEntry
palmcolor_mapEntryColorFmPixel(pixel    const color,
                               pixval const maxval,
                               pixval const newMaxval);

Colormap *
palmcolor_build_custom_8bit_colormap(pixel **     const pixels,
                                     unsigned int const rows,
                                     unsigned int const cols,
                                     pixval       const maxval);

Colormap *
palmcolor_build_default_8bit_colormap(void);

Colormap *
palmcolor_read_colormap (FILE * const ifP);

#endif
