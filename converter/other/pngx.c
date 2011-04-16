#include <png.h>
#include "pm_c_util.h"
#include "mallocvar.h"
#include "pm.h"
#include "pngx.h"


static void
errorHandler(png_structp     const png_ptr,
             png_const_charp const msg) {

    jmp_buf * jmpbufP;

    /* this function, aside from the extra step of retrieving the "error
       pointer" (below) and the fact that it exists within the application
       rather than within libpng, is essentially identical to libpng's
       default error handler.  The second point is critical:  since both
       setjmp() and longjmp() are called from the same code, they are
       guaranteed to have compatible notions of how big a jmp_buf is,
       regardless of whether _BSD_SOURCE or anything else has (or has not)
       been defined.
    */

    pm_message("fatal libpng error: %s", msg);

    jmpbufP = png_get_error_ptr(png_ptr);

    if (!jmpbufP) {
        /* we are completely hosed now */
        pm_error("EXTREMELY fatal error: jmpbuf unrecoverable; terminating.");
    }

    longjmp(*jmpbufP, 1);
}



void
pngx_create(struct pngx ** const pngxPP,
            pngx_rw        const rw,
            jmp_buf *      const jmpbufP) {

    struct pngx * pngxP;

    MALLOCVAR(pngxP);

    if (!pngxP)
        pm_error("Failed to allocate memory for PNG object");
    else {
        switch(rw) {
        case PNGX_READ:
            pngxP->png_ptr = png_create_read_struct(
                PNG_LIBPNG_VER_STRING,
                jmpbufP, errorHandler, NULL);
            break;
        case PNGX_WRITE:
            pngxP->png_ptr = png_create_write_struct(
                PNG_LIBPNG_VER_STRING,
                jmpbufP, errorHandler, NULL);
            break;
        }
        if (!pngxP->png_ptr)
            pm_error("cannot allocate main libpng structure (png_ptr)");
        else {
            pngxP->info_ptr = png_create_info_struct(pngxP->png_ptr);

            if (!pngxP->info_ptr)
                pm_error("cannot allocate libpng info structure (info_ptr)");
            else
                *pngxPP = pngxP;
        }
        pngxP->rw = rw;
    }
}



void
pngx_destroy(struct pngx * const pngxP) {

    switch(pngxP->rw) {
    case PNGX_READ:
        png_destroy_read_struct(&pngxP->png_ptr, &pngxP->info_ptr, NULL);
        break;
    case PNGX_WRITE:
        png_destroy_write_struct(&pngxP->png_ptr, &pngxP->info_ptr);
        break;
    }

    free(pngxP);
}



bool
pngx_chunkIsPresent(struct pngx * const pngxP,
                    uint32_t      const chunkType) {

    return png_get_valid(pngxP->png_ptr, pngxP->info_ptr, chunkType);
}



void
pngx_setText(struct pngx * const pngxP,
             png_textp     const textP,
             unsigned int  const count) {

    png_set_text(pngxP->png_ptr, pngxP->info_ptr, textP, count);
}



void
pngx_setIhdr(struct pngx * const pngxP,
             unsigned int  const width,
             unsigned int  const height,
             unsigned int  const bitDepth,
             int           const colorType,
             int           const interlaceMethod,
             int           const compressionMethod,
             int           const filterMethod) {

    png_set_IHDR(pngxP->png_ptr, pngxP->info_ptr, width, height,
                 bitDepth, colorType, interlaceMethod, compressionMethod,
                 filterMethod);
}



void
pngx_writeInfo(struct pngx * const pngxP) {

    png_write_info(pngxP->png_ptr, pngxP->info_ptr);
}



void
pngx_writeEnd(struct pngx * const pngxP) {

    png_write_end(pngxP->png_ptr, pngxP->info_ptr);
}



png_byte
pngx_colorType(struct pngx * const pngxP) {

    return png_get_color_type(pngxP->png_ptr, pngxP->info_ptr);
}



void
pngx_setGama(struct pngx * const pngxP,
             float         const fileGamma) {

    png_set_gAMA(pngxP->png_ptr, pngxP->info_ptr, fileGamma);
}



void
pngx_setChrm(struct pngx *      const pngxP,
             struct pngx_chroma const chroma) {

    png_set_cHRM(pngxP->png_ptr, pngxP->info_ptr, 
                 chroma.wx, chroma.wy,
                 chroma.rx, chroma.ry,
                 chroma.gx, chroma.gy,
                 chroma.bx, chroma.by);
}



void
pngx_setPhys(struct pngx *    const pngxP,
             struct pngx_phys const phys) {

    png_set_pHYs(pngxP->png_ptr, pngxP->info_ptr, 
                 phys.x, phys.y, phys.unit);
}



void
pngx_setTime(struct pngx * const pngxP,
             png_time      const timeArg) {

    png_time time;

    time = timeArg;

    png_set_tIME(pngxP->png_ptr, pngxP->info_ptr, &time);
}



void
pngx_setSbit(struct pngx * const pngxP,
             png_color_8   const sbitArg) {

    png_color_8 sbit;

    sbit = sbitArg;

    png_set_sBIT(pngxP->png_ptr, pngxP->info_ptr, &sbit);
}



void
pngx_setInterlaceHandling(struct pngx * const pngxP) {

    png_set_interlace_handling(pngxP->png_ptr);
}



void
pngx_setPlte(struct pngx * const pngxP,
             png_color *   const palette,
             unsigned int  const paletteSize) {

    png_set_PLTE(pngxP->png_ptr, pngxP->info_ptr, palette, paletteSize);
}



void
pngx_setTrnsPalette(struct pngx *    const pngxP,
                    const png_byte * const transPalette,
                    unsigned int     const paletteSize) {

    png_set_tRNS(pngxP->png_ptr, pngxP->info_ptr,
                 (png_byte *)transPalette, paletteSize, NULL);
}



void
pngx_setTrnsValue(struct pngx * const pngxP,
                  png_color_16  const transColorArg) {

    png_color_16 transColor;

    transColor = transColorArg;

    png_set_tRNS(pngxP->png_ptr, pngxP->info_ptr,
                 NULL, 0, &transColor);
}



void
pngx_setHist(struct pngx * const pngxP,
             png_uint_16 * const histogram) {

    png_set_hIST(pngxP->png_ptr, pngxP->info_ptr, histogram);
}



struct pngx_trans
pngx_getTrns(struct pngx * const pngxP) {

    struct pngx_trans retval;

    png_get_tRNS(pngxP->png_ptr, pngxP->info_ptr,
                 &retval.trans, &retval.numTrans, &retval.transColorP);

    return retval;
}



void
pngx_setBkgdPalette(struct pngx * const pngxP,
                    unsigned int  const backgroundIndex) {

    png_color_16 background;

    background.index = backgroundIndex;

    png_set_bKGD(pngxP->png_ptr, pngxP->info_ptr, &background);
}



void
pngx_setBkgdRgb(struct pngx * const pngxP,
                png_color_16  const backgroundArg) {

    png_color_16 background;

    background = backgroundArg;

    png_set_bKGD(pngxP->png_ptr, pngxP->info_ptr, &background);
}



