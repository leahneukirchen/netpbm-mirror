#ifndef PNGX_H_INCLUDED
#define PNGX_H_INCLUDED

#include <png.h>
#include "pm_c_util.h"

/* pngx is designed to be an extension of the PNG library to make using
   the PNG library easier and cleaner.
*/

typedef enum {PNGX_READ, PNGX_WRITE} pngx_rw;

struct pngx {
    png_structp png_ptr;
    png_infop   info_ptr;
    pngx_rw     rw;
    png_uint_16 maxval;
};

void
pngx_create(struct pngx ** const pngxPP,
            pngx_rw        const rw,
            jmp_buf *      const jmpbufP);

void
pngx_destroy(struct pngx * const pngxP);

bool
pngx_chunkIsPresent(struct pngx * const pngxP,
                    uint32_t      const chunkType);

void
pngx_setText(struct pngx * const pngxP,
             png_textp     const textP,
             unsigned int  const count);

void
pngx_setIhdr(struct pngx * const pngxP,
             unsigned int  const width,
             unsigned int  const height,
             unsigned int  const bitDepth,
             int           const colorType,
             int           const interlaceMethod,
             int           const compressionMethod,
             int           const filterMethod);

void
pngx_writeInfo(struct pngx * const pngxP);

void
pngx_writeEnd(struct pngx * const pngxP);

#endif
