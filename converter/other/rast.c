/* rast.c
** (Formerly named libpnm4.c - pnm utility library part 4)
**
** Copyright (C) 1988 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdbool.h>
#include <limits.h>
#include <assert.h>

#include "mallocvar.h"
#include "buffer.h"
#include "pnm.h"

#include "rast.h"

/*
** Semi-work-alike versions of some Sun pixrect routines.  Just enough
** for rasterfile reading and writing to work.
*/

struct pixrect *
mem_create(int const width,
           int const height,
           int const depth) {
/*----------------------------------------------------------------------------
   Caller must ensure dimensions are such that the size of the raster array
   in the image can be represented by a signed integer.
-----------------------------------------------------------------------------*/
    struct pixrect *  retval;
    struct pixrect *  pixRectP;
    struct mpr_data * mprDataP;

    MALLOCVAR(pixRectP);

    if (pixRectP) {
        pixRectP->pr_ops    = NULL;
        pixRectP->pr_size.x = width;
        pixRectP->pr_size.y = height;
        pixRectP->pr_depth  = depth;

        MALLOCVAR(mprDataP);

        if (mprDataP) {
            unsigned int const lineByteCt = (width * depth + 15) / 16 * 2;
            unsigned int const mdImageSz  = lineByteCt * height;

            /* According to the documentation, linebytes is supposed to be
               rounded up to a longword (except on 386 boxes).  However, this
               turns out not to be the case.  In reality, all of Sun's code
               rounds up to a short, not a long.
            */
            assert(width <= ((INT_MAX-15)/depth/height));
                /* Per calling requirements */

            pixRectP->pr_data = mprDataP;

            mprDataP->md_linebytes = lineByteCt;
            mprDataP->md_imageSz   = mdImageSz;
            mprDataP->md_offset.x  = 0;
            mprDataP->md_offset.y  = 0;
            mprDataP->md_flags     = 0;

            MALLOCARRAY(mprDataP->md_image, mdImageSz);

            if (mprDataP->md_image)
                retval = pixRectP;
            else
                retval = NULL;

            if (retval == NULL)
                free(mprDataP);
        }
        if (retval == NULL)
            free(pixRectP);
    }
    return retval;
}



void
mem_free(struct pixrect * const pixRectP) {

    free(pixRectP->pr_data->md_image);
    free(pixRectP->pr_data);
    free(pixRectP);
}



/* Sun rasterfile RLE raster format:

   Each code represents a simple byte or a complex code.  A simple byte
   is a single byte that is not 0x80.  A complex code is multiple bytes
   starting with 0x80.

   A complex code represents either a single 0x80 byte or a run of 2-256
   of any byte.

   A single 0x80 byte is encoded as 0x8000.

   A run of 2-256 any byte is encoded as 0x80NNCC where NN is the count minus
   1 (2 means three instances of the byte, etc.) and CC is the byte value.

   As the code for a run is 3 bytes, it makes sense to encode two identical
   bytes as two simple bytes (assuming they are not 0x80) instead of as a run.
*/


static void
encodeRun(unsigned char   const c,
          unsigned int    const count,
          pm_Buffer *     const beimageP) {
/*----------------------------------------------------------------------------
   Encode into *beimageP a run of 'count' instances of byte 'c'.

   Return as *encodedSizeP the size in bytes of the code produced.

   There must be at least 'count' and at least 3 bytes of storage available
   at 'beimage'.

   We don't necessarily create a "run" as defined by the format; in this
   context, we just mean a sequence (as short as 1) of identical source bytes.

   Note that this encoding stands alone -- we maintain no state.  So calling
   thrice for one 0x32 byte is quite different from calling once for three
   0x32 bytes.
-----------------------------------------------------------------------------*/
    /* Note that 0x80 marks a complex code, so source value of 0x80 must be
       treated specially.
    */

    if (count == 1 && c == 0x80) {
        /* Special coding 0x8000 means a single 128 byte */
        pm_buf_addByte(beimageP, 0x80);
        pm_buf_addByte(beimageP, 0x00);
    } else if (count > 2 || c == 0x80) {
        /* Encode it as a run */
        pm_buf_addByte(beimageP, 0x80);
        assert(count <= 256); assert(count >= 1);
        pm_buf_addByte(beimageP, count - 1);
        pm_buf_addByte(beimageP, c);
    } else {
        unsigned int j;
        for (j = 0; j < count; ++j)
            pm_buf_addByte(beimageP, c);
    }
}



static void
setupDumpRtByteEncoded(struct mpr_data *   const mprDataP,
                       struct rasterfile * const rasterFileP,
                       pm_Buffer **        const beimagePP) {

    unsigned int const beimageAllocSz =
        mprDataP->md_imageSz * 3 / 2 + 1;  /* worst case */

    pm_Buffer * beimageP;

    assert(mprDataP->md_imageSz <= UINT_MAX / 3);
        /* Because of definition of struct rasterfile */

    beimageP = pm_buf_new(beimageAllocSz);
    if (beimageP) {
        unsigned int  count;
        unsigned char c;
        unsigned char pc;
        unsigned int  inCursor;

        for (inCursor = 0, count = 0;
             inCursor < mprDataP->md_imageSz;
             ++inCursor) {

            c = mprDataP->md_image[inCursor];

            if (count > 0) {
                if (pc != c) {
                    encodeRun(pc, count, beimageP);

                    count = 0;
                }
            }
            pc = c;
            ++count;
            if (count == 256) {
                /* Maximum size of a run is 256, so output a run now and start
                   building again.
                */
                encodeRun(c, count, beimageP);

                count = 0;
            }
        }
        if (count > 0)
            encodeRun(pc, count, beimageP);
    }
    *beimagePP = beimageP;
}



static void
setUpColormapDump(colormap_t *        const colormapP,
                  struct rasterfile * const rasterFileP) {

    if (!colormapP) {
        rasterFileP->ras_maptype   = RMT_NONE;
        rasterFileP->ras_maplength = 0;
    } else {
        rasterFileP->ras_maptype = colormapP->type;
        switch (colormapP->type) {
        case RMT_EQUAL_RGB:
            rasterFileP->ras_maplength = colormapP->length * 3;
            break;

        case RMT_RAW:
            rasterFileP->ras_maplength = colormapP->length;
            break;

        default:
            pm_error("unknown colormap type");
        }
    }
}



static int
writeDump(struct rasterfile     const rasterFile,
          const colormap_t *    const colormapP,
          int                   const type,
          const pm_Buffer *     const beimageP,
          const unsigned char * const mdImage,
          FILE *                const ofP) {

    pm_writebiglong(ofP, rasterFile.ras_magic    );
    pm_writebiglong(ofP, rasterFile.ras_width    );
    pm_writebiglong(ofP, rasterFile.ras_height   );
    pm_writebiglong(ofP, rasterFile.ras_depth    );
    pm_writebiglong(ofP, rasterFile.ras_length   );
    pm_writebiglong(ofP, rasterFile.ras_type     );
    pm_writebiglong(ofP, rasterFile.ras_maptype  );
    pm_writebiglong(ofP, rasterFile.ras_maplength);

    if (colormapP) {
        switch (colormapP->type) {
        case RMT_EQUAL_RGB: {
            size_t bytesWrittenCt;

            bytesWrittenCt =
                fwrite(colormapP->map[0], 1, colormapP->length,
                       ofP);
            if (bytesWrittenCt != colormapP->length)
                return PIX_ERR;
            bytesWrittenCt =
                fwrite(colormapP->map[1], 1, colormapP->length,
                       ofP);
            if (bytesWrittenCt != colormapP->length)
                return PIX_ERR;
            bytesWrittenCt =
                fwrite(colormapP->map[2], 1, colormapP->length,
                       ofP);
            if (bytesWrittenCt != colormapP->length)
                return PIX_ERR;
        } break;

        case RMT_RAW: {
            size_t bytesWrittenCt;

            bytesWrittenCt =
                fwrite( colormapP->map[0], 1, colormapP->length,
                        ofP);
            if (bytesWrittenCt != colormapP->length)
                return PIX_ERR;
        } break;
        }
    }

    switch (type) {
    case RT_STANDARD:
    case RT_FORMAT_RGB: {
        size_t bytesWrittenCt;

        bytesWrittenCt = fwrite(mdImage, 1, rasterFile.ras_length, ofP);
        if (bytesWrittenCt != rasterFile.ras_length)
            return PIX_ERR;
    } break;

    case RT_BYTE_ENCODED: {
        size_t bytesWrittenCt;

        bytesWrittenCt = fwrite(pm_buf_buffer(beimageP), 1,
                                pm_buf_length(beimageP), ofP);
        if (bytesWrittenCt != pm_buf_length(beimageP))
            return PIX_ERR;
    } break;
    }
    return 0;
}



int
pr_dump(struct pixrect * const pixRectP,
        FILE *           const ofP,
        colormap_t *     const colormapP,
        int              const type,
        int              const copyFlag) {

    struct rasterfile rasterFile;
    pm_Buffer * beimageP;
    int rc;

    rasterFile.ras_magic  = RAS_MAGIC;
    rasterFile.ras_width  = pixRectP->pr_size.x;
    rasterFile.ras_height = pixRectP->pr_size.y;
    rasterFile.ras_depth  = pixRectP->pr_depth;

    rasterFile.ras_type = type;

    switch (type) {
    case RT_OLD:
        pm_error("This program does not know the Old rasterfile type");
        break;

    case RT_FORMAT_TIFF:
        pm_error("This program does not know the TIFF rasterfile type");
        break;

    case RT_FORMAT_IFF:
        pm_error("This program does not know the IFF rasterfile type");
        break;

    case RT_EXPERIMENTAL:
        pm_error("This program does not know the Experimental "
                 "rasterfile type");
        break;

    case RT_STANDARD:
    case RT_FORMAT_RGB:
        /* Ignore rasterFile.ras_length. */
        rasterFile.ras_length = pixRectP->pr_data->md_imageSz;
        break;

    case RT_BYTE_ENCODED:
        setupDumpRtByteEncoded(pixRectP->pr_data, &rasterFile, &beimageP);
        if (!beimageP)
            pm_error("Failed to allocate memory for beimage");
        rasterFile.ras_length = pm_buf_length(beimageP);
        break;

    default:
        pm_error("unknown rasterfile type");
    }

    setUpColormapDump(colormapP, &rasterFile);

    rc = writeDump(rasterFile, colormapP, type, beimageP,
                   pixRectP->pr_data->md_image, ofP);

    if (rc == PIX_ERR)
        pm_error("Failed to write dump to file");

    if (type == RT_BYTE_ENCODED)
        pm_buf_destroy(beimageP);

    return 0;
}



int
pr_load_header(FILE *              const ifP,
               struct rasterfile * const rasterFileP) {

    {
        long int magic;

        pm_readbiglong(ifP, &magic);
        if (magic != RAS_MAGIC)
            pm_error("Wrong magic number for a RAST file");
    }
    {
        long int width;
        pm_readbiglong(ifP, &width);

        if (width < 0)
            pm_error("Negative width in RAST header");
        else
            rasterFileP->ras_width = width;
    }
    {
        long int height;
        pm_readbiglong(ifP, &height);

        if (height < 0)
            pm_error("Negative height in RAST header");
        else
            rasterFileP->ras_height = height;
    }
    {
        long int depth;
        pm_readbiglong(ifP, &depth);

        if (depth < 0)
            pm_error("Negative depth in RAST header");
        else
            rasterFileP->ras_depth = depth;
    }
    /* Definition of struct rasterfile says dimensions have to allow the
       number of bytes in the entire raster in the image to be representable
       as a signed integer.
    */
    if (rasterFileP->ras_width >
        ((INT_MAX-256)/rasterFileP->ras_depth/rasterFileP->ras_height)) {
        pm_error("Image is too large (%lu wide x %lu high x %lu deep) "
                 "for computations",
                 rasterFileP->ras_width,
                 rasterFileP->ras_height,
                 rasterFileP->ras_depth);
    }

    {
        long int length;
        pm_readbiglong(ifP, &length);

        if (length < 0)
            pm_error("Negative length in RAST header");
        else
            rasterFileP->ras_length = length;
    }
    pm_readbiglong(ifP, &rasterFileP->ras_type);

    pm_readbiglong(ifP, &rasterFileP->ras_maptype);
    {
        long int mapLength;
        pm_readbiglong(ifP, &mapLength);

        if (mapLength < 0)
            pm_error("Negative map length in RAST header");
        else
            rasterFileP->ras_maplength = mapLength;
    }
    return 0;
}



int
pr_load_colormap(FILE *              const ifP,
                 struct rasterfile * const rasterFileP,
                 colormap_t *        const colormapP) {

    int retval;

    if (!colormapP || rasterFileP->ras_maptype == RMT_NONE) {
        unsigned int i;

        for (i = 0, retval = 0;
             i < rasterFileP->ras_maplength && retval == 0;
             ++i) {

            int c;
            c = getc(ifP);
            if (c == EOF)
                retval = PIX_ERR;
        }
    } else {
        colormapP->type = rasterFileP->ras_maptype;
        switch (rasterFileP->ras_maptype) {
        case RMT_EQUAL_RGB: {
            colormapP->length = rasterFileP->ras_maplength / 3;
            MALLOCARRAY(colormapP->map[0], colormapP->length);
            if (!colormapP->map[0])
                retval = PIX_ERR;
            else {
                MALLOCARRAY(colormapP->map[1], colormapP->length);
                if (!colormapP->map[1])
                    retval = PIX_ERR;
                else {
                    MALLOCARRAY(colormapP->map[2], colormapP->length);
                    if (!colormapP->map[2])
                        retval = PIX_ERR;
                    else {
                        if (fread(colormapP->map[0], 1, colormapP->length,
                                  ifP) != colormapP->length ||
                            fread(colormapP->map[1], 1, colormapP->length,
                                  ifP) != colormapP->length ||
                            fread(colormapP->map[2], 1, colormapP->length,
                                  ifP) != colormapP->length)
                            retval = PIX_ERR;
                        else
                            retval = 0;

                        if (retval == PIX_ERR)
                            free(colormapP->map[2]);
                    }
                    if (retval == PIX_ERR)
                        free(colormapP->map[1]);
                }
                if (retval == PIX_ERR)
                    free(colormapP->map[0]);
            }
        } break;

        case RMT_RAW: {
            colormapP->length = rasterFileP->ras_maplength;
            MALLOCARRAY(colormapP->map[0], colormapP->length);
            if (!colormapP->map[0])
                retval = PIX_ERR;
            else {
                size_t bytesRead;

                colormapP->map[2] = colormapP->map[1] = colormapP->map[0];
                bytesRead = fread(colormapP->map[0], 1,
                                  rasterFileP->ras_maplength, ifP);
                if (bytesRead != rasterFileP->ras_maplength)
                    retval = PIX_ERR;
                else
                    retval = 0;

                if (retval == PIX_ERR)
                    free(colormapP->map[0]);
            }
            } break;

        default:
            pm_error("unknown colormap type");
        }
    }
    return retval;
}



static void
loadRgb(FILE *                  const ifP,
        const struct mpr_data * const mprDataP,
        bool *                  const failedP) {

    size_t bytesReadCt;

    bytesReadCt = fread(mprDataP->md_image, 1,
                        mprDataP->md_imageSz, ifP);

    *failedP = (bytesReadCt != mprDataP->md_imageSz);
}



static void
loadByteEncoded(FILE *                    const ifP,
                unsigned int              const beImageSz,
                const struct mpr_data *   const mprDataP,
                bool *                    const failedP) {
/*----------------------------------------------------------------------------
   Load the raster from file *ifP, assuming it is in byte encoded format.

   The raster is 'beImageSz' bytes long in the file.

   Return the decoded raster in *mprDataP.

   Return *failedP == true iff we fail to load and decode the raster.
   These are the ways we can fail:

     - We encounter I/O error
     - File ends before 'beImageSz' bytes
     - Because of invalid coding at the end of the raster. the raster ends in
       the middle of a code
     - *mprDataP does not have enough space allocated for the decoded raster
-----------------------------------------------------------------------------*/
    unsigned char * beimage;

    MALLOCARRAY(beimage, beImageSz);
    if (!beimage)
        *failedP = true;
    else {
        size_t bytesReadCt;

        bytesReadCt = fread(beimage, 1, beImageSz, ifP);
        if (bytesReadCt != beImageSz)
            *failedP = true;
        else {
            unsigned char * const mdImage   = mprDataP->md_image;
            unsigned int    const mdImageSz = mprDataP->md_imageSz;

            unsigned int inCursor, outCursor;

            for (inCursor = 0, inCursor = 0, outCursor = 0, *failedP = false;
                 inCursor < beImageSz && !*failedP; ) {

                unsigned char const c = beimage[inCursor++];

                if (c == 0x80) {
                    /* It is a complex code */
                    if (inCursor >= beImageSz)
                        *failedP = true;
                    else {
                        unsigned int const countFld = beimage[inCursor++];
                        if (countFld == 0) {
                            /* It is the complex code for single 0x80 */
                            if (outCursor >= mdImageSz)
                                *failedP = true;
                            else
                                mdImage[outCursor++] = 0x80;
                        } else {
                            /* It is a complex code for a run */
                            if (inCursor >= beImageSz)
                                *failedP = true;
                            else {
                                unsigned int  const count = countFld + 1;
                                unsigned char const c = beimage[inCursor++];

                                if (outCursor + count > mdImageSz)
                                    *failedP = true;
                                else {
                                    unsigned int j;
                                    for (j = 0; j < count; ++j)
                                        mdImage[outCursor++] = c;
                                }
                            }
                        }
                    }
                } else {
                    /* It is a simple single-byte code */
                    if (outCursor >= mdImageSz)
                        *failedP = true;
                    else
                        mdImage[outCursor++] = c;
                }
            }
        }
        free(beimage);
    }
}



struct pixrect *
pr_load_image(FILE *                    const ifP,
              const struct rasterfile * const rasterFileP,
              colormap_t *              const colormapP) {

    struct pixrect * retval;
    struct pixrect * pixRectP;
    bool failed;

    assert(rasterFileP->ras_width <=
           ((INT_MAX-15)/rasterFileP->ras_depth/rasterFileP->ras_height));
        /* Definition of struct rasterfile */

    pixRectP =
        mem_create(rasterFileP->ras_width,
                   rasterFileP->ras_height,
                   rasterFileP->ras_depth);
    if (!pixRectP)
        retval = NULL;
    else {
        switch (rasterFileP->ras_type) {
        case RT_OLD:
            pm_error("This program does not know the Old rasterfile type");

        case RT_FORMAT_TIFF:
            pm_error("This program does not know the TIFF rasterfile type");

        case RT_FORMAT_IFF:
            pm_error("This program does not know the IFF rasterfile type");

        case RT_EXPERIMENTAL:
            pm_error("This program does not know the Experimental "
                     "rasterfile type");

        case RT_STANDARD:
        case RT_FORMAT_RGB:
            /* Ignore rasterFileP->ras_length. */

            loadRgb(ifP, pixRectP->pr_data, &failed);
            break;

        case RT_BYTE_ENCODED:
            loadByteEncoded(ifP, rasterFileP->ras_length, pixRectP->pr_data,
                            &failed);
            break;

        default:
            pm_error("unknown rasterfile type");
        }

        if (failed) {
            mem_free(pixRectP);
            retval = NULL;
        } else
            retval = pixRectP;
    }
    return retval;
}



