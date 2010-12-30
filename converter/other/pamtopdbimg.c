/*=============================================================================
                               pamtopdbimg
===============================================================================

  Convert Netpbm image to Palm Pilot PDB Image format (for viewing by
  Pilot Image Viewer).

  Bryan Henderson derived this from Eric Howe's programs named
  'pgmtoimgv' and 'pbmtoimgv' in September 2010.
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
 *             Bryan Henderson, September 2010.
 */

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/stat.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pam.h"

#include "ipdb.h"

enum CompMode {COMPRESSED, MAYBE, UNCOMPRESSED};

struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char *  inputFileName;  /* '-' if stdin */
    const char * title;
    const char * notefile;  /* NULL if not specified */
    enum CompMode compMode;
    unsigned int depth4;
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

    unsigned int titleSpec, notefileSpec;
    unsigned int compressed, maybeCompressed, uncompressed;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "title",               OPT_STRING,    &cmdlineP->title,
            &titleSpec,               0);
    OPTENT3(0, "notefile",            OPT_STRING,    &cmdlineP->notefile,
            &notefileSpec,            0);
    OPTENT3(0, "compressed",          OPT_FLAG,      NULL,
            &compressed,              0);
    OPTENT3(0, "maybecompressed",     OPT_FLAG,      NULL,
            &maybeCompressed,         0);
    OPTENT3(0, "uncompressed",        OPT_FLAG,      NULL,
            &uncompressed,            0);
    OPTENT3(0, "4depth",              OPT_FLAG,      NULL,
            &cmdlineP->depth4,        0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */


    if (!titleSpec)
        cmdlineP->title = "unnamed";

    if (!notefileSpec)
        cmdlineP->notefile = NULL;
    
    if (compressed + uncompressed + maybeCompressed > 1)
        pm_error("You may specify only one of -compressed, -uncompressed, "
                 "-maybecompressed");
    if (compressed)
        cmdlineP->compMode = COMPRESSED;
    else if (uncompressed)
        cmdlineP->compMode = UNCOMPRESSED;
    else if (maybeCompressed)
        cmdlineP->compMode = MAYBE;
    else
        cmdlineP->compMode = MAYBE;

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileName = argv[1];
    else
        pm_error("Program takes at most one argument:  input file name");
}



/*
 * Pixel setting macros.
 */
#define setg16pixel(b,v,o)  ((b) |= ((v) << (4 - 4*(o))))
#define setgpixel(b,v,o)    ((b) |= ((v) << (6 - 2*(o))))
#define setmpixelblack(b,o)    ((b) |= (1 << (7 - (o))))



static int
pdbheadWrite(PDBHEAD * const pdbheadP,
             FILE *    const fileP) {

    fwrite(pdbheadP->name, 1, 32, fileP);
    pm_writebigshort(fileP, pdbheadP->flags);
    pm_writebigshort(fileP, pdbheadP->version);
    pm_writebiglong(fileP, pdbheadP->ctime);
    pm_writebiglong(fileP, pdbheadP->mtime);
    pm_writebiglong(fileP, pdbheadP->btime);
    pm_writebiglong(fileP, pdbheadP->mod_num);
    pm_writebiglong(fileP, pdbheadP->app_info);
    pm_writebiglong(fileP, pdbheadP->sort_info);
    fwrite(pdbheadP->type, 1, 4,  fileP);
    fwrite(pdbheadP->id,   1, 4,  fileP);
    pm_writebiglong(fileP, pdbheadP->uniq_seed);
    pm_writebiglong(fileP, pdbheadP->next_rec);
    pm_writebigshort(fileP, pdbheadP->num_recs);

    return 0;
}



static int
rechdrWrite(RECHDR * const rechdrP,
            FILE *   const fileP) {

    if (rechdrP) {
        pm_writebiglong(fileP, rechdrP->offset);
        fwrite(rechdrP->unknown,   1, 3, fileP);
        fwrite(&rechdrP->rec_type, 1, 1, fileP);

        if (rechdrP->n_extra != 0)
            fwrite(rechdrP->extra, 1, rechdrP->n_extra, fileP);
    }
    return 0;
}



static void
imageWriteHeader(IMAGE * const imgP,
                 FILE *  const fileP) {

    fwrite(imgP->name,       1, 32, fileP);
    fwrite(&imgP->version,   1,  1, fileP);
    fwrite(&imgP->type,      1,  1, fileP);
    fwrite(imgP->reserved1,  1,  4, fileP);
    fwrite(imgP->note,       1,  4, fileP);
    pm_writebigshort(fileP, imgP->x_last);
    pm_writebigshort(fileP, imgP->y_last);
    fwrite(imgP->reserved2,  1,  4, fileP);
    pm_writebigshort(fileP, imgP->x_anchor);
    pm_writebigshort(fileP, imgP->y_anchor);
    pm_writebigshort(fileP, imgP->width);
    pm_writebigshort(fileP, imgP->height);
}



static void
imageWriteData(IMAGE *         const imgP,
               const uint8_t * const data,
               size_t          const dataSize,
               FILE *          const fileP) {

    fwrite(data, 1,  dataSize, fileP);
}



static void
imageWrite(IMAGE *   const imgP,
           uint8_t * const data,
           size_t    const dataSize,
           FILE *    const fileP) {

    imageWriteHeader(imgP, fileP);

    imageWriteData(imgP, data, dataSize, fileP);
}



static int
textWrite(TEXT * const textP,
          FILE * const fileP) {
    
    if (textP)
        fwrite(textP->data, 1, strlen(textP->data), fileP);

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
putMatch(RLE *  const rleP,
          size_t const n) {

    *rleP->p++ = 0x80 + n - 1;
    *rleP->p++ = rleP->match;
    rleP->used += 2;
    reset(rleP);
}



static void
putLit(RLE *  const rleP,
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

    static void (*put[])(RLE *, size_t) = {putMatch, putLit};
    RLE rle;
    size_t  i;
    const uint8_t * p;

    MEMSZERO(&rle);
    rle.p = out;
    reset(&rle);

    for (i = 0, p = &inData[0]; i < n_in; ++i, ++p) {
        if (*p == rle.match) {
            if (rle.mode == MODE_LIT && rle.len > 1) {
                putLit(&rle, rle.len - 1);
                ++rle.len;
                rle.match = *p;
            }
            rle.mode = MODE_MATCH;
            ++rle.len;
        } else {
            if (rle.mode == MODE_MATCH)
                putMatch(&rle, rle.len);
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



static void
compressIfRequired(IPDB *     const pdbP,
                   int        const comp,
                   uint8_t ** const compressedDataP,
                   size_t *   const compressedSizeP) {

    if (comp == IPDB_NOCOMPRESS) {
        *compressedDataP = pdbP->i->data;
        *compressedSizeP = ipdb_img_size(pdbP->i);
    } else {
        int const uncompressedSz = ipdb_img_size(pdbP->i);
        
        /* Allocate for the worst case. */
        size_t const allocSz = (3 * uncompressedSz + 2)/2;

        uint8_t * data;
            
        data = pdbP->i->data;

        MALLOCARRAY(data, allocSz);
            
        if (data == NULL)
            pm_error("Could not get %lu bytes of memory to decompress",
                     (unsigned long)allocSz);
        else {
            size_t compressedSz;
            compressedSz = compress(pdbP->i->data, uncompressedSz, data);
            if (comp == IPDB_COMPMAYBE && compressedSz >= uncompressedSz) {
                /* Return the uncompressed data */
                free(data);
                *compressedDataP = pdbP->i->data;
                *compressedSizeP = uncompressedSz;
            } else {
                pdbP->i->compressed = TRUE;
                if (pdbP->i->type == IMG_GRAY16)
                    pdbP->i->version = 9;
                else
                    pdbP->i->version = 1;
                if (pdbP->t != NULL)
                    pdbP->t->r->offset -= uncompressedSz - compressedSz;
                *compressedDataP = data;
                *compressedSizeP = compressedSz;
            }
        }
    }
}



static void
ipdbWrite(IPDB * const pdbP,
          int    const comp,
          FILE * const fileP) {

    RECHDR * const trP = pdbP->t == NULL ? NULL : pdbP->t->r;
    RECHDR * const irP = pdbP->i->r;

    int rc;
    uint8_t * compressedData;
        /* This is the image raster, compressed as required.
           (I.e. if it doesn't have to be compressed, it isn't).
        */
    size_t compressedSize;

    assert(pdbP->i);

    compressIfRequired(pdbP, comp, &compressedData, &compressedSize);

    rc = pdbheadWrite(pdbP->p, fileP);
    if (rc != 0)
        pm_error("Failed to write PDB header.  %s", ipdb_err(rc));
            
    rc = rechdrWrite(irP, fileP);
    if (rc != 0)
        pm_error("Failed to write image record header.  %s", ipdb_err(rc));

    rc = rechdrWrite(trP, fileP);
    if (rc != 0)
        pm_error("Failed to write text record header.  %s", ipdb_err(rc));

    imageWrite(pdbP->i, compressedData, compressedSize, fileP);

    rc = textWrite(pdbP->t, fileP);
    if (rc != 0)
        pm_error("Failed to write text.  %s", ipdb_err(rc));

    /* Oh, gross.  compressIfRequired() might have returned a pointer to
       storage that was already allocated, or it might have returned a
       pointer to newly malloc'ed storage.  In the latter case, we have
       to free the storage.
    */
    if (compressedData != pdbP->i->data)
        free(compressedData);
}



static void
g16pack(tuple *         const tupleRow,
        struct pam *    const pamP,
        uint8_t *       const outData,
        unsigned int    const paddedWidth) {
/*----------------------------------------------------------------------------
   Pack a row of 16-level graysacle pixels 'tupleRow', described by *pamP into
   'outData', padding it to 'paddedWidth' with white.

   We pack 2 input pixels into one output byte.
-----------------------------------------------------------------------------*/
    unsigned int col;
    unsigned int off;
    uint8_t * seg;

    for (col = 0, off = 0, seg = &outData[0]; col < paddedWidth; ++col) {
        if (col < pamP->width)
            setg16pixel(*seg, 15 - tupleRow[col][0] * 15 / pamP->maxval, off);
        else
            /* Pad on the right with white */
            setgpixel(*seg, 0, off);

        if (++off == 2) {
            ++seg;
            off = 0;
        }
    }
}



static void
gpack(tuple *         const tupleRow,
      struct pam *    const pamP,
      uint8_t *       const outData,
      unsigned int    const paddedWidth) {
/*----------------------------------------------------------------------------
   Pack a row of 4-level graysacle pixels 'tupleRow', described by *pamP into
   'outData', padding it to 'paddedWidth' with white.

   We pack 4 input pixels into one output byte.
-----------------------------------------------------------------------------*/
    unsigned int col;
    unsigned int off;
    uint8_t * seg;

    for (col = 0, off = 0, seg = &outData[0]; col < paddedWidth; ++col) {
        if (col < pamP->width)
            setgpixel(*seg, 3 - tupleRow[col][0] * 3 / pamP->maxval, off);
        else
            /* Pad on the right with white */
            setgpixel(*seg, 0, off);

        if (++off == 4) {
            ++seg;
            off = 0;
        }
    }
}



static void
mpack(tuple *         const tupleRow,
      struct pam *    const pamP,
      uint8_t *       const outData,
      unsigned int    const paddedWidth) {
/*----------------------------------------------------------------------------
   Pack a row of monochrome pixels 'tupleRow', described by *pamP into
   'outData', padding it to 'paddedWidth' with white.

   We pack 8 input pixels into one output byte.
-----------------------------------------------------------------------------*/
    unsigned int col;
    unsigned int off;
    uint8_t * seg;

    assert(paddedWidth % 8 == 0);

    /* Initialize row to white, then set necessary pixels black */
    memset(outData, 0, paddedWidth/8);

    for (col = 0, off = 0, seg = &outData[0]; col < paddedWidth; ++col) {
        if (col < pamP->width && tupleRow[col][0] == PAM_BLACK)
            setmpixelblack(*seg, off);
        if (++off == 8) {
            ++seg;
            off = 0;
        }
    }
}



static int
adjustDimensions(unsigned int   const w,
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

static void
imageInsertInit(IPDB * const pdbP,
                int    const uw,
                int    const uh,
                int    const type) {

    char * const name = pdbP->p->name;
    unsigned int adjustedWidth, adjustedHeight;

    if (pdbP->p->num_recs != 0)
        pm_error("Image record already present, logic error.");
    else {
        adjustDimensions(uw, uh, &adjustedWidth, &adjustedHeight);
        pm_message("Output dimensions: %uw x %uh",
                   adjustedWidth, adjustedHeight);
        if (adjustedWidth * adjustedHeight > MAX_SIZE(type))
            pm_error("Image too large.   Maximum number of pixels allowed "
                     "for a %s image is %u",
                     ipdb_typeName(type), MAX_SIZE(type));
        else {
            pdbP->i =
                ipdb_image_alloc(name, type, adjustedWidth, adjustedHeight);
            if (pdbP->i == NULL)
                pm_message("Could not get memory for %u x %u image",
                           adjustedWidth, adjustedHeight);
            else
                pdbP->p->num_recs = 1;
        }
    }
}



static void
insertG16image(IPDB *          const pdbP,
               struct pam *    const pamP,
               tuple **        const tuples) {
/*----------------------------------------------------------------------------
   Insert into the PDB an image in 16-level grayscale format.

   The pixels of the image to insert are 'tuples', described by *pamP.
   Note that the image inserted may be padded up to larger dimensions.
-----------------------------------------------------------------------------*/
    imageInsertInit(pdbP, pamP->width, pamP->height, IMG_GRAY16);
    {
        int const rowSize = ipdb_width(pdbP)/2;
            /* The size in bytes of a packed, padded row */

        uint8_t * outP;
        unsigned int row;

        for (row = 0, outP = &pdbP->i->data[0];
             row < pamP->height;
             ++row, outP += rowSize)
            g16pack(tuples[row], pamP, outP, ipdb_width(pdbP));

        /* Pad with white on the bottom */
        for (; row < ipdb_height(pdbP); ++row)
            memset(outP, 0, rowSize);
    } 
}



static void
insertGimage(IPDB *          const pdbP,
             struct pam *    const pamP,
             tuple **        const tuples) {
/*----------------------------------------------------------------------------
   Insert into the PDB an image in 4-level grayscale format.

   The pixels of the image to insert are 'tuples', described by *pamP.
   Note that the image inserted may be padded up to larger dimensions.
-----------------------------------------------------------------------------*/
    imageInsertInit(pdbP, pamP->width, pamP->height, IMG_GRAY);
    {
        int const rowSize = ipdb_width(pdbP)/4;
            /* The size in bytes of a packed, padded row */

        uint8_t * outP;
        unsigned int row;

        for (row = 0, outP = &pdbP->i->data[0];
             row < pamP->height;
             ++row, outP += rowSize)
            gpack(tuples[row], pamP, outP, ipdb_width(pdbP));

        /* Pad with white on the bottom */
        for (; row < ipdb_height(pdbP); ++row)
            memset(outP, 0, rowSize);
    } 
}



static void
insertMimage(IPDB *          const pdbP,
             struct pam *    const pamP,
             tuple **        const tuples) {
/*----------------------------------------------------------------------------
   Insert into the PDB an image in monochrome format.

   The pixels of the image to insert are 'tuples', described by *pamP.
   Note that the image inserted may be padded up to larger dimensions.
-----------------------------------------------------------------------------*/
    imageInsertInit(pdbP, pamP->width, pamP->height, IMG_MONO);
    {
        int const rowSize = ipdb_width(pdbP)/8;
            /* The size in bytes of a packed, padded row */

        uint8_t * outP;
        unsigned int row;

        for (row = 0, outP = &pdbP->i->data[0];
             row < pamP->height;
             ++row, outP += rowSize)
            mpack(tuples[row], pamP, outP, ipdb_width(pdbP));

        /* Pad with white on the bottom */
        for (; row < ipdb_height(pdbP); ++row)
            memset(outP, 0, rowSize);
    } 
}



static int
insertText(IPDB *       const pdbP,
           const char * const s) {

    int retval;

    if (pdbP->i == NULL)
        retval = E_IMAGENOTTHERE;
    else if (pdbP->p->num_recs == 2)
        retval = E_TEXTTHERE;
    else {
        pdbP->t = ipdb_text_alloc(s);
        if (pdbP->t == NULL)
            retval = ENOMEM;
        else {
            pdbP->p->num_recs = 2;

            pdbP->i->r->offset += 8;
            pdbP->t->r->offset =
                pdbP->i->r->offset + IMAGESIZE + ipdb_img_size(pdbP->i);

            retval = 0;
        }
    }
    return retval;
}



static void
readimg(IPDB * const pdbP,
        FILE * const ifP,
        bool   const depth4) {

     struct pam inpam;
    tuple ** tuples;

    tuples = pnm_readpam(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    if (strneq(inpam.tuple_type, "RGB", 3))
        pm_error("Input image is color.  Cannot make a Palm color image.");

    if (inpam.maxval == 1)
        insertMimage(pdbP, &inpam, tuples);
    else if (depth4)
        insertG16image(pdbP, &inpam, tuples);
    else
        insertGimage(pdbP, &inpam, tuples);

    pnm_freepamarray(tuples, &inpam);
}



static void
readtxt(IPDB *       const pdbP,
        const char * const noteFileName) {

    struct stat st;
    char * fileContent;
    FILE * fP;
    int n;
    int rc;
    size_t bytesRead;

    rc = stat(noteFileName, &st);

    if (rc != 0)
        pm_error("stat of '%s' failed, errno = %d (%s)",
                 noteFileName, errno, strerror(errno));

    fP = pm_openr(noteFileName);

    MALLOCARRAY(fileContent, st.st_size + 1);

    if (fileContent == NULL)
        pm_error("Couldn't get %lu bytes of storage to read in note file",
                 (unsigned long) st.st_size);

    bytesRead = fread(fileContent, 1, st.st_size, fP);

    if (bytesRead != st.st_size)
        pm_error("Failed to read note file '%s'.  Errno = %d (%s)",
                 noteFileName, errno, strerror(errno));

    pm_close(fP);

    /* Chop of trailing newlines */
    for (n = strlen(fileContent) - 1; n >= 0 && fileContent[n] == '\n'; --n)
        fileContent[n] = '\0';

    insertText(pdbP, fileContent);
}



int
main(int argc, const char **argv) {

    struct cmdlineInfo cmdline;
    IPDB * pdbP;
    FILE * ifP;
    int comp;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    switch (cmdline.compMode) {
    case COMPRESSED:   comp = IPDB_COMPRESS;   break;
    case UNCOMPRESSED: comp = IPDB_NOCOMPRESS; break;
    case MAYBE:        comp = IPDB_COMPMAYBE;  break;
    }

    pdbP = ipdb_alloc(cmdline.title);

    if (pdbP == NULL)
        pm_error("Failed to allocate IPDB structure");

    readimg(pdbP, ifP, cmdline.depth4);

    if (cmdline.notefile)
        readtxt(pdbP, cmdline.notefile);

    ipdbWrite(pdbP, comp, stdout);

    if (comp == IPDB_COMPMAYBE && !ipdb_compressed(pdbP))
        pm_message("Image too complex to be compressed.");

    ipdb_free(pdbP);

    pm_close(ifP);

    return EXIT_SUCCESS;
}
