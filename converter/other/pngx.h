#ifndef PNGX_H_INCLUDED
#define PNGX_H_INCLUDED

/* pngx is designed to be an extension of the PNG library to make using
   the PNG library easier and cleaner.
*/

typedef enum {PNGX_READ, PNGX_WRITE} pngx_rw;

struct pngx {
    png_structp png_ptr;
    png_infop   info_ptr;
    pngx_rw     rw;
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

#endif
