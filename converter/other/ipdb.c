/*
 *
 * Copyright (C) 1997 Eric A. Howe
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *   Authors:  Eric A. Howe (mu@trends.net)
 *             Bryan Henderson, 2010
 */
#define _DEFAULT_SOURCE /* New name for SVID & BSD source defines */
#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */
#define _BSD_SOURCE   /* Ensure strdup() is in <string.h> */
#include <assert.h>
#include <string.h>

#include "mallocvar.h"
#include "nstring.h"

#include "ipdb.h"



static unsigned int
imgPpb(IMAGE * const imgP) {
/*----------------------------------------------------------------------------
   Pixels per byte
-----------------------------------------------------------------------------*/
    return
        imgP->type == IMG_GRAY   ? 4 :
        imgP->type == IMG_GRAY16 ? 2 :
        8;
}



unsigned int
ipdb_imgPpb(IMAGE * const imgP) {
/*----------------------------------------------------------------------------
   Pixels per byte
-----------------------------------------------------------------------------*/
    return imgPpb(imgP);
}



size_t
ipdb_imgSize(IMAGE * const imgP) {
/*----------------------------------------------------------------------------
  Size (in bytes) of an image's data.
-----------------------------------------------------------------------------*/
    return (size_t)(imgP->width / imgPpb(imgP) * imgP->height);
}



/*
 * Return the start of row `r'.
 */
uint8_t *
ipdb_imgRow(IMAGE *      const imgP,
            unsigned int const row) {

    return &imgP->data[(row) * imgP->width / imgPpb(imgP)];
}



static const char * const errorDesc[] = {
    /* E_BADCOLORS      */
    "Invalid palette, only {0x00, 0x55, 0xAA, 0xFF} allowed.",

    /* E_NOTIMAGE       */
    "Not an image file.",

    /* E_IMAGETHERE     */
    "Image record already present, logic error.",

    /* E_IMAGENOTTHERE  */
    "Image record required before text record, logic error.",

    /* E_TEXTTHERE      */
    "Text record already present, logic error.",

    /* E_NOTRECHDR      */
    "Invalid record header encountered.",

    /* E_UNKNOWNRECHDR  */
    "Unknown record header.",

    /* E_TOOBIGG        */
    "Image too big, maximum size approx. 640*400 gray pixels.",

    /* E_TOOBIGM        */
    "Image too big, maximum size approx. 640*800 monochrome pixels.",
};



const char *
ipdb_err(int const e) {

    if (e < 0)
        return e >= E_LAST ? errorDesc[-e - 1] : "unknown error";
    else
        return strerror(e);
}



static void
rechdr_free(RECHDR * const recP) {

    if (recP) {
        free(recP->extra);
        free(recP);
    }
}



void
ipdb_imageFree(IMAGE * const imgP) {

    if (imgP) {
        rechdr_free(imgP->r);
        free(imgP->data);
        free(imgP);
    }
}



void
ipdb_textFree(TEXT * const textP) {

    if (textP) {
        rechdr_free(textP->r);
        if (textP->data)
            free(textP->data);
        free(textP);
    }
}



void
ipdb_pdbheadFree(PDBHEAD * const headP) {

    free(headP);
}



void
ipdb_free(IPDB * const pdbP) {

    if (pdbP->i)
        ipdb_imageFree(pdbP->i);

    if (pdbP->t)
        ipdb_textFree(pdbP->t);

    if (pdbP->p)
        ipdb_pdbheadFree(pdbP->p);

    free(pdbP);
}



PDBHEAD *
ipdb_pdbheadAlloc() {

    PDBHEAD * pdbHeadP;

    MALLOCVAR(pdbHeadP);

    if (pdbHeadP) {
        MEMSZERO(pdbHeadP);
    }
    return pdbHeadP;
}



static RECHDR *
rechdrCreate(int      const type,
             uint32_t const offset) {

    /*
     * We never produce the `extra' bytes (we only read them from a file)
     * so there is no point allocating them here.
     */

    RECHDR  * recHdrP;

    MALLOCVAR(recHdrP);

    if (recHdrP) {
        MEMSSET(recHdrP, 0);

        recHdrP->offset   = offset;
        recHdrP->rec_type = (uint8_t)(0xff & type);
        MEMSCPY(&recHdrP->unknown, IPDB_MYST);
    }
    return recHdrP;
}



/*
 * The offset will be patched up as needed elsewhere.
 */
#define IMGOFFSET   (PDBHEAD_SIZE + 8)



IMAGE *
ipdb_imageCreate(const char * const name,
                 int          const type,
                 int          const w,
                 int          const h) {

    bool failed;
    IMAGE * imgP;

    failed = false;

    MALLOCVAR(imgP);

    if (imgP) {
        MEMSZERO(imgP);

        STRSCPY(imgP->name, name == NULL ? "unnamed" : name);
        imgP->type     = type;
        imgP->x_anchor = 0xffff;
        imgP->y_anchor = 0xffff;
        imgP->width    = w;
        imgP->height   = h;

        imgP->r = rechdrCreate(IMG_REC, IMGOFFSET);

        if (imgP->r) {
            if (w != 0 && h != 0) {
                MALLOCARRAY(imgP->data, w * h);

                if (imgP->data != NULL) {
                  memset(imgP->data, 0, sizeof(*(imgP->data)) * w * h);
                } else
                    failed = true;
            }
            if (failed)
                rechdr_free(imgP->r);
        } else
            failed = true;

        if (failed)
            free(imgP);
    } else
        failed = true;

    return failed ? NULL : imgP;
}



TEXT *
ipdb_textAlloc(void) {

    TEXT * textP;
    bool failed;

    failed = false;
    /*
     * The offset will be patched up later on when we know what it
     * should be.
     */

    MALLOCVAR(textP);

    if (textP) {
        MEMSZERO(textP);

        textP->r = rechdrCreate(TEXT_REC, 0);

        if (textP->r == NULL)
            failed = true;

        if (failed)
            free(textP);
    } else
        failed = true;

    return failed ? NULL : textP;
}



IPDB *
ipdb_alloc(void) {

    IPDB * pdbP;
    bool failed;

    failed = false;

    MALLOCVAR(pdbP);

    if (pdbP) {
        MEMSZERO(pdbP);

        pdbP->p = ipdb_pdbheadAlloc();

        if (!pdbP->p)
            failed = true;

        if (failed)
            ipdb_free(pdbP);
    } else
        failed = true;

    return failed ? NULL : pdbP;
}



const char *
ipdb_typeName(uint8_t const type) {

    switch (type) {
    case IMG_GRAY16: return "16 Bit Grayscale"; break;
    case IMG_GRAY: return "Grayscale"; break;
    case IMG_MONO: return "Monochrome"; break;
    default: return "???";
    }
}



