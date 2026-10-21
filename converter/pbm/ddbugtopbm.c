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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "pm_c_util.h"
#include "pbm.h"
#include "mallocvar.h"
#include "nstring.h"



static void
uncompressSketch(const unsigned char * const compressed,
                 unsigned char *       const uncompressed,
                 unsigned int          const compressedLen) {

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
            byteCt = compressed[inputCursor] & 0x3f;  /* ~(0x40|0x80) */
            ++byteCt;
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
            byteCt = compressed[inputCursor] & 0x7f; /* ~0x80 */
            ++byteCt;
            memset(&uncompressed[outputCursor], 0x00, byteCt);
        }
        outputCursor += byteCt;
    }
}



static const char *
makeNoname(void) {

    static char name[128];
    static int num=0;
    FILE * ofP;

    ofP = NULL;

    do {
        ++num;
        if (ofP != NULL)
            fclose(ofP);
        sprintf(name, "sketch-%04d.pbm", num);
    } while (num < 10000 && (ofP = fopen(name, "rb")) != NULL);

    if (num >= 10000)
        pm_error("too many unnamed sketches!");

    return name;
}



static void
makeOutFileName(const char *  const name,
                const char ** const outfilenameP) {

    unsigned int i;

    char * buffer;

    MALLOCARRAY_NOFAIL(buffer, strlen(name) + 1);

    strcpy(buffer, name);

    for (i = 0; buffer[i]; ++i) {
        if (!isalnum(buffer[i]) &&
            strchr("()-_+=[]:;,.<>?", buffer[i]) == NULL)
            buffer[i] = '_';
        if (isupper(buffer[i]))
            buffer[i] = tolower(buffer[i]);
    }

    if (buffer[0] == '\0') {
        *outfilenameP = pm_strdup(makeNoname());
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

    static unsigned char buf[64*1024];
    unsigned int * recoffsets;  /* malloced */
    int f;
    int recCt;
    bool isDiddle;

    pm_proginit(&argc, argv);

    if (argc-1 > 0)
        pm_error("Program takes no arguments.");

    /* main DB header */
    fread(buf, 1, 64, ifP);

    fread(buf, 1, 14, ifP);

    if (!memeq(buf, "DIDL", 4) && ! memeq(buf, "DIDB", 4))
        pm_error("not a Diddle or DiddleBug DB.");

    isDiddle = memeq(buf, "DIDL", 4);

    recCt = buf[12] * 256 + buf[13];

    MALLOCARRAY_NOFAIL(recoffsets, recCt);

    for (f = 0; f < recCt; ++f) {
        unsigned char buf[8];
        fread(buf, 1, 8, ifP);
        recoffsets[f] =
            (unsigned)(buf[0]<<24) | (buf[1]<<16) | (buf[2]<<8) | (buf[3]<<0);
    }

    for (f = 0; f < recCt; ++f) {
        static unsigned char obuf[64*1024];
        char * name;  /* pointer into buf[] */
        const char * outfilename;

        fseek(ifP, recoffsets[f], SEEK_SET);
        fread(buf, 1, 16*1024, ifP);  /* XXX crappy! */

        if (isDiddle) {
            /* Diddle */
            unsigned int const sketchLen = 160*160/8;
            memcpy(obuf, buf, sketchLen);
            name = (char*)&(buf[sketchLen]);
        } else {
            /* DiddleBug */
            unsigned int const sketchLen =
                (unsigned int)(buf[8] << 8) | (buf[9] << 0);

            uncompressSketch(&buf[26+80+1], obuf, sketchLen-80-1);

            name = (char*)&buf[26+sketchLen];
        }

        makeOutFileName(name, &outfilename);

        pm_message("extracting sketch %2d as `%s'", f, outfilename);

        writeOneSketch(obuf, outfilename);

        pm_strfree(outfilename);
    }
    return 0;
}



