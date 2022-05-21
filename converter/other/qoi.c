#include <stdlib.h>
#include <string.h>

#include "qoi.h"

#ifndef QOI_MALLOC
    #define QOI_MALLOC(sz) malloc(sz)
    #define QOI_FREE(p)    free(p)
#endif
#ifndef QOI_ZEROARR
    #define QOI_ZEROARR(a) memset((a),0,sizeof(a))
#endif

#define QOI_OP_INDEX  0x00 /* 00xxxxxx */
#define QOI_OP_DIFF   0x40 /* 01xxxxxx */
#define QOI_OP_LUMA   0x80 /* 10xxxxxx */
#define QOI_OP_RUN    0xc0 /* 11xxxxxx */
#define QOI_OP_RGB    0xfe /* 11111110 */
#define QOI_OP_RGBA   0xff /* 11111111 */

#define QOI_MASK_2    0xc0 /* 11000000 */

#define QOI_COLOR_HASH(C) (C.rgba.r*3 + C.rgba.g*5 + C.rgba.b*7 + C.rgba.a*11)
#define QOI_MAGIC \
    (((unsigned int)'q') << 24 | ((unsigned int)'o') << 16 | \
     ((unsigned int)'i') <<  8 | ((unsigned int)'f'))
#define QOI_HEADER_SIZE 14

/* 2GB is the max file size that this implementation can safely handle. We
   guard against anything larger than that, assuming the worst case with 5
   bytes per pixel, rounded down to a nice clean value. 400 million pixels
   ought to be enough for anybody.
 */
#define QOI_PIXELS_MAX ((unsigned int)400000000)

typedef union {
    struct { unsigned char r, g, b, a; } rgba;
    unsigned int v;
} Rgba;

static unsigned char const padding[8] = {0,0,0,0,0,0,0,1};

static void
write32(unsigned char * const bytes,
        int *           const p,
        unsigned int    const v) {

    bytes[(*p)++] = (0xff000000 & v) >> 24;
    bytes[(*p)++] = (0x00ff0000 & v) >> 16;
    bytes[(*p)++] = (0x0000ff00 & v) >> 8;
    bytes[(*p)++] = (0x000000ff & v);
}



static unsigned int
read32(const unsigned char *bytes, int *p) {

    unsigned int a = bytes[(*p)++];
    unsigned int b = bytes[(*p)++];
    unsigned int c = bytes[(*p)++];
    unsigned int d = bytes[(*p)++];
    return a << 24 | b << 16 | c << 8 | d;
}



void *
qoi_encode(const void *     const data,
           const qoi_Desc * const descP,
           unsigned int *   const outLenP) {

    int p;
    unsigned int i, maxSize, run;
    unsigned int pxLen, pxEnd, pxPos, channelCt;
    unsigned char * bytes;
    const unsigned char * pixels;
    Rgba index[64];
    Rgba px, pxPrev;

    if (data == NULL || outLenP == NULL || descP == NULL ||
        descP->width == 0 || descP->height == 0 ||
        descP->channelCt < 3 || descP->channelCt > 4 ||
        descP->colorspace > 1 ||
        descP->height >= QOI_PIXELS_MAX / descP->width) {

        return NULL;
    }

    maxSize =
        descP->width * descP->height * (descP->channelCt + 1) +
        QOI_HEADER_SIZE + sizeof(padding);

    p = 0;
    bytes = (unsigned char *) QOI_MALLOC(maxSize);
    if (!bytes)
        return NULL;

    write32(bytes, &p, QOI_MAGIC);
    write32(bytes, &p, descP->width);
    write32(bytes, &p, descP->height);
    bytes[p++] = descP->channelCt;
    bytes[p++] = descP->colorspace;

    pixels = (const unsigned char *)data;

    QOI_ZEROARR(index);

    run = 0;
    pxPrev.rgba.r = 0;
    pxPrev.rgba.g = 0;
    pxPrev.rgba.b = 0;
    pxPrev.rgba.a = 255;
    px = pxPrev;

    pxLen = descP->width * descP->height * descP->channelCt;
    pxEnd = pxLen - descP->channelCt;
    channelCt = descP->channelCt;

    for (pxPos = 0; pxPos < pxLen; pxPos += channelCt) {
        px.rgba.r = pixels[pxPos + 0];
        px.rgba.g = pixels[pxPos + 1];
        px.rgba.b = pixels[pxPos + 2];

        if (channelCt == 4) {
            px.rgba.a = pixels[pxPos + 3];
        }

        if (px.v == pxPrev.v) {
            ++run;
            if (run == 62 || pxPos == pxEnd) {
                bytes[p++] = QOI_OP_RUN | (run - 1);
                run = 0;
            }
        } else {
            unsigned int indexPos;

            if (run > 0) {
                bytes[p++] = QOI_OP_RUN | (run - 1);
                run = 0;
            }

            indexPos = QOI_COLOR_HASH(px) % 64;

            if (index[indexPos].v == px.v) {
                bytes[p++] = QOI_OP_INDEX | indexPos;
            } else {
                index[indexPos] = px;

                if (px.rgba.a == pxPrev.rgba.a) {
                    signed char vr = px.rgba.r - pxPrev.rgba.r;
                    signed char vg = px.rgba.g - pxPrev.rgba.g;
                    signed char vb = px.rgba.b - pxPrev.rgba.b;

                    signed char vgR = vr - vg;
                    signed char vgB = vb - vg;

                    if (
                        vr > -3 && vr < 2 &&
                        vg > -3 && vg < 2 &&
                        vb > -3 && vb < 2
                    ) {
                        bytes[p++] = QOI_OP_DIFF |
                            (vr + 2) << 4 | (vg + 2) << 2 | (vb + 2);
                    } else if (
                        vgR >  -9 && vgR <  8 &&
                        vg  > -33 && vg  < 32 &&
                        vgB >  -9 && vgB <  8
                    ) {
                        bytes[p++] = QOI_OP_LUMA     | (vg   + 32);
                        bytes[p++] = (vgR + 8) << 4 | (vgB +  8);
                    } else {
                        bytes[p++] = QOI_OP_RGB;
                        bytes[p++] = px.rgba.r;
                        bytes[p++] = px.rgba.g;
                        bytes[p++] = px.rgba.b;
                    }
                } else {
                    bytes[p++] = QOI_OP_RGBA;
                    bytes[p++] = px.rgba.r;
                    bytes[p++] = px.rgba.g;
                    bytes[p++] = px.rgba.b;
                    bytes[p++] = px.rgba.a;
                }
            }
        }
        pxPrev = px;
    }

    for (i = 0; i < (int)sizeof(padding); ++i)
        bytes[p++] = padding[i];

    *outLenP = p;

    return bytes;
}



void *
qoi_decode(const void * const data,
           unsigned int const size,
           qoi_Desc *   const descP) {

    const unsigned char * bytes;
    unsigned int header_magic;
    unsigned char * pixels;
    Rgba index[64];
    Rgba px;
    unsigned int pxLen, chunksLen, pxPos;
    int p;
    unsigned int run;

    if (data == NULL || descP == NULL ||
        size < QOI_HEADER_SIZE + (int)sizeof(padding)) {
        return NULL;
    }

    bytes = (const unsigned char *)data;

    header_magic      = read32(bytes, &p);
    descP->width      = read32(bytes, &p);
    descP->height     = read32(bytes, &p);
    descP->channelCt  = bytes[p++];
    descP->colorspace = bytes[p++];

    if (
        descP->width == 0 || descP->height == 0 ||
        descP->channelCt < 3 || descP->channelCt > 4 ||
        descP->colorspace > 1 ||
        header_magic != QOI_MAGIC ||
        descP->height >= QOI_PIXELS_MAX / descP->width
    ) {
        return NULL;
    }

    pxLen = descP->width * descP->height * descP->channelCt;
    pixels = (unsigned char *) QOI_MALLOC(pxLen);
    if (!pixels) {
        return NULL;
    }

    QOI_ZEROARR(index);
    px.rgba.r = 0;
    px.rgba.g = 0;
    px.rgba.b = 0;
    px.rgba.a = 255;

    chunksLen = size - sizeof(padding);
    for (pxPos = 0, run = 0; pxPos < pxLen; pxPos += descP->channelCt) {
        if (run > 0) {
            --run;
        } else if (p < chunksLen) {
            unsigned char const b1 = bytes[p++];

            if (b1 == QOI_OP_RGB) {
                px.rgba.r = bytes[p++];
                px.rgba.g = bytes[p++];
                px.rgba.b = bytes[p++];
            } else if (b1 == QOI_OP_RGBA) {
                px.rgba.r = bytes[p++];
                px.rgba.g = bytes[p++];
                px.rgba.b = bytes[p++];
                px.rgba.a = bytes[p++];
            } else if ((b1 & QOI_MASK_2) == QOI_OP_INDEX) {
                px = index[b1];
            } else if ((b1 & QOI_MASK_2) == QOI_OP_DIFF) {
                px.rgba.r += ((b1 >> 4) & 0x03) - 2;
                px.rgba.g += ((b1 >> 2) & 0x03) - 2;
                px.rgba.b += ( b1       & 0x03) - 2;
            } else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA) {
                unsigned char const b2 = bytes[p++];
                unsigned char const vg = (b1 & 0x3f) - 32;
                px.rgba.r += vg - 8 + ((b2 >> 4) & 0x0f);
                px.rgba.g += vg;
                px.rgba.b += vg - 8 +  (b2       & 0x0f);
            } else if ((b1 & QOI_MASK_2) == QOI_OP_RUN) {
                run = (b1 & 0x3f);
            }

            index[QOI_COLOR_HASH(px) % 64] = px;
        }

        pixels[pxPos + 0] = px.rgba.r;
        pixels[pxPos + 1] = px.rgba.g;
        pixels[pxPos + 2] = px.rgba.b;

        if (descP->channelCt == 4)
            pixels[pxPos + 3] = px.rgba.a;
    }

    return pixels;
}



