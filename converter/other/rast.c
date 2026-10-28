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

#include <assert.h>

#include "mallocvar.h"
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
            pixRectP->pr_data = mprDataP;

            /* According to the documentation, linebytes is supposed to be
               rounded up to a longword (except on 386 boxes).  However, this
               turns out not to be the case.  In reality, all of Sun's code
               rounds up to a short, not a long.
            */
            assert(width <= ((INT_MAX-15)/depth/height));
                /* Per calling requirements */

            mprDataP->md_linebytes = (width * depth + 15) / 16 * 2;
            mprDataP->md_offset.x  = 0;
            mprDataP->md_offset.y  = 0;
            mprDataP->md_flags     = 0;

            MALLOCARRAY(mprDataP->md_image, mprDataP->md_linebytes * height);

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



static void
setupDumpRtByteEncoded(struct pixrect *    const pixRectP,
                       struct rasterfile * const rasterFileP,
                       unsigned char **    const beimageP,
                       unsigned int *      const besizeP) {

    unsigned int const size =
        pixRectP->pr_size.y * pixRectP->pr_data->md_linebytes;
    unsigned int const beimageAllocSz = size * 3 / 2 + 1;  /* worst case */

    unsigned char * beimage;
    unsigned int    besize;

    assert(pixRectP->pr_size.y <= UINT_MAX / pixRectP->pr_data->md_linebytes);
    assert(size <= UINT_MAX / 3);
        /* Because of definition of struct rasterfile */

    MALLOCARRAY(beimage, beimageAllocSz);
    if (beimage) {
        unsigned int  count;
        unsigned char c;
        unsigned char pc;
        unsigned int  inCursor;

        for (inCursor = 0, besize = 0, count = 0;
             inCursor < size;
             ++inCursor) {

            c = pixRectP->pr_data->md_image[inCursor];

            if (count > 0) {
                if (pc != c) {
                    if (count == 1 && pc == 128) {
                        beimage[besize++] = 128;
                        beimage[besize++] = 0;
                        count = 0;
                    } else if (count > 2 || pc == 128) {
                        beimage[besize++] = 128;
                        beimage[besize++] = count - 1;
                        beimage[besize++] = pc;
                        count = 0;
                    } else {
                        unsigned int j;
                        for (j = 0; j < count; ++j)
                            beimage[besize++] = pc;
                        count = 0;
                    }
                }
            }
            pc = c;
            ++count;
            if (count == 256) {
                beimage[besize++] = 128;
                beimage[besize++] = count - 1;
                beimage[besize++] = c;
                count = 0;
            }
        }
        if (count > 0) {
            if (count == 1 && c == 128) {
                beimage[besize++] = 128;
                beimage[besize++] = 0;
            }
            if (count > 2 || c == 128) {
                beimage[besize++] = 128;
                beimage[besize++] = count - 1;
                beimage[besize++] = c;
            } else {
                unsigned int j;
                for (j = 0; j < count; ++j)
                    beimage[besize++] = c;
            }
        }
    }
    assert(besize <= beimageAllocSz);
    *beimageP = beimage;
    *besizeP  = besize;
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
          const unsigned char * const beimage,
          unsigned int          const besize,
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

        bytesWrittenCt =  fwrite(beimage, 1, besize, ofP);
        if (bytesWrittenCt != besize)
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
    unsigned char * beimage;
    unsigned int besize;
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
        rasterFile.ras_length =
            pixRectP->pr_size.y * pixRectP->pr_data->md_linebytes;
        break;

    case RT_BYTE_ENCODED:
        setupDumpRtByteEncoded(pixRectP, &rasterFile, &beimage, &besize);
        if (!beimage)
            pm_error("Failed to allocate memory for beimage");
        rasterFile.ras_length = besize;
        break;

    default:
        pm_error("unknown rasterfile type");
    }

    setUpColormapDump(colormapP, &rasterFile);

    rc = writeDump(rasterFile, colormapP, type, beimage, besize,
                   pixRectP->pr_data->md_image, ofP);

    if (rc == PIX_ERR)
        pm_error("Failed to write dump to file");

    if (type == RT_BYTE_ENCODED)
        free(beimage);

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
        ((INT_MAX-15)/rasterFileP->ras_depth/rasterFileP->ras_height)) {
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

                if (retval == PIX_ERR)
                    free(colormapP->map[0]);
            }
            } break;

        default:
            pm_error("unknown colormap type");
        }
    }
    return 0;
}



struct pixrect *
pr_load_image(FILE *                    const ifP,
              const struct rasterfile * const rasterFileP,
              colormap_t *              const colormapP) {

    struct pixrect * retval;
    struct pixrect * pixRectP;

    assert(rasterFileP->ras_width <=
           ((INT_MAX-15)/rasterFileP->ras_depth/rasterFileP->ras_height));
        /* Definition of struct rasterfile */

    pixRectP =
        mem_create(rasterFileP->ras_width,
                   rasterFileP->ras_height,
                   rasterFileP->ras_depth);
    if (!pixRectP)
        return NULL;

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
    case RT_FORMAT_RGB: {
        size_t bytesReadCt;
        /* Ignore rasterFileP->ras_length. */
        unsigned int const mdImageSz =
            pixRectP->pr_size.y * pixRectP->pr_data->md_linebytes;
        bytesReadCt = fread(pixRectP->pr_data->md_image, 1, mdImageSz, ifP);
        if (bytesReadCt != mdImageSz)
            retval = NULL;
    } break;

    case RT_BYTE_ENCODED: {
        unsigned char * beimage;

        MALLOCARRAY(beimage, rasterFileP->ras_length);
        if (!beimage)
            retval = NULL;
        else {
            size_t bytesReadCt;

            bytesReadCt = fread(beimage, 1, rasterFileP->ras_length, ifP);
            if (bytesReadCt != rasterFileP->ras_length)
                retval = NULL;
            else {
                unsigned char * const mdImage = pixRectP->pr_data->md_image;

                unsigned int inCursor, outCursor;

                for (inCursor = 0, inCursor = 0, outCursor = 0;
                     inCursor < rasterFileP->ras_length; ) {

                    unsigned char const c = beimage[inCursor++];

                    if (c == 128) {
                        unsigned int const count = beimage[inCursor++] + 1;
                        if (count == 1) {
                            mdImage[outCursor++] = 128;
                        } else {
                            unsigned char const c = beimage[inCursor++];
                            unsigned int j;
                            for (j = 0; j < count; ++j)
                                mdImage[outCursor++] = c;
                        }
                    } else
                        mdImage[outCursor++] = c;
                }
                retval = pixRectP;
                assert(inCursor == rasterFileP->ras_length);
                assert(outCursor ==
                       pixRectP->pr_size.y * pixRectP->pr_data->md_linebytes);
            }
            free(beimage);
        }
    } break;

    default:
        pm_error("unknown rasterfile type");
    }

    if (retval == NULL)
        mem_free(pixRectP);

    return retval;
}



