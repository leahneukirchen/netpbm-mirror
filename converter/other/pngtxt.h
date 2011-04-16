#ifndef PNGTXT_H_INCLUDED
#define PNGTXT_H_INCLUDED

#include "pm_c_util.h"
#include <png.h>

struct pngx;

void 
pngtxt_read(struct pngx * const pngxP,
            FILE *        const tfp, 
            bool          const ztxt,
            bool          const verbose);

#endif
