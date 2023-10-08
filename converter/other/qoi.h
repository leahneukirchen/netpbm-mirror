#ifndef QOI_H_INCLUDED
#define QOI_H_INCLUDED
/*

QOI - The "Quite OK Image" format for fast, lossless image compression

Dominic Szablewski - https://phoboslab.org


-- LICENSE: The MIT License(MIT)

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


typedef enum {
    QOI_SRGB = 0,
    QOI_LINEAR = 1
} qoi_Colorspace;


typedef struct {
    unsigned int   width;
    unsigned int   height;
    unsigned int   channelCt;
    qoi_Colorspace colorspace;
} qoi_Desc;



#define QOI_OP_INDEX  0x00 /* 00xxxxxx */
#define QOI_OP_DIFF   0x40 /* 01xxxxxx */
#define QOI_OP_LUMA   0x80 /* 10xxxxxx */
#define QOI_OP_RUN    0xc0 /* 11xxxxxx */
#define QOI_OP_RGB    0xfe /* 11111110 */
#define QOI_OP_RGBA   0xff /* 11111111 */

#define QOI_MASK_2    0xc0 /* 11000000 */

#define QOI_HEADER_SIZE 14

/* 2GB is the max file size that this implementation can safely handle. We
   guard against anything larger than that, assuming the worst case with 5
   bytes per pixel, rounded down to a nice clean value. 400 million pixels
   ought to be enough for anybody.
 */
#define QOI_PIXELS_MAX 400000000

static unsigned int const qoi_pixels_max = (unsigned int) QOI_PIXELS_MAX;

#define QOI_MAXVAL 255

#define QOI_INDEX_SIZE 64


typedef union {
    struct { unsigned char r, g, b, a; } rgba;
    unsigned int v;
} qoi_Rgba;

static __inline__ unsigned int
qoi_colorHash(qoi_Rgba const x) {

    return
        (x.rgba.r*3 + x.rgba.g*5 + x.rgba.b*7 + x.rgba.a*11) % QOI_INDEX_SIZE;
}



static __inline__ void
qoi_clearQoiIndex(qoi_Rgba * index) {

    memset(index, 0, QOI_INDEX_SIZE * sizeof(qoi_Rgba));

}



#define QOI_MAGIC_SIZE 4

static char const qoi_magic[QOI_MAGIC_SIZE + 1] = {'q','o','i','f','\0'};

#define QOI_PADDING_SIZE 8

static unsigned char const qoi_padding[QOI_PADDING_SIZE] = {0,0,0,0,0,0,0,1};


#endif
