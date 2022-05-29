/*

QOI - The "Quite OK Image" format for fast, lossless image compression

Dominic Szablewski - https://phoboslab.org


LICENSE: The MIT License(MIT)

Copyright(c) 2021 Dominic Szablewski

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files(the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and / or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions :
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/


#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "pm.h"
#include "mallocvar.h"
#include "qoi.h"

#define ZEROARRAY(a) memset((a),0,sizeof(a))

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
        size_t *        const cursorP,
        unsigned int    const v) {

    bytes[(*cursorP)++] = (0xff000000 & v) >> 24;
    bytes[(*cursorP)++] = (0x00ff0000 & v) >> 16;
    bytes[(*cursorP)++] = (0x0000ff00 & v) >> 8;
    bytes[(*cursorP)++] = (0x000000ff & v);
}



static unsigned int
read32(const unsigned char * const bytes,
       size_t *              const cursorP) {

    unsigned int a = bytes[(*cursorP)++];
    unsigned int b = bytes[(*cursorP)++];
    unsigned int c = bytes[(*cursorP)++];
    unsigned int d = bytes[(*cursorP)++];

    return a << 24 | b << 16 | c << 8 | d;
}



static void
encodeQoiHeader(unsigned char * const bytes,
                qoi_Desc        const qoiDesc,
                size_t *        const cursorP) {

    write32(bytes, cursorP, QOI_MAGIC);
    write32(bytes, cursorP, qoiDesc.width);
    write32(bytes, cursorP, qoiDesc.height);
    bytes[(*cursorP)++] = qoiDesc.channelCt;
    bytes[(*cursorP)++] = qoiDesc.colorspace;
}



static void
encodeNewPixel(Rgba            const px,
               Rgba            const pxPrev,
               unsigned char * const bytes,
               size_t *        const cursorP) {

    if (px.rgba.a == pxPrev.rgba.a) {
        signed char const vr = px.rgba.r - pxPrev.rgba.r;
        signed char const vg = px.rgba.g - pxPrev.rgba.g;
        signed char const vb = px.rgba.b - pxPrev.rgba.b;

        signed char const vgR = vr - vg;
        signed char const vgB = vb - vg;

        if (
            vr > -3 && vr < 2 &&
            vg > -3 && vg < 2 &&
            vb > -3 && vb < 2
            ) {
            bytes[(*cursorP)++] = QOI_OP_DIFF |
                (vr + 2) << 4 | (vg + 2) << 2 | (vb + 2);
        } else if (
            vgR >  -9 && vgR <  8 &&
            vg  > -33 && vg  < 32 &&
            vgB >  -9 && vgB <  8
            ) {
            bytes[(*cursorP)++] = QOI_OP_LUMA     | (vg   + 32);
            bytes[(*cursorP)++] = (vgR + 8) << 4 | (vgB +  8);
        } else {
            bytes[(*cursorP)++] = QOI_OP_RGB;
            bytes[(*cursorP)++] = px.rgba.r;
            bytes[(*cursorP)++] = px.rgba.g;
            bytes[(*cursorP)++] = px.rgba.b;
        }
    } else {
        bytes[(*cursorP)++] = QOI_OP_RGBA;
        bytes[(*cursorP)++] = px.rgba.r;
        bytes[(*cursorP)++] = px.rgba.g;
        bytes[(*cursorP)++] = px.rgba.b;
        bytes[(*cursorP)++] = px.rgba.a;
    }
}



void
qoi_encode(const unsigned char *  const pixels,
           const qoi_Desc *       const descP,
           const unsigned char ** const qoiImageP,
           size_t *               const outLenP) {

    size_t cursor;
    unsigned int i, maxSize, run;
    unsigned int pxLen, pxEnd, pxPos;
    unsigned char * bytes;
    Rgba index[64];
    Rgba px, pxPrev;

    assert(pixels);
    assert(descP);
    assert(outLenP);
    assert(descP->width > 0);
    assert(descP->height > 0);
    assert(descP->channelCt >= 3 && descP->channelCt <= 4);
    assert(descP->colorspace == QOI_SRGB || descP->colorspace == QOI_LINEAR);

    if (descP->height >= QOI_PIXELS_MAX / descP->width)
        pm_error("Too many pixles for OQI: %u x %u (max is %u",
                 descP->height, descP->width, QOI_PIXELS_MAX);

    maxSize =
        descP->width * descP->height * (descP->channelCt + 1) +
        QOI_HEADER_SIZE + sizeof(padding);

    MALLOCARRAY(bytes, maxSize);
    if (!bytes)
        pm_error("Cannot allocate %u bytes", maxSize);

    cursor = 0;

    encodeQoiHeader(bytes, *descP, &cursor);

    ZEROARRAY(index);

    run = 0;
    pxPrev.rgba.r = 0;
    pxPrev.rgba.g = 0;
    pxPrev.rgba.b = 0;
    pxPrev.rgba.a = 255;
    px = pxPrev;

    pxLen = descP->width * descP->height * descP->channelCt;
    pxEnd = pxLen - descP->channelCt;

    for (pxPos = 0; pxPos < pxLen; pxPos += descP->channelCt) {
        px.rgba.r = pixels[pxPos + 0];
        px.rgba.g = pixels[pxPos + 1];
        px.rgba.b = pixels[pxPos + 2];

        if (descP->channelCt == 4) {
            px.rgba.a = pixels[pxPos + 3];
        }

        if (px.v == pxPrev.v) {
            ++run;
            if (run == 62 || pxPos == pxEnd) {
                bytes[cursor++] = QOI_OP_RUN | (run - 1);
                run = 0;
            }
        } else {
            unsigned int const indexPos = QOI_COLOR_HASH(px) % 64;

            if (run > 0) {
                bytes[cursor++] = QOI_OP_RUN | (run - 1);
                run = 0;
            }

            if (index[indexPos].v == px.v) {
                bytes[cursor++] = QOI_OP_INDEX | indexPos;
            } else {
                index[indexPos] = px;

                encodeNewPixel(px, pxPrev, bytes, &cursor);
            }
        }
        pxPrev = px;
    }

    for (i = 0; i < sizeof(padding); ++i)
        bytes[cursor++] = padding[i];

    *qoiImageP = bytes;
    *outLenP   = cursor;
}



static void
decodeQoiHeader(const unsigned char * const qoiImage,
                qoi_Desc *            const qoiDescP,
                size_t *              const cursorP) {

    unsigned int headerMagic;

    headerMagic          = read32(qoiImage, cursorP);
    qoiDescP->width      = read32(qoiImage, cursorP);
    qoiDescP->height     = read32(qoiImage, cursorP);
    qoiDescP->channelCt  = qoiImage[(*cursorP)++];
    qoiDescP->colorspace = qoiImage[(*cursorP)++];

    if (qoiDescP->width == 0)
        pm_error("Invalid QOI image: width is zero");
    if (qoiDescP->height == 0)
        pm_error("Invalid QOI image: height is zero");
    if (qoiDescP->channelCt != 3 && qoiDescP->channelCt != 4)
        pm_error("Invalid QOI image: channel count is %u.  "
                 "Only 3 and 4 are valid", qoiDescP->channelCt);
    if (qoiDescP->colorspace != QOI_SRGB && qoiDescP->colorspace != QOI_LINEAR)
        pm_error("Invalid QOI image: colorspace code is %u.  "
                 "Only %u (SRGB) and %u (LINEAR) are valid",
                 qoiDescP->colorspace, QOI_SRGB, QOI_LINEAR);
    if (headerMagic != QOI_MAGIC)
        pm_error("Invalid QOI image: Where the magic number 0x%04x "
                 "should be, there is 0x%04x",
                 QOI_MAGIC, headerMagic);
    if (qoiDescP->height >= QOI_PIXELS_MAX / qoiDescP->width)
        pm_error ("Invalid QOI image: %u x %u is More than %u pixels",
                  qoiDescP->width, qoiDescP->height, QOI_PIXELS_MAX);
}



void
qoi_decode(const unsigned char *  const qoiImage,
           size_t                 const size,
           qoi_Desc *             const qoiDescP,
           const unsigned char ** const qoiRasterP) {

    unsigned int const chunksLen = size - sizeof(padding);

    unsigned char * pixels;
    Rgba index[64];
    Rgba px;
    unsigned int pxLen, pxPos;
    size_t cursor;
    unsigned int run;

    assert(qoiImage);
    assert(qoiDescP);
    assert(size >= QOI_HEADER_SIZE + sizeof(padding));

    cursor = 0;

    decodeQoiHeader(qoiImage, qoiDescP, &cursor);

    pxLen = qoiDescP->width * qoiDescP->height * qoiDescP->channelCt;
    MALLOCARRAY(pixels, pxLen);
    if (!pixels)
        pm_error("Failed to allocate %u bytes for %u x %u x %u QOI raster",
                 pxLen,
                 qoiDescP->width, qoiDescP->height, qoiDescP->channelCt);

    ZEROARRAY(index);
    px.rgba.r = 0;
    px.rgba.g = 0;
    px.rgba.b = 0;
    px.rgba.a = 255;

    for (pxPos = 0, run = 0; pxPos < pxLen; pxPos += qoiDescP->channelCt) {
        if (run > 0) {
            --run;
        } else if (cursor < chunksLen) {
            unsigned char const b1 = qoiImage[cursor++];

            if (b1 == QOI_OP_RGB) {
                px.rgba.r = qoiImage[cursor++];
                px.rgba.g = qoiImage[cursor++];
                px.rgba.b = qoiImage[cursor++];
            } else if (b1 == QOI_OP_RGBA) {
                px.rgba.r = qoiImage[cursor++];
                px.rgba.g = qoiImage[cursor++];
                px.rgba.b = qoiImage[cursor++];
                px.rgba.a = qoiImage[cursor++];
            } else if ((b1 & QOI_MASK_2) == QOI_OP_INDEX) {
                px = index[b1];
            } else if ((b1 & QOI_MASK_2) == QOI_OP_DIFF) {
                px.rgba.r += ((b1 >> 4) & 0x03) - 2;
                px.rgba.g += ((b1 >> 2) & 0x03) - 2;
                px.rgba.b += ( b1       & 0x03) - 2;
            } else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA) {
                unsigned char const b2 = qoiImage[cursor++];
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

        if (qoiDescP->channelCt == 4)
            pixels[pxPos + 3] = px.rgba.a;
    }

    *qoiRasterP = pixels;
}



