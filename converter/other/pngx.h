#ifndef PNGX_H_INCLUDED
#define PNGX_H_INCLUDED

#include <png.h>
#include "pm_c_util.h"

/* pngx is designed to be an extension of the PNG library to make using
   the PNG library easier and cleaner.
*/

struct pngx_chroma {
    float wx;
    float wy;
    float rx;
    float ry;
    float gx;
    float gy;
    float bx;
    float by;
};

struct pngx_phys {
    int x;
    int y;
    int unit;
};

struct pngx_trans {
    png_bytep trans;
    int numTrans;
    png_color_16 * transColorP;
};

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

png_byte
pngx_colorType(struct pngx * const pngxP);

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
pngx_setGama(struct pngx * const pngxP,
             float         const fileGamma);

void
pngx_setChrm(struct pngx *      const pngxP,
             struct pngx_chroma const chroma);

void
pngx_setPhys(struct pngx *    const pngxP,
             struct pngx_phys const phys);

void
pngx_setTime(struct pngx * const pngxP,
             png_time      const time);

void
pngx_setSbit(struct pngx * const pngxP,
             png_color_8   const sbit);

void
pngx_setInterlaceHandling(struct pngx * const pngxP);

void
pngx_setPlte(struct pngx * const pngxP,
             png_color *   const palette,
             unsigned int  const paletteSize);

void
pngx_setTrnsPalette(struct pngx *    const pngxP,
                    const png_byte * const transPalette,
                    unsigned int     const paletteSize);

void
pngx_setTrnsValue(struct pngx * const pngxP,
                  png_color_16  const transColorArg);

void
pngx_setHist(struct pngx * const pngxP,
             png_uint_16 * const histogram);

struct pngx_trans
pngx_getTrns(struct pngx * const pngxP);

void
pngx_setBkgdPalette(struct pngx * const pngxP,
                    unsigned int  const backgroundIndex);

void
pngx_setBkgdRgb(struct pngx * const pngxP,
                png_color_16  const backgroundArg);

void
pngx_writeInfo(struct pngx * const pngxP);

void
pngx_writeEnd(struct pngx * const pngxP);

#endif
