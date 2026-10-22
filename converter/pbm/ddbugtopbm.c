/* ddbugtopbm - convert Diddle/DiddleBug sketch DB to PBM files.
 * Copyright (c) 2002 Russell Marks. See COPYING for licence details.
 *
 * The decompression code (uncompress_sketch() is directly from
 * DiddleBug itself, which like ddbugtopbm is distributed under the
 * terms of the GNU GPL. As such, the following copyright applies to
 * uncompress_sketch():
 *
 * Copyright (c) 1999,2000 Mitch Blevins <mblevin@debian.org>.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Adapted to Netpbm by Bryan Henderson 2003.08.09.  Bryan got his copy
 * from ftp:ibiblio.com/pub/linux/apps/graphics/convert, dated 2002.08.21.
 */

#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "pm_c_util.h"
#include "pbm.h"
#include "mallocvar.h"
#include "nstring.h"



static void
readHeader(FILE *         const ifP,
           bool *         const isDiddleP,
           unsigned int * const recCtP) {

    unsigned char buf[64];
    size_t bytesReadCt;

    /* main DB header */
    bytesReadCt = fread(buf, 1, 64, ifP);

    if (bytesReadCt < 64)
        pm_error("Invalid input -- too small to contain the header");

    bytesReadCt = fread(buf, 1, 14, ifP);

    if (bytesReadCt < 14)
        pm_error("Invalid input -- too small to contain the header");

    if (!memeq(buf, "DIDL", 4) && ! memeq(buf, "DIDB", 4))
        pm_error("not a Diddle or DiddleBug DB.  Doesn't contain "
                 "'DIDL' or 'DIBD' signature");

    *isDiddleP = memeq(buf, "DIDL", 4);

    *recCtP = buf[12] * 256 + buf[13];
}



static void
uncompressSketch(const unsigned char * const compressed,
                 unsigned int          const compressedLen,
                 unsigned char *       const uncompressed,
                 unsigned int          const uncompressedSz) {

    /* This is basically UncompressSketch() from DiddleBug 2.50's
       diddlebug.c
    */

    unsigned int inputCursor;
    unsigned int outputCursor;

    for (inputCursor = 0, outputCursor = 0;
         inputCursor < compressedLen;
         ++inputCursor) {

        unsigned int byteCt;

        if (compressed[inputCursor] & 0x80) {
            byteCt = (compressed[inputCursor] & 0x3f) + 1;  /* ~(0x40|0x80) */

            if (outputCursor + byteCt > uncompressedSz)
                pm_error("Record too big for this program to handle");

            if (compressed[inputCursor] & 0x40) {
                /* Mixed */

                memmove(&uncompressed[outputCursor],
                        &compressed[inputCursor + 1],
                        byteCt);

                inputCursor += byteCt;
            } else {
                /* Black */
                memset(&uncompressed[outputCursor], 0xff, byteCt);
            }
        } else {
            /* White */
            byteCt = (compressed[inputCursor] & 0x7f) + 1; /* ~0x80 */

            if (outputCursor + byteCt > uncompressedSz)
                pm_error("Record too big for this program to handle");

            memset(&uncompressed[outputCursor], 0x00, byteCt);
        }
        outputCursor += byteCt;
    }
}



static const char *
makeNoname(void) {

    char name[128];
    unsigned int num;
    FILE * ofP;

    ofP = NULL;
    num = 0;

    do {
        ++num;
        if (ofP != NULL)
            fclose(ofP);
        sprintf(name, "sketch-%04d.pbm", num);
    } while (num < 10000 && (ofP = fopen(name, "rb")) != NULL);

    if (num >= 10000)
        pm_error("too many unnamed sketches!");

    return pm_strdup(name);
}



static void
makeOutFileName(const char *  const name,
                size_t        const nameSz,
                const char ** const outfilenameP) {
/*----------------------------------------------------------------------------
   'name' is what is in the input file where the name field belongs.  It is
   supposed to be a null-terminated ASCII string, but there are 'nameSz' bytes
   physically allocated to it.
-----------------------------------------------------------------------------*/
    unsigned int i;

    char * buffer;

    MALLOCARRAY_NOFAIL(buffer, nameSz + 1);

    for (i = 0; name[i] && i < nameSz; ++i)
        buffer[i] = name[i];
    buffer[i] = '\0';

    for (i = 0; buffer[i]; ++i) {
        if (!isalnum(buffer[i]) &&
            strchr("()-_+=[]:;,.<>?", buffer[i]) == NULL)
            buffer[i] = '_';
        if (isupper(buffer[i]))
            buffer[i] = tolower(buffer[i]);
    }

    if (buffer[0] == '\0') {
        *outfilenameP = makeNoname();
    } else {
        pm_asprintf(outfilenameP, "%s.pbm", buffer);
    }

    free(buffer);
}



static void
writeOneSketch(const unsigned char * const obuf,
               const char *          const outfilename) {

    FILE * ofP;

    ofP = fopen(outfilename,"wb");
    if (!ofP)
        pm_message("WARNING: couldn't open output file '%s'.  "
                   "Carrying on...",
                   outfilename);
    else {
        pbm_writepbminit(ofP, 160, 160, FALSE);
        fwrite(obuf, 1, 160*160/8, ofP);
        fclose(ofP);
    }
}



int
main(int argc, const char ** argv) {

    FILE * const ifP = stdin;

    unsigned char * inbuf;  /* malloced */
    unsigned int * recoffsets;  /* malloced */
    unsigned int f;
    unsigned int recCt;
    bool isDiddle;

    pm_proginit(&argc, argv);

    if (argc-1 > 0)
        pm_error("Program takes no arguments.");

    readHeader(ifP, &isDiddle, &recCt);

    MALLOCARRAY_NOFAIL(recoffsets, recCt);

    for (f = 0; f < recCt; ++f) {
        unsigned char buf[8];
        size_t bytesReadCt;

        bytesReadCt = fread(buf, 1, 8, ifP);

        if (bytesReadCt < 8)
            pm_error("Invalid input -- file ends in the middle of "
                     "record offset table entry for Record %u", f);

        recoffsets[f] =
            (unsigned)(buf[0]<<24) | (buf[1]<<16) | (buf[2]<<8) | (buf[3]<<0);
    }

    for (f = 0; f < recCt; ++f) {
        size_t const inbufSz = 64*1024;
        size_t const obufSz  = 64*1024;

        unsigned char * obuf;  /* malloced */
        const char * name;  /* pointer into inbuf[] */
        const char * outfilename;
        size_t bytesReadCt;
        size_t nameSz;

        MALLOCARRAY(inbuf, inbufSz);
        if (!inbuf) {
            pm_error("Unable to allocate %u bytes for input buffer.",
                     (unsigned)inbufSz);
        }

        MALLOCARRAY(obuf, obufSz);
        if (!obuf) {
            pm_error("Unable to allocate %u bytes for output buffer.",
                     (unsigned)obufSz);
        }

        fseek(ifP, recoffsets[f], SEEK_SET);
        bytesReadCt = fread(inbuf, 1, 16*1024, ifP);  /* XXX crappy! */

        if (isDiddle) {
            /* Diddle */
            unsigned int const sketchLen = 160*160/8;

            if (sketchLen > bytesReadCt) {
                pm_error("Invalid input - too small to contain 160x160 "
                         "uncompressed sketch for record %u", f);
            }
            assert(sketchLen <= obufSz);
            memcpy(obuf, inbuf, sketchLen);
            name = (const char*)&(inbuf[sketchLen]);
            nameSz = bytesReadCt - sketchLen;
        } else {
            /* DiddleBug */
            unsigned int const sketchLen =
                (unsigned int)(inbuf[8] << 8) | (inbuf[9] << 0);

            uncompressSketch(&inbuf[26+80+1], sketchLen-80-1,
                             obuf, obufSz);

            name = (const char*)&inbuf[26+sketchLen];
            nameSz = bytesReadCt - 26 - sketchLen;
        }

        makeOutFileName(name, nameSz, &outfilename);

        pm_message("extracting sketch %2d as `%s'", f, outfilename);

        writeOneSketch(obuf, outfilename);

        pm_strfree(outfilename);

        free(obuf);
    }
    return 0;
}



