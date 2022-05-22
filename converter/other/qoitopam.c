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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#include "pm.h"
#include "pam.h"
#include "mallocvar.h"
#include "qoi.h"

#define QOI_MAXVAL 0xFF



static void
readQoi(FILE *                 const ifP,
        qoi_Desc *             const qoiDescP,
        const unsigned char ** const qoiRasterP) {

    size_t qoiSz;
    const unsigned char * qoiImg;

    /* Unfortunately, qoi.h does not implement a streaming decoder,
       we need to read the whole stream into memory -- expensive.
       We might be able to cheat here with mmap sometimes,
       but it's not worth the effort.
    */
    pm_readfile(stdin, &qoiImg, &qoiSz);

    qoi_decode(qoiImg, qoiSz, qoiDescP, qoiRasterP);

    free((void*)qoiImg);
}



static void
writeRaster(struct pam            const outpam,
            const unsigned char * const qoiRaster) {

    unsigned int qoiRasterCursor;
    unsigned int row;
    tuple * tuplerow;

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

    pnm_freepamrow(tuplerow);
}



int
main(int argc, char **argv) {

    const unsigned char * qoiRaster;
    qoi_Desc qoiDesc;
    struct pam outpam;

    pm_proginit(&argc, (const char **)argv);

    outpam.size        = sizeof(struct pam);
    outpam.len         = PAM_STRUCT_SIZE(tuple_type);
    outpam.maxval      = QOI_MAXVAL;
    outpam.plainformat = 0;

    readQoi(stdin, &qoiDesc, &qoiRaster);

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

    writeRaster(outpam, qoiRaster);

    free((void*)qoiRaster);

    return 0;
}
