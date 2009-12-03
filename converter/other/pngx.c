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



