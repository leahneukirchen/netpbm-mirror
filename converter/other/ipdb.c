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
#include <time.h>
#include <string.h>

#include "mallocvar.h"
#include "nstring.h"
#include "ipdb.h"

typedef uint32_t pilot_time_t;

/*
 * Pixel setting macros.
 */
#define setg16pixel(b,v,o)  ((b) |= ((v) << (4 - 4*(o))))
#define getg16pixel(b,o)    (((b) >> (4 - 4*(o))) & 0x0f)
#define setgpixel(b,v,o)    ((b) |= ((v) << (6 - 2*(o))))
#define getgpixel(b,o)      (((b) >> (6 - 2*(o))) & 0x03)
#define setmpixel(b,v,o)    ((b) |= ((v) << (7 - (o))))
#define getmpixel(b,o)      (((b) >> (7 - (o))) & 0x01)

/*
 * Pixels/byte.
 */
#define img_ppb(i) (                            \
        (i)->type == IMG_GRAY   ? 4 :           \
        (i)->type == IMG_GRAY16 ? 2 :           \
        8                                       \
        )

/*
 * Size (in bytes) of an image's data.
 */
#define img_size(i) (size_t)((i)->width/img_ppb(i)*(i)->height)

/*
 * Return the start of row `r'.
 */
#define img_row(i, r)   (&(i)->data[(r)*(i)->width/img_ppb(i)])

/*
 * Only use four bytes of these.
 */
#define IPDB_vIMG   "vIMG"
#define IPDB_View   "View"

/*
 * Only use three bytes of this.
 */
#define IPDB_MYST   "\x40\x6f\x80"

static pilot_time_t const unixepoch = (66*365+17)*24*3600;
    /* The unix epoch in Mac time (the Mac epoch is 00:00 UTC 1904.01.01).
       The 17 is the number of leap years.
    */

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



static void
image_free(IMAGE * const imgP) {

    if (imgP) {
        rechdr_free(imgP->r);
        free(imgP->data);
        free(imgP);
    }
}



static void
text_free(TEXT * const textP) {

    if (textP) {
        rechdr_free(textP->r);
        free(textP->data);
        free(textP);
    }
}



static void
pdbhead_free(PDBHEAD * const headP) {

    free(headP);
}



static void
ipdb_clear(IPDB * const pdbP) {

    if (pdbP) {
        image_free(pdbP->i);
        text_free(pdbP->t);
        pdbhead_free(pdbP->p);
    }
}



void
ipdb_free(IPDB * const pdbP) {

    ipdb_clear(pdbP);
    free(pdbP);
}



static PDBHEAD *
pdbhead_alloc(const char * const name) {

    PDBHEAD * pdbHeadP;

    MALLOCVAR(pdbHeadP);

    if (pdbHeadP) {
        MEMSZERO(pdbHeadP);

        STRSCPY(pdbHeadP->name, name == NULL ? "unnamed" : name);

        /*
         * All of the Image Viewer pdb files that I've come across have
         * 3510939142U (1997.08.16 14:38:22 UTC) here.  I don't know where
         * this bizarre date comes from but the real date works fine so
         * I'm using it.
         */
        pdbHeadP->ctime =
            pdbHeadP->mtime = (pilot_time_t)time(NULL) + unixepoch;
        
        MEMSCPY(&pdbHeadP->type, IPDB_vIMG);
        MEMSCPY(&pdbHeadP->id,   IPDB_View);
    }
    return pdbHeadP;
}



static RECHDR *
rechdr_alloc(int      const type,
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



static IMAGE *
image_alloc(const char * const name,
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

        imgP->r = rechdr_alloc(IMG_REC, IMGOFFSET);

        if (imgP->r) {
            if (w != 0 && h != 0) {
                MALLOCARRAY(imgP->data, w * h);

                if (imgP->data) {
                    MEMSZERO(imgP->data);
                } else
                    failed = true;
            }
            if (failed)
                rechdr_free(imgP->r);
        } else
            failed = true;
        
        if (failed)
            image_free(imgP);
    } else 
        failed = true;

    return failed ? NULL : imgP;
}



static TEXT *
text_alloc(const char * const content) {

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

        textP->r = rechdr_alloc(TEXT_REC, 0);

        if (textP->r) {
            if (content) {
                textP->data = strdup(content);

                if (!textP->data)
                    failed = true;
            }
            if (failed)
                rechdr_free(textP->r);
        } else
            failed = true;

        if (failed)
            text_free(textP);
    } else
        failed = true;

    return failed ? NULL : textP;
}



IPDB *
ipdb_alloc(const char * const name) {

    IPDB * pdbP;
    bool failed;

    failed = false;

    MALLOCVAR(pdbP);

    if (pdbP) {
        MEMSZERO(pdbP);

        if (name) {
            pdbP->p = pdbhead_alloc(name);

            if (!pdbP->p)
                failed = true;
        }
        if (failed)
            ipdb_free(pdbP);
    } else
        failed = true;

    return failed ? NULL : pdbP;
}



static uint8_t *
decompress(const uint8_t * const buffer,
           int             const byteCount) {
/*
 * This will *always* free `buffer'.
 *
 * The compression scheme used is a simple RLE; the control codes,
 * CODE, are one byte and have the following meanings:
 *
 *  CODE >  0x80    Insert (CODE + 1 - 0x80) copies of the next byte.
 *  CODE <= 0x80    Insert the next (CODE + 1) literal bytes.
 *
 * Compressed pieces can (and do) cross row boundaries.
 */
    uint8_t * data;

    MALLOCARRAY(data, byteCount);

    if (data) {
        const uint8_t * inP;
        uint8_t * outP;
        int bytesLeft;
        
        for (bytesLeft = byteCount, inP  = &buffer[0], outP = &data[0];
             bytesLeft > 0;
            ) {

            int got, put;

            if (*inP > 0x80) {
                put = *inP++ + 1 - 0x80;
                memset(outP, *inP, put);
                got = 1;
            } else {
                put = *inP++ + 1;
                memcpy(outP, inP, put);
                got = put;
            }
            inP       += got;
            outP      += put;
            bytesLeft -= put;
        }
    }
    free((void *)buffer);
    return data;
}



#define UNKNOWN_OFFSET  (uint32_t)-1

static int
image_read_data(IMAGE *  const imgP,
                uint32_t const end_offset,
                FILE *   const fP) {

    int retval;
    size_t data_size;
    uint8_t * buffer;

    data_size = 0;  /* initial value */

    if (end_offset == UNKNOWN_OFFSET) {
        /*
         * Read until EOF. Some of them have an extra zero byte
         * dangling off the end.  I originally thought this was
         * an empty note record (even though there was no record
         * header for it); however, the release notes for Image
         * Compression Manager 1.1 on http://www.pilotgear.com
         * note this extra byte as a bug in Image Compression
         * Manager 1.0 which 1.1 fixes.  We'll just blindly read
         * this extra byte and ignore it by paying attention to
         * the image dimensions.
         */
        MALLOCARRAY(buffer, img_size(imgP));

        if (buffer == NULL)
            retval = ENOMEM;
        else {
            data_size = fread(buffer, 1, img_size(imgP), fP);
            if (data_size <= 0)
                retval = EIO;
            else
                retval = 0;

            if (retval != 0)
                free(buffer);
        }
    } else {
        /*
         * Read to the indicated offset.
         */
        data_size = end_offset - ftell(fP) + 1;
        
        MALLOCARRAY(buffer, data_size);

        if (buffer == NULL)
            retval = ENOMEM;
        else {
            ssize_t rc;
            rc = fread(buffer, 1, data_size, fP);
            if (rc != data_size)
                retval = EIO;
            else
                retval = 0;

            if (retval != 0)
                free(buffer);
        }
    }
    if (retval == 0) {
        /*
         * Compressed data can cross row boundaries so we decompress
         * the data here to avoid messiness in the row access functions.
         */
        if (data_size != img_size(imgP)) {
            imgP->data = decompress(buffer, img_size(imgP));
            if (imgP->data == NULL)
                retval = ENOMEM;
            else
                imgP->compressed = true;
        } else {
            imgP->compressed = false;
            imgP->data       = buffer;
        }

        if (retval != 0)
            free(buffer);
    }
    return retval;
}



static int
image_read(IMAGE *  const imgP,
           uint32_t const end_offset,
           FILE *   const fP) {

    if (imgP) {
        imgP->r->offset = (uint32_t)ftell(fP);

        fread(&imgP->name, 1, 32, fP);
        pm_readcharu(fP, &imgP->version);
        pm_readcharu(fP, &imgP->type);
        fread(&imgP->reserved1, 1, 4, fP);
        fread(&imgP->note, 1, 4, fP);
        pm_readbigshortu(fP, &imgP->x_last);
        pm_readbigshortu(fP, &imgP->y_last);
        fread(&imgP->reserved2, 1, 4, fP);
        pm_readbigshortu(fP, &imgP->x_anchor);
        pm_readbigshortu(fP, &imgP->y_anchor);
        pm_readbigshortu(fP, &imgP->width);
        pm_readbigshortu(fP, &imgP->height);
        
        image_read_data(imgP, end_offset, fP);
    }
    return 0;
}



static int
text_read(TEXT * const textP,
          FILE * const fP) {

    int retval;
    char    * s;
    char    buf[128];
    int used, alloced, len;

    if (textP == NULL)
        return 0;

    textP->r->offset = (uint32_t)ftell(fP);
    
    /*
     * What a pain in the ass!  Why the hell isn't there a length
     * attached to the text record?  I suppose the designer wasn't
     * concerned about non-seekable (i.e. pipes) input streams.
     * Perhaps I'm being a little harsh, the lack of a length probably
     * isn't much of an issue on the Pilot.
     */
    used    = 0;
    alloced = 0;
    s       = NULL;
    retval = 0;  /* initial value */
    while ((len = fread(buf, 1, sizeof(buf), fP)) != 0 && retval == 0) {
        if (buf[len - 1] == '\0')
            --len;
        if (used + len > alloced) {
            alloced += 2 * sizeof(buf);
            REALLOCARRAY(s, alloced);

            if (s == NULL)
                retval = ENOMEM;
        }
        if (retval == 0) {
            memcpy(s + used, buf, len);
            used += len;
        }
    }
    if (retval == 0) {
        textP->data = calloc(1, used + 1);
        if (textP->data == NULL)
            retval = ENOMEM;
        else
            memcpy(textP->data, s, used);
    }
    if (s)
        free(s);

    return retval;
}



static int
pdbhead_read(PDBHEAD * const pdbHeadP,
             FILE *    const fP) {

    int retval;

    fread(pdbHeadP->name, 1, 32, fP);
    pm_readbigshortu(fP, &pdbHeadP->flags);
    pm_readbigshortu(fP, &pdbHeadP->version);
    pm_readbiglongu2(fP, &pdbHeadP->ctime);
    pm_readbiglongu2(fP, &pdbHeadP->mtime);
    pm_readbiglongu2(fP, &pdbHeadP->btime);
    pm_readbiglongu2(fP, &pdbHeadP->mod_num);
    pm_readbiglongu2(fP, &pdbHeadP->app_info);
    pm_readbiglongu2(fP, &pdbHeadP->sort_info);
    fread(pdbHeadP->type, 1, 4,  fP);
    fread(pdbHeadP->id,   1, 4,  fP);
    pm_readbiglongu2(fP, &pdbHeadP->uniq_seed);
    pm_readbiglongu2(fP, &pdbHeadP->next_rec);
    pm_readbigshortu(fP, &pdbHeadP->num_recs);

    if (!memeq(pdbHeadP->type, IPDB_vIMG, 4) 
        || !memeq(pdbHeadP->id, IPDB_View, 4))
        retval = E_NOTIMAGE;
    else
        retval = 0;

    return retval;
}



static int
rechdr_read(RECHDR * const rechdrP,
            FILE *   const fP) {

    int retval;
    off_t   len;

    pm_readbiglongu2(fP, &rechdrP->offset);

    len = (off_t)rechdrP->offset - ftell(fP);
    switch(len) {
    case 4:
    case 12:
        /*
         * Version zero (eight bytes of record header) or version
         * two with a note (two chunks of eight record header bytes).
         */
        fread(&rechdrP->unknown[0], 1, 3, fP);
        fread(&rechdrP->rec_type,   1, 1, fP);
        rechdrP->n_extra = 0;
        rechdrP->extra   = NULL;
        retval = 0;
        break;
    case 6:
        /*
         * Version one (ten bytes of record header).
         */
        fread(&rechdrP->unknown[0], 1, 3, fP);
        fread(&rechdrP->rec_type,   1, 1, fP);
        rechdrP->n_extra = 2;
        MALLOCARRAY(rechdrP->extra, rechdrP->n_extra);
        if (rechdrP->extra == NULL)
            retval = ENOMEM;
        else {
            fread(rechdrP->extra, 1, rechdrP->n_extra, fP);
            retval = 0;
        }
        break;
    default:
        /*
         * hmmm.... I'll assume this is the record header
         * for a text record.
         */
        fread(&rechdrP->unknown[0], 1, 3, fP);
        fread(&rechdrP->rec_type,   1, 1, fP);
        rechdrP->n_extra = 0;
        rechdrP->extra   = NULL;
        retval = 0;
        break;
    }
    if (retval == 0) {
        if ((rechdrP->rec_type != IMG_REC && rechdrP->rec_type != TEXT_REC)
            || !memeq(rechdrP->unknown, IPDB_MYST, 3))
            retval = E_NOTRECHDR;
    }
    return retval;
}



int
ipdb_read(IPDB * const pdbP,
          FILE * const fP) {

    int retval;

    ipdb_clear(pdbP);

    pdbP->p = pdbhead_alloc(NULL);

    if (pdbP->p == NULL)
        retval = ENOMEM;
    else {
        int status;

        status = pdbhead_read(pdbP->p, fP);

        if (status != 0)
            retval = status;
        else {
            pdbP->i = image_alloc(pdbP->p->name, IMG_GRAY, 0, 0);
            if (pdbP->i == NULL)
                retval = ENOMEM;
            else {
                int status;
                status = rechdr_read(pdbP->i->r, fP);
                if (status != 0)
                    retval = status;
                else {
                    if (pdbP->p->num_recs > 1) {
                        pdbP->t = text_alloc(NULL);
                        if (pdbP->t == NULL)
                            retval = ENOMEM;
                        else {
                            int status;
                            status = rechdr_read(pdbP->t->r, fP);
                            if (status != 0)
                                retval = status;
                            else
                                retval = 0;
                        }
                    } else
                        retval = 0;
                    
                    if (retval == 0) {
                        uint32_t const offset =
                            pdbP->t == NULL ?
                            UNKNOWN_OFFSET : pdbP->t->r->offset - 1;

                        int status;

                        status = image_read(pdbP->i, offset, fP);
                        if (status != 0)
                            retval = status;
                        else {
                            if (pdbP->t != NULL) {
                                int status;
                                
                                status = text_read(pdbP->t, fP);
                                if (status != 0)
                                    retval = status;
                            }
                        }
                    }
                }
            }
        }
    }
    return retval;
}



static const uint8_t *
g16unpack(const uint8_t * const p,
          uint8_t *       const g,
          int             const w) {

    static const uint8_t pal[] =
        {0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,
         0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00};
    const uint8_t * seg;
    unsigned int i;

    for (i = 0, seg = p; i < w; i += 2, ++seg) {
        g[i + 0] = pal[getg16pixel(*seg, 0)];
        g[i + 1] = pal[getg16pixel(*seg, 1)];
    }
    return g;
}



static const uint8_t *
gunpack(const uint8_t * const p,
        uint8_t *       const g,
        int             const w) {

    static const uint8_t pal[] = {0xff, 0xaa, 0x55, 0x00};
    const uint8_t * seg;
    unsigned int i;

    for (i = 0, seg = p; i < w; i += 4, ++seg) {
        g[i + 0] = pal[getgpixel(*seg, 0)];
        g[i + 1] = pal[getgpixel(*seg, 1)];
        g[i + 2] = pal[getgpixel(*seg, 2)];
        g[i + 3] = pal[getgpixel(*seg, 3)];
    }
    return g;
}



static const uint8_t *
munpack(const uint8_t * const p,
        uint8_t *       const b,
        int             const w) {

    static const uint8_t pal[] = {0x00, 0x01};
    const uint8_t * seg;
    unsigned int i;

    for (i = 0, seg = p; i < w; i += 8, ++seg) {
        b[i + 0] = pal[getmpixel(*seg, 0)];
        b[i + 1] = pal[getmpixel(*seg, 1)];
        b[i + 2] = pal[getmpixel(*seg, 2)];
        b[i + 3] = pal[getmpixel(*seg, 3)];
        b[i + 4] = pal[getmpixel(*seg, 4)];
        b[i + 5] = pal[getmpixel(*seg, 5)];
        b[i + 6] = pal[getmpixel(*seg, 6)];
        b[i + 7] = pal[getmpixel(*seg, 7)];
    }
    return b;
}



const uint8_t *
ipdb_g16row(IPDB *       const pdbP,
            unsigned int const row,
            uint8_t *    const buffer) {

    return g16unpack(img_row(pdbP->i, row), buffer, ipdb_width(pdbP));
}



const uint8_t *
ipdb_grow(IPDB *       const pdbP,
          unsigned int const row,
          uint8_t *       const buffer) {

    return gunpack(img_row(pdbP->i, row), buffer, ipdb_width(pdbP));
}



const uint8_t *
ipdb_mrow(IPDB *       const pdbP,
          unsigned int const row,
          uint8_t *    const buffer) {

    return munpack(img_row(pdbP->i, row), buffer, ipdb_width(pdbP));
}



int
ipdb_remove_image(IPDB * const pdbP) {
    
    int retval;
    /*
     * There's no point in fiddling with pdbP->t->r->offset here since we
     * never know what it really should be until write-time anyway.
     */

    if (pdbP->i == NULL)
        retval = 0;
    else {
        image_free(pdbP->i);
        --pdbP->p->num_recs;
        retval = 0;
    }
    return retval;
}



int
ipdb_remove_text(IPDB * const pdbP) {

    int retval;

    if (pdbP->t == NULL)
        retval = 0;
    else {
        text_free(pdbP->t);
        if (pdbP->i)
            pdbP->i->r->offset -= 8;
        --pdbP->p->num_recs;
        retval = 0;
    }
    return retval;
}



static int
pdbhead_write(PDBHEAD * const pdbheadP,
              FILE *    const fP) {

    fwrite(pdbheadP->name, 1, 32, fP);
    pm_writebigshort(fP, pdbheadP->flags);
    pm_writebigshort(fP, pdbheadP->version);
    pm_writebiglong(fP, pdbheadP->ctime);
    pm_writebiglong(fP, pdbheadP->mtime);
    pm_writebiglong(fP, pdbheadP->btime);
    pm_writebiglong(fP, pdbheadP->mod_num);
    pm_writebiglong(fP, pdbheadP->app_info);
    pm_writebiglong(fP, pdbheadP->sort_info);
    fwrite(pdbheadP->type, 1, 4,  fP);
    fwrite(pdbheadP->id,   1, 4,  fP);
    pm_writebiglong(fP, pdbheadP->uniq_seed);
    pm_writebiglong(fP, pdbheadP->next_rec);
    pm_writebigshort(fP, pdbheadP->num_recs);

    return 0;
}



static int
rechdr_write(RECHDR * const rechdrP,
             FILE *   const fP) {

    if (rechdrP) {
        pm_writebiglong(fP, rechdrP->offset);
        fwrite(rechdrP->unknown,   1, 3, fP);
        fwrite(&rechdrP->rec_type, 1, 1, fP);

        if (rechdrP->n_extra != 0)
            fwrite(rechdrP->extra, 1, rechdrP->n_extra, fP);
    }
    return 0;
}



static int
image_write(IMAGE *   const imgP,
            uint8_t * const data,
            size_t    const n,
            FILE *    const fP) {

    fwrite(imgP->name,      1, 32, fP);
    fwrite(&imgP->version,   1,  1, fP);
    fwrite(&imgP->type,      1,  1, fP);
    fwrite(imgP->reserved1, 1,  4, fP);
    fwrite(imgP->note,      1,  4, fP);
    pm_writebigshort(fP, imgP->x_last);
    pm_writebigshort(fP, imgP->y_last);
    fwrite(imgP->reserved2, 1, 2, fP);
    pm_writebigshort(fP, imgP->x_anchor);
    pm_writebigshort(fP, imgP->y_anchor);
    pm_writebigshort(fP, imgP->width);
    pm_writebigshort(fP, imgP->height);
    fwrite(data, 1,  n, fP);

    return 0;
}



static int
text_write(TEXT * const textP,
           FILE * const fP) {

    if (textP)
        fwrite(textP->data, 1, strlen(textP->data), fP);

    return 0;
}



typedef struct {
    unsigned int match;
    uint8_t      buf[128];
    int          mode;
    size_t       len;
    size_t       used;
    uint8_t *    p;
} RLE;
#define MODE_MATCH  0
#define MODE_LIT    1
#define MODE_NONE   2

#define reset(r) {                              \
        (r)->match = 0xffff;                    \
        (r)->mode  = MODE_NONE;                 \
        (r)->len   = 0;                         \
    }



static void
put_match(RLE *  const rleP,
          size_t const n) {

    *rleP->p++ = 0x80 + n - 1;
    *rleP->p++ = rleP->match;
    rleP->used += 2;
    reset(rleP);
}



static void
put_lit(RLE *  const rleP,
        size_t const n) {

    *rleP->p++ = n - 1;
    rleP->p = (uint8_t *)memcpy(rleP->p, rleP->buf, n) + n;
    rleP->used += n + 1;
    reset(rleP);
}



static size_t
compress(const uint8_t * const inData,
         size_t          const n_in,
         uint8_t *       const out) {

    static void (*put[])(RLE *, size_t) = {put_match, put_lit};
    RLE rle;
    size_t  i;
    const uint8_t * p;

    MEMSZERO(&rle);
    rle.p = out;
    reset(&rle);

    for (i = 0, p = &inData[0]; i < n_in; ++i, ++p) {
        if (*p == rle.match) {
            if (rle.mode == MODE_LIT && rle.len > 1) {
                put_lit(&rle, rle.len - 1);
                ++rle.len;
                rle.match = *p;
            }
            rle.mode = MODE_MATCH;
            ++rle.len;
        } else {
            if (rle.mode == MODE_MATCH)
                put_match(&rle, rle.len);
            rle.mode         = MODE_LIT;
            rle.match        = *p;
            rle.buf[rle.len++] = *p;
        }
        if (rle.len == 128)
            put[rle.mode](&rle, rle.len);
    }
    if (rle.len != 0)
        put[rle.mode](&rle, rle.len);

    return rle.used;
}



int
ipdb_write(IPDB * const pdbP,
           int    const comp,
           FILE * const fP) {

    int retval;

    if (pdbP->i == NULL)
        retval = E_IMAGENOTTHERE;
    else {
        RECHDR * const trP = pdbP->t == NULL ? NULL : pdbP->t->r;
        RECHDR * const irP= pdbP->i->r;

        int n;
        uint8_t * data;

        n    = img_size(pdbP->i);  /* initial value */
        data = pdbP->i->data;  /* initial value */

        if (comp == IPDB_NOCOMPRESS)
            retval = 0;
        else {
            /* Allocate for the worst case. */
            int const allocSz = (3*n + 2)/2;
            
            MALLOCARRAY(data, allocSz);
            
            if (data == NULL)
                retval = ENOMEM;
            else {
                int sz;
                sz = compress(pdbP->i->data, n, data);
                if (comp == IPDB_COMPMAYBE && sz >= n) {
                    free(data);
                    data = pdbP->i->data;
                } else {
                    pdbP->i->compressed = TRUE;
                    if (pdbP->i->type == IMG_GRAY16)
                        pdbP->i->version = 9;
                    else
                        pdbP->i->version = 1;
                    if (pdbP->t != NULL)
                        pdbP->t->r->offset -= n - sz;
                    n = sz;
                }
                retval = 0;
            }

            if (retval == 0) {
                retval = pdbhead_write(pdbP->p, fP);
                if (retval == 0) {
                    retval = rechdr_write(irP, fP);
                    if (retval == 0) {
                        retval = rechdr_write(trP, fP);
                        if (retval == 0) {
                            retval = image_write(pdbP->i, data, n, fP);
                            if (retval == 0) {
                                retval = text_write(pdbP->t, fP);
                            }
                        }
                    }
                }
                if (data != pdbP->i->data)
                    free(data);
            }
        }
    }
    return retval;
}



static int
g16pack(const uint8_t * const inData,
        uint8_t *       const p,
        int             const w) {

    int off, i;
    uint8_t * seg;
    const uint8_t * g;

    for(i = off = 0, seg = p, g=inData; i < w; ++i, ++g) {
        switch(*g) {
        case 0xff:  setg16pixel(*seg, 0x00, off);   break;
        case 0xee:  setg16pixel(*seg, 0x01, off);   break;
        case 0xdd:  setg16pixel(*seg, 0x02, off);   break;
        case 0xcc:  setg16pixel(*seg, 0x03, off);   break;
        case 0xbb:  setg16pixel(*seg, 0x04, off);   break;
        case 0xaa:  setg16pixel(*seg, 0x05, off);   break;
        case 0x99:  setg16pixel(*seg, 0x06, off);   break;
        case 0x88:  setg16pixel(*seg, 0x07, off);   break;
        case 0x77:  setg16pixel(*seg, 0x08, off);   break;
        case 0x66:  setg16pixel(*seg, 0x09, off);   break;
        case 0x55:  setg16pixel(*seg, 0x0a, off);   break;
        case 0x44:  setg16pixel(*seg, 0x0b, off);   break;
        case 0x33:  setg16pixel(*seg, 0x0c, off);   break;
        case 0x22:  setg16pixel(*seg, 0x0d, off);   break;
        case 0x11:  setg16pixel(*seg, 0x0e, off);   break;
        case 0x00:  setg16pixel(*seg, 0x0f, off);   break;
        default:    return E_BADCOLORS;
        }
        if (++off == 2) {
            ++seg;
            off = 0;
        }
    }
    return w/2;
}



static int
gpack(const uint8_t * const inData,
      uint8_t *       const p,
      int             const w) {

    int off, i;
    uint8_t * seg;
    const uint8_t * g;

    for (i = off = 0, seg = p, g = inData; i < w; ++i, ++g) {
        switch(*g) {
        case 0xff:  setgpixel(*seg, 0x00, off); break;
        case 0xaa:  setgpixel(*seg, 0x01, off); break;
        case 0x55:  setgpixel(*seg, 0x02, off); break;
        case 0x00:  setgpixel(*seg, 0x03, off); break;
        default: return E_BADCOLORS;
        }
        if (++off == 4) {
            ++seg;
            off = 0;
        }
    }
    return w/4;
}



static int
mpack(const uint8_t * const inData,
      uint8_t *       const p,
      int             const w) {

    int off, i;
    uint8_t * seg;
    const uint8_t * b;

    for (i = off = 0, seg = p, b = inData; i < w; ++i, ++b) {
        setmpixel(*seg, *b == 0, off);
        if (++off == 8) {
            ++seg;
            off = 0;
        }
    }
    return w/8;
}



static int
adjust_dims(unsigned int   const w,
            unsigned int   const h,
            unsigned int * const awP,
            unsigned int * const ahP) {

    unsigned int provW, provH;

    provW = w;
    provH = h;
    if (provW % 16 != 0)
        provW += 16 - (provW % 16);
    if (provW < 160)
        provW = 160;
    if (provH < 160)
        provH = 160;

    *awP = provW;
    *ahP = provH;

    return w == provW && h == provH;
}



/*
 * You can allocate only 64k chunks of memory on the pilot and that
 * supplies an image size limit.
 */
#define MAX_SIZE(t) ((1 << 16)*((t) == IMG_GRAY ? 4 : 8))
#define MAX_ERROR(t)    ((t) == IMG_GRAY ? E_TOOBIGG : E_TOOBIGM)

static int
image_insert_init(IPDB * const pdbP,
                  int    const uw,
                  int    const uh,
                  int    const type) {

    char * const name = pdbP->p->name;
    unsigned int w, h;
    int retval;

    if (pdbP->p->num_recs != 0)
        retval = E_IMAGETHERE;
    else {
        adjust_dims(uw, uh, &w, &h);
        if (w*h > MAX_SIZE(type))
            retval = MAX_ERROR(type);
        else {
            pdbP->i = image_alloc(name, type, w, h);
            if(pdbP->i == NULL)
                retval = ENOMEM;
            else {
                pdbP->p->num_recs = 1;

                retval =0;
            }
        }
    }
    return retval;
}



int
ipdb_insert_g16image(IPDB *          const pdbP,
                     int             const w,
                     int             const h,
                     const uint8_t * const gArg) {

    int retval;
    int i;

    i = image_insert_init(pdbP, w, h, IMG_GRAY16);
    if (i != 0)
        retval = i;
    else {
        int const incr = ipdb_width(pdbP)/2;
        unsigned int i;
        uint8_t * p;
        const uint8_t * g;

        for (i = 0, p = pdbP->i->data, g = gArg, retval = 0;
             i < h && retval == 0;
             ++i, p += incr, g += w) {

            int const len = g16pack(g, p, w);
            if (len  < 0)
                retval = len;
        }
    } 
    return retval;
}



int
ipdb_insert_gimage(IPDB *          const pdbP,
                   int             const w,
                   int             const h,
                   const uint8_t * const gArg) {

    int i;
    int retval;

    i = image_insert_init(pdbP, w, h, IMG_GRAY);

    if (i != 0)
        retval = i;
    else {
        int const incr = ipdb_width(pdbP)/4;
        unsigned int i;
        uint8_t * p;
        const uint8_t * g;

        for (i = 0, p = pdbP->i->data, g = gArg, retval = 0;
             i < h && retval == 0;
             ++i, p += incr, g += w) {

            int const len = gpack(g, p, w);
            if (len < 0)
                retval = len;
        }
    }
    return retval;
}



int
ipdb_insert_mimage(IPDB *          const pdbP,
                   int             const w,
                   int             const h,
                   const uint8_t * const bArg) {

    int retval;
    int i;

    i = image_insert_init(pdbP, w, h, IMG_MONO);
    if (i != 0)
        retval = i;
    else {
        int const incr = ipdb_width(pdbP)/8;
        unsigned int i;
        uint8_t  * p;
        const uint8_t * b;

        for (i = 0, p = pdbP->i->data, b = bArg, retval = 0;
             i < h && retval == 0;
             ++i, p += incr, b += w) {

            int const len = mpack(b, p, w);
            if (len < 0)
                retval = len;
        }
    } 
    return retval;
}



int
ipdb_insert_text(IPDB *       const pdbP,
                 const char * const s) {

    int retval;

    if (pdbP->i == NULL)
        retval = E_IMAGENOTTHERE;
    else if (pdbP->p->num_recs == 2)
        retval = E_TEXTTHERE;
    else {
        pdbP->t = text_alloc(s);
        if (pdbP->t == NULL)
            retval = ENOMEM;
        else {
            pdbP->p->num_recs = 2;

            pdbP->i->r->offset += 8;
            pdbP->t->r->offset =
                pdbP->i->r->offset + IMAGESIZE + img_size(pdbP->i);

            retval = 0;
        }
    }
    return retval;
}
