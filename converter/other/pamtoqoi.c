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
#include "pam.h"
#include <string.h>
#include <assert.h>
#include "qoi.h"

#define QOI_MAXVAL 0xFF

/* tuple row to qoi row */
typedef void (*trqr_t)(const tuplen *tuprow,
                       size_t len,
                       unsigned char *qoi_row);

static void trqr_ppm(const tuplen *tuprow,
               size_t len,
               unsigned char *qoi_row) {
    size_t j = 0;
    for(size_t i = 0; i < len; i++) {
        qoi_row[j++] = tuprow[i][0] * QOI_MAXVAL;
        qoi_row[j++] = tuprow[i][1] * QOI_MAXVAL;
        qoi_row[j++] = tuprow[i][2] * QOI_MAXVAL;
    }
}
static void trqr_ppma(const tuplen *tuprow,
               size_t len,
               unsigned char *qoi_row) {
    size_t j = 0;
    for(size_t i = 0; i < len; i++) {
        qoi_row[j++] = tuprow[i][0] * QOI_MAXVAL;
        qoi_row[j++] = tuprow[i][1] * QOI_MAXVAL;
        qoi_row[j++] = tuprow[i][2] * QOI_MAXVAL;
        qoi_row[j++] = tuprow[i][3] * QOI_MAXVAL;
    }
}

static void trqr_pgm(const tuplen *tuprow,
               size_t len,
               unsigned char *qoi_row) {
    size_t j = 0;
    for(size_t i = 0; i < len; i++) {
        unsigned char tmp = tuprow[i][0] * QOI_MAXVAL;
        qoi_row[j++] = tmp;
        qoi_row[j++] = tmp;
        qoi_row[j++] = tmp;
    }
}
static void trqr_pgma(const tuplen *tuprow,
               size_t len,
               unsigned char *qoi_row) {
    size_t j = 0;
    for(size_t i = 0; i < len; i++) {
        unsigned char tmp = tuprow[i][0] * QOI_MAXVAL;
        qoi_row[j++] = tmp;
        qoi_row[j++] = tmp;
        qoi_row[j++] = tmp;
        qoi_row[j++] = tuprow[i][1] * QOI_MAXVAL;
    }
}

static trqr_t get_trqr(const char *tuple_type) {

    if(!strcmp(tuple_type, PAM_PPM_TUPLETYPE))
       return trqr_ppm;

    if(!strcmp(tuple_type, PAM_PPM_ALPHA_TUPLETYPE))
        return trqr_ppma;

    if(!strcmp(tuple_type, PAM_PBM_TUPLETYPE) ||
       !strcmp(tuple_type, PAM_PGM_TUPLETYPE))
        return trqr_pgm;

    if(!strcmp(tuple_type, PAM_PBM_ALPHA_TUPLETYPE) ||
       !strcmp(tuple_type, PAM_PGM_ALPHA_TUPLETYPE))
        return trqr_pgma;
    return NULL;
}

int main(int argc, char **argv) {
    struct pam input;
    trqr_t trqr = NULL;
    qoi_Desc qd = {
        .colorspace = QOI_SRGB
    };
    tuplen *tr = NULL;
    unsigned char *qb = NULL;

    pm_proginit(&argc, (const char **)argv);

    pnm_readpaminit(stdin, &input, PAM_STRUCT_SIZE(tuple_type));
    tr = pnm_allocpamrown(&input);

    qd.width = input.width;
    qd.height = input.height;
    qd.channelCt = input.depth <= 3 ? 3 : 4;

    qb = malloc(qd.width * qd.height * qd.channelCt);

    trqr = get_trqr(input.tuple_type);
    if(!trqr) {
        pm_message("Unknown tuple type. Determining conversion by depth.");
        switch(input.depth) {
            case 1:
            trqr = trqr_pgm;
            pm_message("Conversion: like grayscale");
            break;

            case 2:
            trqr = trqr_pgma;
            pm_message("Conversion: like grayscale_alpha");
            break;

            case 3:
            trqr = trqr_ppm;
            pm_message("Conversion: like rgb");
            break;

            case 4:
            trqr = trqr_ppma;
            pm_message("Conversion: like rgb_alpha");
            break;

            default:
            pm_error("Unsupported depth?");
            break;
        }
    }
    /* Read and convert rows. */
    for(size_t i = 0; i < qd.height; i++) {
        pnm_readpamrown(&input, tr);
        trqr(tr, input.width, qb + i * input.width * qd.channelCt);
    }
    pnm_freepamrown(tr);
    size_t ol;
    unsigned char *buf = qoi_encode(qb, &qd, &ol);
    free(qb);
    fwrite(buf, ol, 1, stdout);
    return 0;
}
