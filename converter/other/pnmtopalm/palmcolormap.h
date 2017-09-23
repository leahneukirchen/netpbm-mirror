#ifndef PALMCOLORMAP_H_INCLUDED
#define PALMCOLORMAP_H_INCLUDED

#include <stdio.h>
#include "ppm.h"
#include "palm.h"

Colormap *
palmcolor_build_custom_8bit_colormap(unsigned int const rows,
                                     unsigned int const cols,
                                     pixel **     const pixels);

Colormap *
palmcolor_build_default_8bit_colormap(void);

Colormap *
palmcolor_read_colormap (FILE * const ifP);

#endif
