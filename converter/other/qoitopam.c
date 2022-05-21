/*
 * This file is part of Netpbm (http://netpbm.sourceforge.org).
 * Copyright (c) 2022 cancername.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#include "pam.h"
#include "qoi.h"

#define QOI_MAXVAL 0xFF

/* Resizes buf of type T* so that at least needed+1 bytes
   are available, executing err if realloc fails.
   Uses realloc -- is there a Netpbm equivalent?
*/
#define RESIZE(buf, T, needed, allocated, err)  \
    do {                                        \
    size_t tmpn = (allocated);                  \
    while((needed)+1 >= tmpn)                   \
        tmpn = tmpn ? tmpn * 2 : 8;             \
    if(tmpn != allocated) {                     \
        T *tmpb = realloc((buf), tmpn);         \
        if(!tmpb) {err}                         \
        else {                                  \
            (allocated) = tmpn;                 \
            (buf) = tmpb;                       \
        }                                       \
    }                                           \
    } while(0)


static void
readFile(FILE *                 const fileP,
         const unsigned char ** const bytesP,
         unsigned int *         const sizeP) {

    unsigned char * buf;
    size_t allocatedSz;
    size_t sizeSoFar;
    size_t bytesReadCt;

    buf = NULL; /* initial value */
    allocatedSz = 0;  /* initial value */
    sizeSoFar = 0;  /* initial value */
    *sizeP  = 0;

    bytesReadCt = 0;  /* initial value */
    do {
        sizeSoFar += bytesReadCt;
        RESIZE(buf, unsigned char, sizeSoFar + 4096, allocatedSz, free(buf);
               pm_error("Failed to get memory"););
        bytesReadCt = fread(buf + sizeSoFar, 1, 4096, fileP);
    } while (bytesReadCt != 0);

    if (ferror(fileP)) {
        free(buf);
        pm_error("Failed to read input");
    } else {
        *bytesP = buf;
        *sizeP  = sizeSoFar;
    }
}



int
main(int argc, char **argv) {

    struct pam outpam;

    outpam.size        = sizeof(struct pam);
    outpam.len         = PAM_STRUCT_SIZE(tuple_type);
    outpam.maxval      = QOI_MAXVAL;
    outpam.plainformat = 0;

    qoi_Desc qoiDesc;

    qoiDesc.channelCt = 0;

    pm_proginit(&argc, (const char **)argv);

    unsigned int qoiSz;
    const unsigned char * qoiImg;
    unsigned char * qoiRaster;
    tuple * tuplerow;
    unsigned int qoiRasterCursor;
    unsigned int row;

    /* Unfortunately, qoi.h does not implement a streaming decoder,
       we need to read the whole stream into memory -- expensive.
       We might be able to cheat here with mmap sometimes,
       but it's not worth the effort.
    */
    readFile(stdin, &qoiImg, &qoiSz);

    qoiRaster = qoi_decode(qoiImg, qoiSz, &qoiDesc);

    if (!qoiRaster)
        pm_error("Decoding qoi failed.");

    outpam.depth  = qoiDesc.channelCt == 3 ? 3 : 4;
    outpam.width  = qoiDesc.width;
    outpam.height = qoiDesc.height;
    outpam.format = PAM_FORMAT;
    outpam.file   = stdout;

    if (qoiDesc.channelCt == 3)
        strcpy(outpam.tuple_type, PAM_PPM_TUPLETYPE);
    else
        strcpy(outpam.tuple_type, PAM_PPM_ALPHA_TUPLETYPE);

    pnm_writepaminit(&outpam);

    tuplerow = pnm_allocpamrow(&outpam);

    qoiRasterCursor = 0;  /* initial value */

    for (row = 0; row < outpam.height; ++row) {
        unsigned int col;

        for (col = 0; col < outpam.width; ++col) {
            tuplerow[col][PAM_RED_PLANE] = qoiRaster[qoiRasterCursor++];
            tuplerow[col][PAM_GRN_PLANE] = qoiRaster[qoiRasterCursor++];
            tuplerow[col][PAM_BLU_PLANE] = qoiRaster[qoiRasterCursor++];
            if (outpam.depth > 3)
                tuplerow[col][PAM_TRN_PLANE] = qoiRaster[qoiRasterCursor++];
            }
        pnm_writepamrow(&outpam, tuplerow);
    }

    free((void*)qoiImg);
    free(qoiRaster);
    pnm_freepamrow(tuplerow);

    return 0;
}
