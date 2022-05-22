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
#include <string.h>
#include <assert.h>
#include "pam.h"
#include "nstring.h"
#include "qoi.h"

#define QOI_MAXVAL 0xFF

/* tuple row to qoi row */
typedef void Trqr(const tuplen *  const tuplerown,
                  unsigned int    const width,
                  unsigned char * const qoiRow);

static Trqr trqrRgb;

static void
trqrRgb(const tuplen *  const tuplerown,
        unsigned int    const cols,
        unsigned char * const qoiRow) {

    size_t qoiRowCursor;
    unsigned int col;

    for (col = 0, qoiRowCursor = 0; col < cols; ++col) {
        qoiRow[qoiRowCursor++] = tuplerown[col][PAM_RED_PLANE] * QOI_MAXVAL;
        qoiRow[qoiRowCursor++] = tuplerown[col][PAM_GRN_PLANE] * QOI_MAXVAL;
        qoiRow[qoiRowCursor++] = tuplerown[col][PAM_BLU_PLANE] * QOI_MAXVAL;
    }
}



static Trqr trqrRgba;

static void
trqrRgba(const tuplen *  const tuplerown,
         unsigned int    const cols,
         unsigned char * const qoiRow) {

    size_t qoiRowCursor;
    unsigned int col;

    for (col = 0, qoiRowCursor = 0; col < cols; ++col) {
        qoiRow[qoiRowCursor++] = tuplerown[col][PAM_RED_PLANE] * QOI_MAXVAL;
        qoiRow[qoiRowCursor++] = tuplerown[col][PAM_GRN_PLANE] * QOI_MAXVAL;
        qoiRow[qoiRowCursor++] = tuplerown[col][PAM_BLU_PLANE] * QOI_MAXVAL;
        qoiRow[qoiRowCursor++] = tuplerown[col][PAM_TRN_PLANE] * QOI_MAXVAL;
    }
}



static Trqr trqrGray;

static void
trqrGray(const tuplen *  const tuplerown,
         unsigned int    const cols,
         unsigned char * const qoiRow) {

    size_t qoiRowCursor;
    unsigned int col;

    for (col = 0, qoiRowCursor = 0; col < cols; ++col) {
        unsigned char const qoiSample = tuplerown[col][0] * QOI_MAXVAL;

        qoiRow[qoiRowCursor++] = qoiSample;
        qoiRow[qoiRowCursor++] = qoiSample;
        qoiRow[qoiRowCursor++] = qoiSample;
    }
}



static Trqr trqrGrayAlpha;

static void
trqrGrayAlpha(const tuplen *  const tuplerown,
              unsigned int    const cols,
              unsigned char * const qoiRow) {

    size_t qoiRowCursor;
    unsigned int col;

    for (col = 0, qoiRowCursor = 0; col < cols; ++col) {
        unsigned char const qoiSample = tuplerown[col][0] * QOI_MAXVAL;

        qoiRow[qoiRowCursor++] = qoiSample;
        qoiRow[qoiRowCursor++] = qoiSample;
        qoiRow[qoiRowCursor++] = qoiSample;
        qoiRow[qoiRowCursor++] =
            tuplerown[col][PAM_GRAY_TRN_PLANE] * QOI_MAXVAL;
    }
}



static Trqr *
trqrForTupleType(const char * const tupleType) {

    if (streq(tupleType, PAM_PPM_TUPLETYPE))
        return &trqrRgb;
    else if(streq(tupleType, PAM_PPM_ALPHA_TUPLETYPE))
        return &trqrRgba;
    else if(streq(tupleType, PAM_PBM_TUPLETYPE) ||
            streq(tupleType, PAM_PGM_TUPLETYPE))
        return &trqrGray;
    else if(streq(tupleType, PAM_PBM_ALPHA_TUPLETYPE) ||
            streq(tupleType, PAM_PGM_ALPHA_TUPLETYPE))
        return &trqrGrayAlpha;
    else {
        pm_error("Don't know how to convert tuple type '%s'.", tupleType);
        return NULL;  /* Suppress compiler warning */
    }
}



int
main(int argc, char **argv) {

    struct pam inpam;
    Trqr * trqr;
    qoi_Desc qoiDesc;
    tuplen * tuplerown;
    unsigned char * qoiRaster;
    const unsigned char * qoiImage;
    size_t qoiSz;
    unsigned int row;

    pm_proginit(&argc, (const char **)argv);

    pnm_readpaminit(stdin, &inpam, PAM_STRUCT_SIZE(tuple_type));

    tuplerown = pnm_allocpamrown(&inpam);

    qoiDesc.colorspace = QOI_SRGB;
    qoiDesc.width      = inpam.width;
    qoiDesc.height     = inpam.height;
    qoiDesc.channelCt  = inpam.depth <= 3 ? 3 : 4;

    qoiRaster = malloc(qoiDesc.width * qoiDesc.height * qoiDesc.channelCt);

    if (!qoiRaster)
        pm_error("Unable to get memory for QOI raster %u x %u x %u",
                 qoiDesc.width, qoiDesc.height, qoiDesc.channelCt);

    trqr = trqrForTupleType(inpam.tuple_type);

    /* Read and convert rows. */
    for (row = 0; row < inpam.height; ++row) {
        pnm_readpamrown(&inpam, tuplerown);
        trqr(tuplerown,
             inpam.width,
             &qoiRaster[row * inpam.width * qoiDesc.channelCt]);
    }
    qoi_encode(qoiRaster, &qoiDesc, &qoiImage, &qoiSz);

    pm_writefile(stdout, qoiImage, qoiSz);

    free((void*)qoiImage);
    free(qoiRaster);
    pnm_freepamrown(tuplerown);

    return 0;
}



