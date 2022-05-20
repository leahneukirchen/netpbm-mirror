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
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include "qoi.h"

#define QOI_MAXVAL 0xFF

/* Resizes buf of type T* so that at least needed+1 bytes
   are available, executing err if realloc fails.
   Uses realloc -- is there a Netpbm equivalent? */
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


/* Unfortunately, qoi.h does not implement a streaming decoder,
   we need to read the whole stream into memory -- expensive.
   We might be able to cheat here with mmap sometimes,
   but it's not worth the effort. */
static void *read_into_mem(FILE *f, size_t *s) {
    *s = 0;
    unsigned char *buf = NULL;
    size_t allocated = 0;
    size_t x = 0;
    size_t r = 0;

    do {
        x += r;
        RESIZE(buf, unsigned char, x+4096, allocated, free(buf); return NULL;);
        r = fread(buf + x, 1, 4096, f);
    } while(r != 0);

    if(ferror(f)) {
        free(buf);
        return NULL;
    }
    else {
        /* buf = realloc(buf, x); */
        *s = x;
        return buf;
    }
}

int main(int argc, char **argv) {
    struct pam output = {
        .size = sizeof(struct pam),
        .len = PAM_STRUCT_SIZE(tuple_type),
        .maxval = QOI_MAXVAL,
        .plainformat = 0
    };

    qoi_desc qd = {
        .channels = 0
    };

    pm_proginit(&argc, (const char **)argv);

    size_t il = 0;
    char *img;
    unsigned char *qoi_buf;
    tuple *tr;

    img = read_into_mem(stdin, &il);

    if(!img || il == 0)
        pm_error("Failed to read qoi into memory.");

    qoi_buf = qoi_decode(img, il, &qd, 0);
    free(img);

    if(!qoi_buf)
        pm_error("Decoding qoi failed.");

    output.depth = qd.channels == 3 ? 3 : 4;
    output.width = qd.width;
    output.height = qd.height;
    output.file = stdout;

    /* Output PPM if the input is RGB only,
       PAM with tuple type RGB_ALPHA otherwise. */
    if(qd.channels == 3) {
        output.format = PPM_FORMAT;
        strcpy(output.tuple_type, PAM_PPM_TUPLETYPE);
    }
    else {
        output.format = PAM_FORMAT;
        strcpy(output.tuple_type, PAM_PPM_ALPHA_TUPLETYPE);
    }
    pnm_writepaminit(&output);
    tr = pnm_allocpamrow(&output);

    size_t k = 0;

    if(output.depth == 3) {
        for(int i = 0; i < output.height; i++) {
            for(int j = 0; j < output.width; j++) {
                tr[j][0] = qoi_buf[k++];
                tr[j][1] = qoi_buf[k++];
                tr[j][2] = qoi_buf[k++];
            }
            pnm_writepamrow(&output, tr);
        }
    }
    else {
        for(int i = 0; i < output.height; i++) {
            for(int j = 0; j < output.width; j++) {
                tr[j][0] = qoi_buf[k++];
                tr[j][1] = qoi_buf[k++];
                tr[j][2] = qoi_buf[k++];
                tr[j][3] = qoi_buf[k++];
            }
            pnm_writepamrow(&output, tr);
        }
    }

    free(qoi_buf);
    pnm_freepamrow(tr);
    return 0;
}
