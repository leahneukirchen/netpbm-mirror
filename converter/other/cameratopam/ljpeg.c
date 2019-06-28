#define _BSD_SOURCE    /* Make sure string.h containst strcasecmp() */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include "pm.h"

#include "global_variables.h"
#include "util.h"
#include "decode.h"
#include "bayer.h"

#include "ljpeg.h"


/*
   Not a full implementation of Lossless JPEG, just
   enough to decode Canon, Kodak and Adobe DNG images.
 */

int  
ljpeg_start(FILE *         const ifP,
            struct jhead * const jhP) {

    int i, tag;
    unsigned char data[256], *dp;

    init_decoder();
    for (i=0; i < 4; i++)
        jhP->huff[i] = free_decode;
    fread (data, 2, 1, ifP);
    if (data[0] != 0xff || data[1] != 0xd8) return 0;
    do {
        unsigned int len;

        fread (data, 2, 2, ifP);
        tag =  data[0] << 8 | data[1];
        len = data[2] << 8 | data[3];

        if (len < 2)
            pm_error("Length field is %u; must be at least 2", len);
        else {
            unsigned int const dataLen = len - 2;

            if (tag <= 0xff00 || dataLen > 255) return 0;
            fread (data, 1, dataLen, ifP);
            switch (tag) {
            case 0xffc3:
                jhP->bits = data[0];
                jhP->high = data[1] << 8 | data[2];
                jhP->wide = data[3] << 8 | data[4];
                jhP->clrs = data[5];
                break;
            case 0xffc4:
                for (dp = data; dp < data + dataLen && *dp < 4; ) {
                    jhP->huff[*dp] = free_decode;
                    dp = make_decoder (++dp, 0);
                }
            }
        }
    } while (tag != 0xffda);
    jhP->row = calloc (jhP->wide*jhP->clrs, 2);
    if (jhP->row == NULL)
        pm_error("Out of memory in ljpeg_start()");
    for (i=0; i < 4; i++)
        jhP->vpred[i] = 1 << (jhP->bits-1);
    zero_after_ff = 1;
    getbits(ifP, -1);
    return 1;
}



int 
ljpeg_diff(FILE *          const ifP,
           struct decode * const dindexHeadP) {

    int len;
    int diff;
    struct decode * dindexP;

    for (dindexP = dindexHeadP; dindexP->branch[0]; )
        dindexP = dindexP->branch[getbits(ifP, 1)];

    diff = getbits(ifP, len = dindexP->leaf);

    if ((diff & (1 << (len-1))) == 0)
        diff -= (1 << len) - 1;

    return diff;
}



void
ljpeg_row(FILE *         const ifP,
          struct jhead * const jhP) {

    int col, c, diff;
    unsigned short *outp=jhP->row;

    for (col=0; col < jhP->wide; col++)
        for (c=0; c < jhP->clrs; c++) {
            diff = ljpeg_diff(ifP, jhP->huff[c]);
            *outp = col ? outp[-jhP->clrs]+diff : (jhP->vpred[c] += diff);
            outp++;
        }
}



void  
lossless_jpeg_load_raw(Image  const image) {

    int jwide, jrow, jcol, val, jidx, i, row, col;
    struct jhead jh;
    int min=INT_MAX;

    if (!ljpeg_start (ifp, &jh)) return;
    jwide = jh.wide * jh.clrs;

    for (jrow=0; jrow < jh.high; jrow++) {
        ljpeg_row (ifp, &jh);
        for (jcol=0; jcol < jwide; jcol++) {
            val = curve[jh.row[jcol]];
            jidx = jrow*jwide + jcol;
            if (raw_width == 5108) {
                i = jidx / (1680*jh.high);
                if (i < 2) {
                    row = jidx / 1680 % jh.high;
                    col = jidx % 1680 + i*1680;
                } else {
                    jidx -= 2*1680*jh.high;
                    row = jidx / 1748;
                    col = jidx % 1748 + 2*1680;
                }
            } else if (raw_width == 3516) {
                row = jidx / 1758;
                col = jidx % 1758;
                if (row >= raw_height) {
                    row -= raw_height;
                    col += 1758;
                }
            } else {
                row = jidx / raw_width;
                col = jidx % raw_width;
            }
            if ((unsigned) (row-top_margin) >= height) continue;
            if ((unsigned) (col-left_margin) < width) {
                BAYER(row-top_margin,col-left_margin) = val;
                if (min > val) min = val;
            } else
                black += val;
        }
    }
    free (jh.row);
    if (raw_width > width)
        black /= (raw_width - width) * height;
    if (!strcasecmp(make,"KODAK"))
        black = min;
}


