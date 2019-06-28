/*=============================================================================
                               pamtopdbimg
===============================================================================

  Convert Palm Pilot PDB Image format (for viewing by
  Pilot Image Viewer) to Netpbm image.

  Bryan Henderson derived this from Eric Howe's program named
  'imgvtopnm', in September 2010.
=============================================================================*/
/*
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
 *             Bryan Henderson
 */
#include <stdlib.h>
#include <assert.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pam.h"

#include "ipdb.h"


struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* '-' if stdin */
    const char * notefile;  /* NULL if not specified */
    unsigned int verbose;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct cmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.  

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry *option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int notefileSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "notefile",            OPT_STRING,    &cmdlineP->notefile,
            &notefileSpec,            0);
    OPTENT3(0, "verbose",             OPT_FLAG,    NULL,
            &cmdlineP->verbose,       0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!notefileSpec)
        cmdlineP->notefile = NULL;
    
    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileName = argv[1];
    else
        pm_error("Program takes at most one argument:  input file name");
}



#define getg16pixel(b,o)    (((b) >> (4 - 4*(o))) & 0x0f)
#define getgpixel(b,o)      (((b) >> (6 - 2*(o))) & 0x03)
#define getmpixel(b,o)      (((b) >> (7 - (o))) & 0x01)


static void
abortShort() {
    pm_error("Invalid image.  Compression algorithm runs out of "
             "compressed data before generating the expected "
             "amount of image data");
}



static void
abortOverrun() {
    pm_error("Invalid image.  Compression algorithm finds the end of "
             "the image in the middle of a run");
}



static void
decompress(const uint8_t * const compressed,
           size_t          const compressedSize,
           size_t          const imageSize,
           uint8_t **      const uncompressedP) {
/*----------------------------------------------------------------------------
   Decompress the data 'compressed', which is 'compressedSize' bytes long.
   Return the decompressed data in newly malloced storage as
   *decompressedP.  Decompression should yield exactly 'imageSize' bytes.
-----------------------------------------------------------------------------*/
    /*
     * The compression scheme used is a simple RLE; the control codes,
     * CODE, are one byte and have the following meanings:
     *
     *  CODE >  0x80    Insert (CODE + 1 - 0x80) copies of the next byte.
     *  CODE <= 0x80    Insert the next (CODE + 1) literal bytes.
     *
     * Compressed pieces can (and do) cross row boundaries.
     */
    uint8_t * uncompressed;

    MALLOCARRAY(uncompressed, imageSize);

    if (uncompressed) {
        const uint8_t * inP;
        uint8_t *       outP;
        size_t          bytesLeft;
        
        for (bytesLeft = imageSize,
                 inP  = &compressed[0], outP = &uncompressed[0];
             bytesLeft > 0;
            ) {

            int got, put;

            if (inP > compressed + compressedSize)
                abortShort();

            if (*inP > 0x80) {
                put = *inP++ + 1 - 0x80;
                if (outP + put > uncompressed + imageSize)
                    abortOverrun();
                memset(outP, *inP, put);
                got = 1;
            } else {
                put = *inP++ + 1;
                if (inP + put > compressed + compressedSize)
                    abortShort();
                if (outP + put > uncompressed + imageSize)
                    abortOverrun();
                memcpy(outP, inP, put);
                got = put;
            }
            inP       += got;
            outP      += put;
            assert(bytesLeft >= put);
            bytesLeft -= put;
        }
    }
    *uncompressedP = uncompressed;
}



#define UNKNOWN_OFFSET  (uint32_t)-1

static void
readCompressed(IMAGE *    const imgP,
               uint32_t   const end_offset,
               FILE *     const fP,
               size_t *   const dataSizeP,
               uint8_t ** const dataP,
               int *      const retvalP) {
/*----------------------------------------------------------------------------
   Read the compressed data from file *fP (actually, if the image isn't
   compressed, then it's just the regular data).

   Return the data in newly malloced storage as *dataP, which is
   *dataSizeP bytes long.
-----------------------------------------------------------------------------*/
    int retval;
    uint8_t * buffer;
    size_t dataSize;

    dataSize = 0;  /* initial value */

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
        MALLOCARRAY(buffer, ipdb_img_size(imgP));

        if (buffer == NULL)
            retval = ENOMEM;
        else {
            dataSize = fread(buffer, 1, ipdb_img_size(imgP), fP);
            if (dataSize <= 0)
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
        dataSize = end_offset - ftell(fP) + 1;
        
        MALLOCARRAY(buffer, dataSize);

        if (buffer == NULL)
            retval = ENOMEM;
        else {
            ssize_t rc;
            rc = fread(buffer, 1, dataSize, fP);
            if (rc != dataSize)
                retval = EIO;
            else
                retval = 0;

            if (retval != 0)
                free(buffer);
        }
    }
    *dataSizeP = dataSize;
    *dataP = buffer;
    *retvalP = retval;
}



static void
imageReadHeader(FILE *  const fileP,
                IMAGE * const imgP,
                bool    const dump) {

    fread(&imgP->name, 1, 32, fileP);
    pm_readcharu(fileP, &imgP->version);
    pm_readcharu(fileP, &imgP->type);
    fread(&imgP->reserved1, 1, 4, fileP);
    fread(&imgP->note, 1, 4, fileP);
    pm_readbigshortu(fileP, &imgP->x_last);
    pm_readbigshortu(fileP, &imgP->y_last);
    fread(&imgP->reserved2, 1, 4, fileP);
    pm_readbigshortu(fileP, &imgP->x_anchor);
    pm_readbigshortu(fileP, &imgP->y_anchor);
    pm_readbigshortu(fileP, &imgP->width);
    pm_readbigshortu(fileP, &imgP->height);

    if (dump) {
        pm_message("PDB IMAGE header:");
        pm_message("  Name: '%.*s'", (int)sizeof(imgP->name), imgP->name);
        pm_message("  Version: %02x", imgP->version);
        pm_message("  Type: %s", ipdb_typeName(imgP->type));
        pm_message("  Note: %02x %02x %02x %02x",
                   imgP->note[0], imgP->note[1], imgP->note[2], imgP->note[3]);
        pm_message("  X_last: %u", imgP->x_last);
        pm_message("  Y_last: %u", imgP->y_last);
        pm_message("  X_anchor: %u", imgP->x_anchor);
        pm_message("  Y_anchor: %u", imgP->y_anchor);
        pm_message("  Width: %u", imgP->width);
        pm_message("  Height: %u", imgP->height);
        pm_message("Pixels per byte: %u", ipdb_img_ppb(imgP));
        pm_message("Image size: %lu bytes",
                   (unsigned long)ipdb_img_size(imgP));
    }
}


static int
imageReadData(FILE *   const fileP,
              IMAGE *  const imgP,
              uint32_t const end_offset) {

    int retval;
    size_t dataSize;
    uint8_t * buffer;

    readCompressed(imgP, end_offset, fileP, &dataSize, &buffer, &retval);

    if (retval == 0) {
        /*
         * Compressed data can cross row boundaries so we decompress
         * the data here to avoid messiness in the row access functions.
         */
        if (dataSize != ipdb_img_size(imgP)) {
            decompress(buffer, dataSize, ipdb_img_size(imgP), &imgP->data);
            if (imgP->data == NULL)
                retval = ENOMEM;
            else
                imgP->compressed = true;
            free(buffer);
        } else {
            imgP->compressed = false;
            imgP->data       = buffer;
            /* Storage at 'buffer' now belongs to *imgP */
        }
    }
    return retval;
}



static int
imageRead(IMAGE *  const imgP,
          uint32_t const end_offset,
          FILE *   const fileP,
          bool     const verbose) {

    if (imgP) {
        imgP->r->offset = (uint32_t)ftell(fileP);

        imageReadHeader(fileP, imgP, verbose);

        imageReadData(fileP, imgP, end_offset);
    }
    return 0;
}



static int
textRead(TEXT * const textP,
         FILE * const fileP) {

    int retval;
    char    * s;
    char    buf[128];
    int used, alloced, len;

    if (textP == NULL)
        return 0;

    textP->r->offset = (uint32_t)ftell(fileP);
    
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
    while ((len = fread(buf, 1, sizeof(buf), fileP)) != 0 && retval == 0) {
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
pdbheadRead(PDBHEAD * const pdbHeadP,
            FILE *    const fileP) {

    int retval;

    fread(pdbHeadP->name, 1, 32, fileP);
    pm_readbigshortu(fileP, &pdbHeadP->flags);
    pm_readbigshortu(fileP, &pdbHeadP->version);
    pm_readbiglongu2(fileP, &pdbHeadP->ctime);
    pm_readbiglongu2(fileP, &pdbHeadP->mtime);
    pm_readbiglongu2(fileP, &pdbHeadP->btime);
    pm_readbiglongu2(fileP, &pdbHeadP->mod_num);
    pm_readbiglongu2(fileP, &pdbHeadP->app_info);
    pm_readbiglongu2(fileP, &pdbHeadP->sort_info);
    fread(pdbHeadP->type, 1, 4,  fileP);
    fread(pdbHeadP->id,   1, 4,  fileP);
    pm_readbiglongu2(fileP, &pdbHeadP->uniq_seed);
    pm_readbiglongu2(fileP, &pdbHeadP->next_rec);
    pm_readbigshortu(fileP, &pdbHeadP->num_recs);

    if (!memeq(pdbHeadP->type, IPDB_vIMG, 4) 
        || !memeq(pdbHeadP->id, IPDB_View, 4))
        retval = E_NOTIMAGE;
    else
        retval = 0;

    return retval;
}



static int
rechdrRead(RECHDR * const rechdrP,
           FILE *   const fileP) {

    int retval;
    off_t   len;

    pm_readbiglongu2(fileP, &rechdrP->offset);

    len = (off_t)rechdrP->offset - ftell(fileP);
    switch(len) {
    case 4:
    case 12:
        /*
         * Version zero (eight bytes of record header) or version
         * two with a note (two chunks of eight record header bytes).
         */
        fread(&rechdrP->unknown[0], 1, 3, fileP);
        fread(&rechdrP->rec_type,   1, 1, fileP);
        rechdrP->n_extra = 0;
        rechdrP->extra   = NULL;
        retval = 0;
        break;
    case 6:
        /*
         * Version one (ten bytes of record header).
         */
        fread(&rechdrP->unknown[0], 1, 3, fileP);
        fread(&rechdrP->rec_type,   1, 1, fileP);
        rechdrP->n_extra = 2;
        MALLOCARRAY(rechdrP->extra, rechdrP->n_extra);
        if (rechdrP->extra == NULL)
            retval = ENOMEM;
        else {
            fread(rechdrP->extra, 1, rechdrP->n_extra, fileP);
            retval = 0;
        }
        break;
    default:
        /*
         * hmmm.... I'll assume this is the record header
         * for a text record.
         */
        fread(&rechdrP->unknown[0], 1, 3, fileP);
        fread(&rechdrP->rec_type,   1, 1, fileP);
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



static int
ipdbRead(IPDB * const pdbP,
         FILE * const fileP,
         bool   const verbose) {

    int retval;

    ipdb_clear(pdbP);

    pdbP->p = ipdb_pdbhead_alloc(NULL);

    if (pdbP->p == NULL)
        retval = ENOMEM;
    else {
        int status;

        status = pdbheadRead(pdbP->p, fileP);

        if (status != 0)
            retval = status;
        else {
            pdbP->i = ipdb_image_alloc(pdbP->p->name, IMG_GRAY, 0, 0);
            if (pdbP->i == NULL)
                retval = ENOMEM;
            else {
                int status;
                status = rechdrRead(pdbP->i->r, fileP);
                if (status != 0)
                    retval = status;
                else {
                    if (pdbP->p->num_recs > 1) {
                        pdbP->t = ipdb_text_alloc(NULL);
                        if (pdbP->t == NULL)
                            retval = ENOMEM;
                        else {
                            int status;
                            status = rechdrRead(pdbP->t->r, fileP);
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

                        status = imageRead(pdbP->i, offset, fileP, verbose);
                        if (status != 0)
                            retval = status;
                        else {
                            if (pdbP->t != NULL) {
                                int status;
                                
                                status = textRead(pdbP->t, fileP);
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



static void
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
}



static void
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
}



static void
munpack(const uint8_t * const p,
        uint8_t *       const b,
        int             const w) {

    static const uint8_t pal[] = {PAM_BW_WHITE, PAM_BLACK};
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
}



static void
g16row(IPDB *       const pdbP,
       unsigned int const row,
       uint8_t *    const buffer) {
    
    g16unpack(ipdb_img_row(pdbP->i, row), buffer, ipdb_width(pdbP));
}



static void
grow(IPDB *       const pdbP,
     unsigned int const row,
     uint8_t *    const buffer) {

    gunpack(ipdb_img_row(pdbP->i, row), buffer, ipdb_width(pdbP));
}



static void
mrow(IPDB *       const pdbP,
     unsigned int const row,
     uint8_t *    const buffer) {

    munpack(ipdb_img_row(pdbP->i, row), buffer, ipdb_width(pdbP));
}



static void
writeImgPam(IPDB * const pdbP,
            FILE * const ofP) {

    struct pam pam;
    tuple * tupleRow;
    unsigned int row;
    uint8_t * imgRow;

    MALLOCARRAY(imgRow, ipdb_width(pdbP));

    pam.size             = sizeof(pam);
    pam.len              = PAM_STRUCT_SIZE(tuple_type);
    pam.file             = ofP;
    pam.plainformat      = 0;
    pam.width            = ipdb_width(pdbP);
    pam.height           = ipdb_height(pdbP);
    pam.depth            = 1;
    pam.maxval           = ipdb_type(pdbP) == IMG_MONO ? 1 : 255;
    pam.bytes_per_sample = pnm_bytespersample(pam.maxval);
    pam.format           = PAM_FORMAT;
    strcpy(pam.tuple_type,
           ipdb_type(pdbP) == IMG_MONO ?
           PAM_PBM_TUPLETYPE : PAM_PGM_TUPLETYPE);

    pnm_writepaminit(&pam);
    
    tupleRow = pnm_allocpamrow(&pam);

    for (row = 0; row < pam.height; ++row) {
        unsigned int col;


        if (ipdb_type(pdbP) == IMG_MONO)
            mrow(pdbP, row, imgRow);
        else if (ipdb_type(pdbP) == IMG_GRAY)
            grow(pdbP, row, imgRow);
        else
            g16row(pdbP, row, imgRow);

        for (col = 0; col < pam.width; ++col)
            tupleRow[col][0] = imgRow[col];
        
        pnm_writepamrow(&pam, tupleRow);
    }
    pnm_freepamrow(tupleRow);

    free(imgRow);
}



static void
writeText(IPDB *       const pdbP,
          const char * const name) {

    const char * const note = ipdb_text(pdbP);

    FILE * fP;

    if (name == NULL || note == NULL) {
    } else {
        fP = pm_openw(name);
        if (fP == NULL)
            pm_error("Could not open note file '%s' for output", name);
        
        fprintf(fP, "%s\n", note);

        pm_close(fP);
    }
}



int
main(int argc, const char ** argv) {

    struct cmdlineInfo cmdline;
    FILE * ifP;
    IPDB * pdbP;
    int status;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    pdbP = ipdb_alloc(NULL);
    if (pdbP == NULL)
        pm_error("Could not allocate IPDB structure.");

    status = ipdbRead(pdbP, ifP, cmdline.verbose);
    if (status != 0)
        pm_error("Image header read error: %s.", ipdb_err(status));

    writeImgPam(pdbP, stdout);

    writeText(pdbP, cmdline.notefile);

    ipdb_free(pdbP);

    pm_close(ifP);

    return EXIT_SUCCESS;
}
