/*
 * picttoppm.c -- convert a MacIntosh PICT file to PPM format.
 *
 * Copyright 1989,1992,1993 George Phillips
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  This software is provided "as is" without express or
 * implied warranty.
 *
 * George Phillips <phillips@cs.ubc.ca>
 * Department of Computer Science
 * University of British Columbia
 *
 *
 * 2003-02:    Handling for DirectBitsRgn opcode (0x9b) added by 
 *             kabe@sra-tohoku.co.jp.
 *
 * 2004-03-27: Several bugs fixed by Steve Summit, scs@eskimo.com.
 *
 */

#define _XOPEN_SOURCE

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "pm_c_util.h"
#include "ppm.h"
#include "pbmfont.h"
#include "mallocvar.h"
#include "nstring.h"


/*
 * Typical byte, 2 byte and 4 byte integers.
 */
typedef unsigned char Byte;
typedef char SignedByte;
typedef unsigned short Word;
typedef unsigned long Longword;


/*
 * Data structures for QuickDraw (and hence PICT) stuff.
 */

struct Rect {
    Word top;
    Word left;
    Word bottom;
    Word right;
};

struct pixMap {
    struct Rect Bounds;
    Word version;
    Word packType;
    Longword packSize;
    Longword hRes;
    Longword vRes;
    Word pixelType;
    Word pixelSize;
    Word cmpCount;
    Word cmpSize;
    Longword planeBytes;
    Longword pmTable;
    Longword pmReserved;
};

struct RGBColor {
    Word red;
    Word grn;
    Word blu;
};

struct Point {
    Word x;
    Word y;
};

struct Pattern {
    Byte pix[64];
};

struct rgbPlanes {
    Word * red;
    Word * grn;
    Word * blu;
};

struct canvas {
    struct rgbPlanes planes;
};

typedef void (*transfer_func) (struct RGBColor* src, struct RGBColor* dst);

static const char * stage;
static struct Rect picFrame;
static Word rowlen;
static Word collen;
static int verbose;
static int fullres;
static int recognize_comment;

static struct RGBColor black = { 0, 0, 0 };
static struct RGBColor white = { 0xffff, 0xffff, 0xffff };

/* various bits of drawing state */
static struct RGBColor foreground = { 0, 0, 0 };
static struct RGBColor background = { 0xffff, 0xffff, 0xffff };
static struct RGBColor op_color;
static struct Pattern bkpat;
static struct Pattern fillpat;
static struct Rect clip_rect;
static struct Rect cur_rect;
static struct Point current;
static struct Pattern pen_pat;
static Word pen_width;
static Word pen_height;
static Word pen_mode;
static transfer_func pen_trf;
static Word text_font;
static Byte text_face;
static Word text_mode;
static transfer_func text_trf;
static Word text_size;
static struct font* tfont;

/* state for magic printer comments */
static int ps_text;
static Byte ps_just;
static Byte ps_flip;
static Word ps_rotation;
static Byte ps_linespace;
static int ps_cent_x;
static int ps_cent_y;
static int ps_cent_set;

struct raster {
/*----------------------------------------------------------------------------
   An image raster.  May be either truecolor or paletted.

   This is an array of pixels in row-major order, with 'rowSize'
   bytes per row, 'rowCount' high.

   Within a row, pixels go left to right.  The rows go top to bottom.

   Each pixel is either a palette index or an RGB triple, depending on
   the format of the associated PICT.
   
   Each pixel is one byte if the associated PICT has 8 or fewer bits
   per pixel.  If the associated PICT has 16 or 32 bits per pixel, an
   element herein is 2 or 4 bytes, respectively.

   For 16 bits per pixel, the two bytes for each pixel encode RGB values
   as described in decode16().

   For 32 bits per pixel, each row is divided into 4 planes.  Red,
   green, blue, and something else, in that order.  The format of a
   plane is one byte per pixel, left to right.
-----------------------------------------------------------------------------*/
    unsigned char * bytes;  /* malloc'ed */
    unsigned int rowSize;
    unsigned int rowCount;
};



static void
allocateRaster(struct raster * const rasterP,
               unsigned int    const width,
               unsigned int    const height,
               unsigned int    const bitsPerPixel) {
/*----------------------------------------------------------------------------
   Allocate storage for a raster that can contain a 'width' x 'height'
   pixel rectangle read from a PICT image with 'bitsPerPixel' bits
   per pixel.

   Make the space large enough to round the number of pixels up to a
   multiple of 16, because we've seen many images in which the PICT raster
   does contain that much padding on the right.  I don't know why; I could
   understand a multiple of 8, since in 1 bpp image, the smallest unit
   expressable in PICT is 8 pixels.  But why 16?  The images we saw came
   from Adobe Illustrator 10 in March 2007, supplied by
   Guillermo Gómez Valcárcel.
-----------------------------------------------------------------------------*/
    unsigned int const allocWidth = ROUNDUP(width, 16);

    if (width > UINT_MAX/4 - 16)
        pm_error("Width %u pixels too large for arithmetic", width);

    rasterP->rowCount = height;

    switch (bitsPerPixel) {
    case 32:
        /* TODO: I'm still trying to figure out this format.

           My theory today:
           The row data is in plane order (a row consists of red
           plane, then, green, then blue, then some 4th plane).

           The old hack code said 3 bytes per pixel here, and could get
           away with it because it never got to decoding the 4th plane.

           But the new clean code needs to tell it like it is and allocate
           4 bytes per pixel.  If we say 3 bytes per pixel here, we get an
           "invalid PICT" error because the image actually contains 4
           bytes per pixel and as we decompress it, we run out of place
           to put the data.

           We have yet to see if we can properly interpret the data.
        */

        rasterP->rowSize = allocWidth * 4;
        break;
    case 16:
        rasterP->rowSize = allocWidth * 2;
        break;
    case 8:
    case 4:
    case 2:
    case 1:
        rasterP->rowSize = allocWidth * 1;
        break;
    default:
        pm_error("INTERNAL ERROR: impossible bitsPerPixel value in "
                 "unpackbits(): %u", bitsPerPixel);
    }    
    if (UINT_MAX / rasterP->rowSize < rasterP->rowCount)
        pm_error("Arithmetic overflow computing size of %u x %u pixel "
                 "array.", rasterP->rowSize, rasterP->rowCount);

    MALLOCARRAY(rasterP->bytes, rasterP->rowSize * rasterP->rowCount);
    if (rasterP->bytes == NULL)
        pm_error("unable to get memory for %u x %u pixel packbits rectangle",
                 width, height);
}


static void
freeRaster(struct raster const raster) {

    free(raster.bytes);
}


struct blit_info {
    struct Rect        srcRect;
    struct Rect        srcBounds;
    struct raster      srcplane;
    int                pixSize;
    struct Rect        dstRect;
    struct RGBColor *  colorMap;
    int                mode;
    struct blit_info * next;
};

typedef struct {
    struct blit_info * firstP;
    struct blit_info ** connectorP;
    bool unblittableText;
        /* The image contains text opcodes, and we don't know how to put that
           in a blit list (I really don't even know what a blit _is_), so
           the image information here is incomplete.
        */
} blitList;


typedef void (drawFn)(struct canvas *, blitList *, int);

struct opdef {
    const char* name;
    int len;
        /* If non-negative, this is the length of the argument of the
           instruction.  If negative, it has special meaning; WORD_LEN
           is the only value negative value.
        */
    drawFn * impl;
    const char* description;
};

#define WORD_LEN (-1)

/*
 * a table of the first 194(?) opcodes.  The table is too empty.
 *
 * Probably could use an entry specifying if the opcode is valid in version
 * 1, etc.
 */

/* for reserved opcodes of known length */
#define RESERVED_OP(length)                                   \
{ "reserved", (length), NULL, "reserved for Apple use" }

/* for reserved opcodes of length determined by a function */
#define RESERVED_OP_F(skipfunction) \
{ "reserved", NA, (skipfunction), "reserved for Apple use" }

/* seems like RGB colors are 6 bytes, but Apple says they're variable */
/* I'll use 6 for now as I don't care that much. */
#define RGB_LEN (6)


static FILE* ifp;
static int align = 0;



static Byte
readByte(void) {
    int c;

    if ((c = fgetc(ifp)) == EOF)
        pm_error("EOF / read error while %s", stage);

    ++align;
    return c & 255;
}



static Word
readWord(void) {

    Byte const hi = readByte();
    Byte const lo = readByte();

    return (hi << 8) | (lo << 0);
}



static void readPoint(struct Point * const p) {
    p->y = readWord();
    p->x = readWord();
}



static Longword
readLong(void) {
    Word const hi = readWord();
    Word const lo = readWord();

    return (hi << 16) | (lo << 0);
}



static SignedByte
readSignedByte(void) {
    return (SignedByte)readByte();
}



static void 
readShortPoint(struct Point * const p) {
    p->x = readSignedByte();
    p->y = readSignedByte();
}



static void
skip(int const byteCount) {
    static Byte buf[1024];
    int n;

    align += byteCount;

    for (n = byteCount; n > 0; n -= 1024)
        if (fread(buf, n > 1024 ? 1024 : n, 1, ifp) != 1)
            pm_error("EOF / read error while %s", stage);
}



struct const_name {
    int value;
    const char * name;
};

struct const_name const transfer_name[] = {
    { 0,    "srcCopy" },
    { 1,    "srcOr" },
    { 2,    "srcXor" },
    { 3,    "srcBic" },
    { 4,    "notSrcCopy" },
    { 5,    "notSrcOr" },
    { 6,    "notSrcXor" },
    { 7,    "notSrcBic" },
    { 32,   "blend" },
    { 33,   "addPin" },
    { 34,   "addOver" },
    { 35,   "subPin" },
    { 36,   "transparent" },
    { 37,   "adMax" },
    { 38,   "subOver" },
    { 39,   "adMin" },
    { -1,   0 }
};

struct const_name font_name[] = {
    { 0,    "systemFont" },
    { 1,    "applFont" },
    { 2,    "newYork" },
    { 3,    "geneva" },
    { 4,    "monaco" },
    { 5,    "venice" },
    { 6,    "london" },
    { 7,    "athens" },
    { 8,    "sanFran" },
    { 9,    "toronto" },
    { 11,   "cairo" },
    { 12,   "losAngeles" },
    { 20,   "times" },
    { 21,   "helvetica" },
    { 22,   "courier" },
    { 23,   "symbol" },
    { 24,   "taliesin" },
    { -1,   0 }
};

struct const_name ps_just_name[] = {
    { 0,    "no" },
    { 1,    "left" },
    { 2,    "center" },
    { 3,    "right" },
    { 4,    "full" },
    { -1,   0 }
};

struct const_name ps_flip_name[] = {
    { 0,    "no" },
    { 1,    "horizontal" },
    { 2,    "vertical" },
    { -1,   0 }
};



static const char*
const_name(const struct const_name * const table,
           unsigned int              const ct) {

    static char numbuf[32];

    unsigned int i;

    for (i = 0; table[i].name; ++i)
        if (table[i].value == ct)
            return table[i].name;
    
    sprintf(numbuf, "? (%u)", ct);
    return numbuf;
}



static void 
picComment(Word const type, 
           int const length) {

    unsigned int remainingLength;

    switch (type) {
    case 150:
        if (verbose) pm_message("TextBegin");
        if (length >= 6) {
            ps_just = readByte();
            ps_flip = readByte();
            ps_rotation = readWord();
            ps_linespace = readByte();
            remainingLength = length - 5;
            if (recognize_comment)
                ps_text = 1;
            ps_cent_set = 0;
            if (verbose) {
                pm_message("%s justification, %s flip, %d degree rotation, "
                           "%d/2 linespacing",
                           const_name(ps_just_name, ps_just),
                           const_name(ps_flip_name, ps_flip),
                           ps_rotation, ps_linespace);
            }
        } else
            remainingLength = length;
        break;
    case 151:
        if (verbose) pm_message("TextEnd");
        ps_text = 0;
        remainingLength = length;
        break;
    case 152:
        if (verbose) pm_message("StringBegin");
        remainingLength = length;
        break;
    case 153:
        if (verbose) pm_message("StringEnd");
        remainingLength = length;
        break;
    case 154:
        if (verbose) pm_message("TextCenter");
        if (length < 8)
            remainingLength = length;
        else {
            ps_cent_y = readWord();
            if (ps_cent_y > 32767)
                ps_cent_y -= 65536;
            skip(2); /* ignore fractional part */
            ps_cent_x = readWord();
            if (ps_cent_x > 32767)
                ps_cent_x -= 65536;
            skip(2); /* ignore fractional part */
            remainingLength = length - 8;
            if (verbose)
                pm_message("offset %d %d", ps_cent_x, ps_cent_y);
        }
        break;
    case 155:
        if (verbose) pm_message("LineLayoutOff");
        remainingLength = length;
        break;
    case 156:
        if (verbose) pm_message("LineLayoutOn");
        remainingLength = length;
        break;
    case 160:
        if (verbose) pm_message("PolyBegin");
        remainingLength = length;
        break;
    case 161:
        if (verbose) pm_message("PolyEnd");
        remainingLength = length;
        break;
    case 163:
        if (verbose) pm_message("PolyIgnore");
        remainingLength = length;
        break;
    case 164:
        if (verbose) pm_message("PolySmooth");
        remainingLength = length;
        break;
    case 165:
        if (verbose) pm_message("picPlyClo");
        remainingLength = length;
        break;
    case 180:
        if (verbose) pm_message("DashedLine");
        remainingLength = length;
        break;
    case 181:
        if (verbose) pm_message("DashedStop");
        remainingLength = length;
        break;
    case 182:
        if (verbose) pm_message("SetLineWidth");
        remainingLength = length;
        break;
    case 190:
        if (verbose) pm_message("PostScriptBegin");
        remainingLength = length;
        break;
    case 191:
        if (verbose) pm_message("PostScriptEnd");
        remainingLength = length;
        break;
    case 192:
        if (verbose) pm_message("PostScriptHandle");
        remainingLength = length;
        break;
    case 193:
        if (verbose) pm_message("PostScriptFile");
        remainingLength = length;
        break;
    case 194:
        if (verbose) pm_message("TextIsPostScript");
        remainingLength = length;
        break;
    case 195:
        if (verbose) pm_message("ResourcePS");
        remainingLength = length;
        break;
    case 200:
        if (verbose) pm_message("RotateBegin");
        remainingLength = length;
        break;
    case 201:
        if (verbose) pm_message("RotateEnd");
        remainingLength = length;
        break;
    case 202:
        if (verbose) pm_message("RotateCenter");
        remainingLength = length;
        break;
    case 210:
        if (verbose) pm_message("FormsPrinting");
        remainingLength = length;
        break;
    case 211:
        if (verbose) pm_message("EndFormsPrinting");
        remainingLength = length;
        break;
    default:
        if (verbose) pm_message("%d", type);
        remainingLength = length;
        break;
    }
    if (remainingLength > 0)
        skip(remainingLength);
}



static drawFn ShortComment;

static void
ShortComment(struct canvas * const canvasP,
             blitList *      const blitListP,
             int             const version) {

    picComment(readWord(), 0);
}



static drawFn LongComment;

static void
LongComment(struct canvas * const canvasP,
            blitList *      const blitListP,
            int             const version) {

    Word type;

    type = readWord();
    picComment(type, readWord());
}



static drawFn skip_poly_or_region;

static void
skip_poly_or_region(struct canvas * const canvasP,
                    blitList *      const blitListP,
                    int             const version) {

    stage = "skipping polygon or region";
    skip(readWord() - 2);
}


#define NA (0)

#define FNT_BOLD    (1)
#define FNT_ITALIC  (2)
#define FNT_ULINE   (4)
#define FNT_OUTLINE (8)
#define FNT_SHADOW  (16)
#define FNT_CONDENSE    (32)
#define FNT_EXTEND  (64)

/* Some font searching routines */

struct fontinfo {
    int font;
    int size;
    int style;
    char* filename;
    struct font* loaded;
    struct fontinfo* next;
};

static struct fontinfo* fontlist = 0;
static struct fontinfo** fontlist_ins = &fontlist;



static void
tokenize(char *         const s,
         const char **  const vec,
         unsigned int   const vecSize,
         unsigned int * const nTokenP) {

    unsigned int nToken;
    char * p;

    p = &s[0];   /* start at beginning of string */
    nToken = 0;  /* no tokens yet */

    while (*p && nToken < vecSize - 1) {
        if (ISSPACE(*p))
            *p++ = '\0';
        else {
            vec[nToken++] = p;
            /* Skip to next non-space character or end */
            while (*p && !ISSPACE(*p))
                ++p;
        }
    }
    vec[nToken] = NULL;

    *nTokenP = nToken;
}



static void
parseFontLine(const char **      const token,
              struct fontinfo ** const fontinfoPP) {

    struct fontinfo * fontinfoP;

    MALLOCVAR(fontinfoP);
    if (fontinfoP == NULL)
        pm_error("out of memory for font information");
    MALLOCARRAY(fontinfoP->filename, strlen(token[3] + 1));
    if (fontinfoP->filename == NULL)
        pm_error("out of memory for font information file name");

    fontinfoP->font  = atoi(token[0]);
    fontinfoP->size  = atoi(token[1]);
    fontinfoP->style = atoi(token[2]);
    strcpy(fontinfoP->filename, token[3]);
    fontinfoP->loaded = 0;

    *fontinfoPP = fontinfoP;
}



static int 
load_fontdir(const char * const dirfile) {
/*----------------------------------------------------------------------------
   Load the font directory from file named 'dirfile'.  Add its contents
   to the global list of fonts 'fontlist'.
-----------------------------------------------------------------------------*/
    FILE * ifP;
    unsigned int nFont;
    char line[1024]; 

    ifP = pm_openr(dirfile);

    nFont = 0;
    while (fgets(line, 1024, ifP) && nFont < INT_MAX) {
        const char * token[10];
        unsigned int nToken;

        tokenize(line, token, ARRAY_SIZE(token), &nToken);

        if (nToken == 0) {
            /* blank line - ignore */
        } else if (token[0][0] == '#') {
            /* comment - ignore */
        } else if (nToken != 4) {
            /* Unrecognized format - ignore */
        } else {
            struct fontinfo * fontinfoP;

            parseFontLine(token, &fontinfoP);

            fontinfoP->next = 0;
            *fontlist_ins = fontinfoP;
            fontlist_ins = &fontinfoP->next;
            ++nFont;
        }
    }
    pm_close(ifP);

    return nFont;
}



static void
dumpRect(const char * const label,
         struct Rect  const rectangle) {

    pm_message("%s (%u,%u) (%u,%u)",
               label,
               rectangle.left,  rectangle.top,
               rectangle.right, rectangle.bottom);
}



static void
readRect(struct Rect * const r) {

    r->top    = readWord();
    r->left   = readWord();
    r->bottom = readWord();
    r->right  = readWord();

    if (r->top > r->bottom || r->right < r->left)
        dumpRect("Invalid rectangle", *r);

    if (r->top > r->bottom)
        pm_error("Invalid PICT: a rectangle has a top below its bottom");
    if (r->right < r->left)
        pm_error("Invalid PICT: a rectangle has a right edge "
                 "left of its left edge");
}



static int
rectwidth(const struct Rect * const r) {
    return r->right - r->left;
}



static int
rectheight(const struct Rect * const r) {
    return r->bottom - r->top;
}



static bool
rectsamesize(struct Rect const r1, 
             struct Rect const r2) {
    return r1.right - r1.left == r2.right - r2.left &&
           r1.bottom - r1.top == r2.bottom - r2.top ;
}



static void
rectinter(struct Rect   const r1, 
          struct Rect   const r2, 
          struct Rect * const intersectionP) {

    intersectionP->left   = MAX(r1.left,   r2.left);
    intersectionP->top    = MAX(r1.top,    r2.top);
    intersectionP->right  = MIN(r1.right,  r2.right);
    intersectionP->bottom = MIN(r1.bottom, r2.bottom);
}



static void
rectscale(struct Rect * const r, 
          double        const xscale, 
          double        const yscale) {
    r->left *= xscale;
    r->right *= xscale;
    r->top *= yscale;
    r->bottom *= yscale;
}



static void
    initBlitList(blitList * const blitListP) {

    blitListP->firstP          = NULL;
    blitListP->connectorP      = &blitListP->firstP;
    blitListP->unblittableText = false;
}



static void
addBlitList(blitList *        const blitListP,
            struct Rect       const srcRect,
            struct Rect       const srcBounds,
            struct raster     const srcplane,
            int               const pixSize,
            struct Rect       const dstRect,
            struct RGBColor * const colorMap,
            int               const mode) {

    struct blit_info * biP;
    
    MALLOCVAR(biP);
    if (biP == NULL)
        pm_error("out of memory for blit list");
    else {
        biP->srcRect   = srcRect;
        biP->srcBounds = srcBounds;
        biP->srcplane  = srcplane;
        biP->pixSize   = pixSize;
        biP->dstRect   = dstRect;
        biP->colorMap  = colorMap;
        biP->mode      = mode;

        biP->next = NULL;

        *blitListP->connectorP = biP;
        blitListP->connectorP = &biP->next;
    }
}



/* Various transfer functions for blits.
 *
 * Note src[Not]{Or,Xor,Copy} only work if the source pixmap was originally
 * a bitmap.
 * There's also a small bug that the foreground and background colors
 * are not used in a srcCopy; this wouldn't be hard to fix.
 * It IS a problem since the foreground and background colors CAN be changed.
 */

static bool
rgbAllSame(const struct RGBColor * const colorP,
           unsigned int            const value) {

    return (colorP->red == value &&
            colorP->grn == value &&
            colorP->blu == value);
}


static bool
rgbIsWhite(const struct RGBColor * const colorP) {

    return rgbAllSame(colorP, 0xffff);
}

static bool
rgbIsBlack(const struct RGBColor * const colorP) {

    return rgbAllSame(colorP, 0);
}


static void 
srcCopy(struct RGBColor * const src, 
        struct RGBColor * const dst) {

    if (rgbIsBlack(src))
        *dst = foreground;
    else
        *dst = background;
}



static void 
srcOr(struct RGBColor * const src, 
      struct RGBColor * const dst) {
    if (rgbIsBlack(src))
        *dst = foreground;
}



static void 
srcXor(struct RGBColor * const src, 
       struct RGBColor * const dst) {
    dst->red ^= ~src->red;
    dst->grn ^= ~src->grn;
    dst->blu ^= ~src->blu;
}



static void 
srcBic(struct RGBColor * const src, 
       struct RGBColor * const dst) {
    if (rgbIsBlack(src))
        *dst = background;
}



static void 
notSrcCopy(struct RGBColor * const src, 
           struct RGBColor * const dst) {
    if (rgbIsWhite(src))
        *dst = foreground;
    else if (rgbIsBlack(src))
        *dst = background;
}



static void 
notSrcOr(struct RGBColor * const src, 
         struct RGBColor * const dst) {
    if (rgbIsWhite(src))
        *dst = foreground;
}



static void 
notSrcBic(struct RGBColor * const src, 
          struct RGBColor * const dst) {
    if (rgbIsWhite(src))
        *dst = background;
}



static void 
notSrcXor(struct RGBColor * const src, 
          struct RGBColor * const dst) {
    dst->red ^= src->red;
    dst->grn ^= src->grn;
    dst->blu ^= src->blu;
}



static void 
addOver(struct RGBColor * const src, 
        struct RGBColor * const dst) {
    dst->red += src->red;
    dst->grn += src->grn;
    dst->blu += src->blu;
}



static void 
addPin(struct RGBColor * const src, 
       struct RGBColor * const dst) {
    if ((long)dst->red + (long)src->red > (long)op_color.red)
        dst->red = op_color.red;
    else
        dst->red = dst->red + src->red;

    if ((long)dst->grn + (long)src->grn > (long)op_color.grn)
        dst->grn = op_color.grn;
    else
        dst->grn = dst->grn + src->grn;

    if ((long)dst->blu + (long)src->blu > (long)op_color.blu)
        dst->blu = op_color.blu;
    else
        dst->blu = dst->blu + src->blu;
}



static void 
subOver(struct RGBColor * const src, 
        struct RGBColor * const dst) {
    dst->red -= src->red;
    dst->grn -= src->grn;
    dst->blu -= src->blu;
}



/* or maybe its src - dst; my copy of Inside Mac is unclear */


static void 
subPin(struct RGBColor * const src, 
       struct RGBColor * const dst) {
    if ((long)dst->red - (long)src->red < (long)op_color.red)
        dst->red = op_color.red;
    else
        dst->red = dst->red - src->red;

    if ((long)dst->grn - (long)src->grn < (long)op_color.grn)
        dst->grn = op_color.grn;
    else
        dst->grn = dst->grn - src->grn;

    if ((long)dst->blu - (long)src->blu < (long)op_color.blu)
        dst->blu = op_color.blu;
    else
        dst->blu = dst->blu - src->blu;
}



static void 
adMax(struct RGBColor * const src, 
      struct RGBColor * const dst) {
    if (src->red > dst->red) dst->red = src->red;
    if (src->grn > dst->grn) dst->grn = src->grn;
    if (src->blu > dst->blu) dst->blu = src->blu;
}



static void 
adMin(struct RGBColor * const src, 
      struct RGBColor * const dst) {
    if (src->red < dst->red) dst->red = src->red;
    if (src->grn < dst->grn) dst->grn = src->grn;
    if (src->blu < dst->blu) dst->blu = src->blu;
}



static void 
blend(struct RGBColor * const src, 
      struct RGBColor * const dst) {
#define blend_component(cmp)    \
    ((long)src->cmp * (long)op_color.cmp) / 65536 +    \
    ((long)dst->cmp * (long)(65536 - op_color.cmp) / 65536)

    dst->red = blend_component(red);
    dst->grn = blend_component(grn);
    dst->blu = blend_component(blu);
}



static void 
transparent(struct RGBColor * const src, 
            struct RGBColor * const dst) {
    if (src->red != background.red ||
        src->grn != background.grn ||
        src->blu != background.blu) {
        *dst = *src;
    }
}



static transfer_func 
transfer(int const mode) {
    switch (mode) {
    case  0: return srcCopy;
    case  1: return srcOr;
    case  2: return srcXor;
    case  3: return srcBic;
    case  4: return notSrcCopy;
    case  5: return notSrcOr;
    case  6: return notSrcXor;
    case  7: return notSrcBic;
    case 32: return blend;
    case 33: return addPin;
    case 34: return addOver;
    case 35: return subPin;
    case 36: return transparent;
    case 37: return adMax;
    case 38: return subOver;
    case 39: return adMin;
    default:
        if (mode != 0)
            pm_message("no transfer function for code %s, using srcCopy",
                const_name(transfer_name, mode));
        return srcCopy;
    }
}



static pixval
redepth(pixval const c,
        pixval const oldMaxval) {
    
    return ROUNDDIV(c * PPM_MAXMAXVAL, oldMaxval);
}



static struct RGBColor
decode16(unsigned char * const sixteen) {
/*----------------------------------------------------------------------------
   Decode a 16 bit PICT encoding of RGB:

      Bit   0:    nothing
      Bits  1- 5: red
      Bits  6-10: green
      Bits 11-15: blue

   'sixteen' is a two byte array.
-----------------------------------------------------------------------------*/
    struct RGBColor retval;

    retval.red = (sixteen[0] & 0x7c) >> 2;
    retval.grn = (sixteen[0] & 0x03) << 3 | (sixteen[1] & 0xe0) >> 5;
    retval.blu = (sixteen[1] & 0x1f) >> 0;
                
    return retval;
}



static void
doDiffSize(struct Rect       const clipsrc,
           struct Rect       const clipdst,
           int               const pixSize,
           int               const xsize,
           int               const ysize,
           transfer_func     const trf,
           struct RGBColor * const color_map, 
           unsigned char *   const src,
           int               const srcwid, 
           struct rgbPlanes  const dst,
           unsigned int      const dstwid) {

    unsigned int const dstadd = dstwid - xsize;

    FILE * pamscalePipeP;
    const char * command;
    FILE * scaled;
    int cols, rows, format;
    pixval maxval;
    pixel * row;
    pixel * rowp;
    FILE * tempFileP;
    const char * tempFilename;
    Word * reddst;
    Word * grndst;
    Word * bludst;

    reddst = dst.red;  /* initial value */
    grndst = dst.grn;  /* initial value */
    bludst = dst.blu;  /* initial value */

    pm_make_tmpfile(&tempFileP, &tempFilename);

    pm_close(tempFileP);

    pm_asprintf(&command, "pamscale -xsize %d -ysize %d > %s",
                rectwidth(&clipdst), rectheight(&clipdst), tempFilename);

    pm_message("running command '%s'", command);

    pamscalePipeP = popen(command, "w");
    if (pamscalePipeP == NULL)
        pm_error("cannot execute command '%s'  popen() errno = %s (%d)",
                 command, strerror(errno), errno);

    pm_strfree(command);

    fprintf(pamscalePipeP, "P6\n%d %d\n%d\n",
            rectwidth(&clipsrc), rectheight(&clipsrc), PPM_MAXMAXVAL);

    switch (pixSize) {
    case 8: {
        unsigned int rowNumber;
        for (rowNumber = 0; rowNumber < ysize; ++rowNumber) {
            unsigned char * const row = &src[rowNumber * srcwid];
            unsigned int colNumber;
            for (colNumber = 0; colNumber < xsize; ++colNumber) {
                unsigned int const colorIndex = row[colNumber];
                struct RGBColor * const ct = &color_map[colorIndex];
                fputc(redepth(ct->red, 65535L), pamscalePipeP);
                fputc(redepth(ct->grn, 65535L), pamscalePipeP);
                fputc(redepth(ct->blu, 65535L), pamscalePipeP);
            }
        }
    }
    break;
    case 16: {
        unsigned int rowNumber;
        for (rowNumber = 0; rowNumber < ysize; ++rowNumber) {
            unsigned char * const row = &src[rowNumber * srcwid];
            unsigned int colNumber;
            for (colNumber = 0; colNumber < xsize; ++colNumber) {
                struct RGBColor const color = decode16(&row[colNumber * 2]);
                fputc(redepth(color.red, 32), pamscalePipeP);
                fputc(redepth(color.grn, 32), pamscalePipeP);
                fputc(redepth(color.blu, 32), pamscalePipeP);
            }
        }
    }
    break;
    case 32: {
        unsigned int const planeSize = srcwid / 4;
        unsigned int rowNumber;
        
        for (rowNumber = 0; rowNumber < ysize; ++rowNumber) {
            unsigned char * const row = &src[rowNumber * srcwid];
            unsigned char * const redPlane = &row[planeSize * 0];
            unsigned char * const grnPlane = &row[planeSize * 1];
            unsigned char * const bluPlane = &row[planeSize * 2];

            unsigned int colNumber;
            for (colNumber = 0; colNumber < xsize; ++colNumber) {
                fputc(redepth(redPlane[colNumber], 256), pamscalePipeP);
                fputc(redepth(grnPlane[colNumber], 256), pamscalePipeP);
                fputc(redepth(bluPlane[colNumber], 256), pamscalePipeP);
            }
        }
    }
    break;
    }

    if (pclose(pamscalePipeP))
        pm_error("pamscale failed.  pclose() returned Errno %s (%d)",
                 strerror(errno), errno);

    ppm_readppminit(scaled = pm_openr(tempFilename), &cols, &rows,
                    &maxval, &format);
    row = ppm_allocrow(cols);
    /* couldn't hurt to assert cols, rows and maxval... */  

    if (trf == NULL) {
        while (rows-- > 0) {
            unsigned int i;
            ppm_readppmrow(scaled, row, cols, maxval, format);
            for (i = 0, rowp = row; i < cols; ++i, ++rowp) {
                *reddst++ = PPM_GETR(*rowp) * 65536L / (maxval + 1); 
                *grndst++ = PPM_GETG(*rowp) * 65536L / (maxval + 1); 
                *bludst++ = PPM_GETB(*rowp) * 65536L / (maxval + 1); 
            }
            reddst += dstadd;
            grndst += dstadd;
            bludst += dstadd;
        }
    }
    else {
        while (rows-- > 0) {
            unsigned int i;
            ppm_readppmrow(scaled, row, cols, maxval, format);
            for (i = 0, rowp = row; i < cols; i++, rowp++) {
                struct RGBColor dst_c, src_c;
                dst_c.red = *reddst;
                dst_c.grn = *grndst;
                dst_c.blu = *bludst;
                src_c.red = PPM_GETR(*rowp) * 65536L / (maxval + 1); 
                src_c.grn = PPM_GETG(*rowp) * 65536L / (maxval + 1); 
                src_c.blu = PPM_GETB(*rowp) * 65536L / (maxval + 1); 
                (*trf)(&src_c, &dst_c);
                *reddst++ = dst_c.red;
                *grndst++ = dst_c.grn;
                *bludst++ = dst_c.blu;
            }
            reddst += dstadd;
            grndst += dstadd;
            bludst += dstadd;
        }
    }

    pm_close(scaled);
    ppm_freerow(row);
    pm_strfree(tempFilename);
    unlink(tempFilename);
}



static void
getRgb(struct rgbPlanes  const planes,
       unsigned int      const index,
       struct RGBColor * const rgbP) {

    rgbP->red = planes.red[index];
    rgbP->grn = planes.grn[index];
    rgbP->blu = planes.blu[index];
}



static void
putRgb(struct RGBColor  const rgb,
       unsigned int     const index,
       struct rgbPlanes const planes) {

    planes.red[index] = rgb.red;
    planes.grn[index] = rgb.grn;
    planes.blu[index] = rgb.blu;
}



static void
doSameSize(transfer_func           trf,
           int               const pixSize,
           int               const xsize,
           int               const ysize,
           unsigned char *   const src,
           unsigned int      const srcwid,
           struct RGBColor * const color_map,
           struct rgbPlanes  const dst,
           unsigned int      const dstwid) {
/*----------------------------------------------------------------------------
   Transfer pixels from 'src' to 'dst', applying the transfer function
   'trf'.

   'src' has the same format as the 'bytes' member of struct raster.
   'srcwid' is the size in bytes of each row, like raster.rowSize.

   We use only the first 'ysize' rows and only the first 'xsize'
   pixels of each row.

   We really should clean this up so that we can take pixels out of
   the middle of a row and rows out of the middle of the raster.  As
   it stands, Caller achieves the same result by passing as 'src'
   a pointer into the middle of a raster -- the upper left corner of
   the rectangle he wants.  But that is messy and nonobvious.

   Each plane of 'dst' is one word per pixel and contains actual
   colors, never a palette index.  It is an array in row-major order
   with 'dstwid' words per row.
-----------------------------------------------------------------------------*/
    switch (pixSize) {
    case 8: {
        unsigned int rowNumber;

        for (rowNumber = 0; rowNumber < ysize; ++rowNumber) {
            unsigned char * const srcrow = &src[rowNumber * srcwid];
            unsigned int const dstRowCurs = rowNumber * dstwid;

            unsigned int colNumber;
            for (colNumber = 0; colNumber < xsize; ++colNumber) {
                unsigned int const dstCursor = dstRowCurs + colNumber;
                unsigned int const colorIndex = srcrow[colNumber];
                struct RGBColor dstColor;
                getRgb(dst, dstCursor, &dstColor);
                (*trf)(&color_map[colorIndex], &dstColor);
                putRgb(dstColor, dstCursor, dst);
            }
        }
    } break;
    case 16: {
        unsigned int rowNumber;

        for (rowNumber = 0; rowNumber < ysize; ++rowNumber) {
            unsigned char * const row = &src[rowNumber * srcwid];
            unsigned int const dstRowCurs = rowNumber * dstwid;

            unsigned int colNumber;
            for (colNumber = 0; colNumber < xsize; ++colNumber) {
                unsigned int const dstCursor = dstRowCurs + colNumber;
                struct RGBColor const srcColor = decode16(&row[colNumber*2]);
                struct RGBColor dstColor;
                struct RGBColor scaledSrcColor;
                scaledSrcColor.red = srcColor.red << 11;
                scaledSrcColor.grn = srcColor.grn << 11;
                scaledSrcColor.blu = srcColor.blu << 11;
                getRgb(dst, dstCursor, &dstColor);
                (*trf)(&scaledSrcColor, &dstColor);
                putRgb(dstColor, dstCursor, dst);
            }
        }
    } break;
    case 32: {
        unsigned int const planeSize = srcwid / 4;
        unsigned int rowNumber;

        for (rowNumber = 0; rowNumber < ysize; ++rowNumber) {
            unsigned char * const row = &src[rowNumber * srcwid];
            unsigned char * const redPlane = &row[planeSize * 0];
            unsigned char * const grnPlane = &row[planeSize * 1];
            unsigned char * const bluPlane = &row[planeSize * 2];
            unsigned int const dstRowCurs = rowNumber * dstwid;

            unsigned int colNumber;

            for (colNumber = 0; colNumber < xsize; ++colNumber) {
                unsigned int const dstCursor = dstRowCurs + colNumber;
                struct RGBColor srcColor, dstColor;
                getRgb(dst, dstCursor, &dstColor);
                srcColor.red = redPlane[colNumber] << 8;
                srcColor.grn = grnPlane[colNumber] << 8;
                srcColor.blu = bluPlane[colNumber] << 8;
                (*trf)(&srcColor, &dstColor);
                putRgb(dstColor, dstCursor, dst);
            }
        }
    } break;
    default:
        pm_error("Impossible value of pixSize: %u", pixSize);
    }
}



static void
blitIdempotent(unsigned int          const pixSize,
               unsigned int          const xsize,
               unsigned int          const ysize,
               unsigned char *       const src,
               unsigned int          const srcwid,
               struct RGBColor *     const colorMap, 
               struct rgbPlanes      const dst,
               unsigned int          const dstwid) {
/*----------------------------------------------------------------------------
  This is the same as doSameSize(), except optimized for the case that
  the transfer function is idempotent (i.e. it's just a straight copy).
  The original author's comments suggest that this optimization isn't
  all that important -- that he just wrote this first and instead of
  expanding it to handle arbitrary transfer functions, added functions
  for that.
-----------------------------------------------------------------------------*/
    switch (pixSize) {
    case 8: {
        unsigned int rowNumber;
        
        for (rowNumber = 0; rowNumber < ysize; ++rowNumber) {
            unsigned char * const srcrow = &src[rowNumber * srcwid];
            unsigned int const dstRowCurs = rowNumber * dstwid;
            unsigned int colNumber;
            for (colNumber = 0; colNumber < xsize; ++colNumber) {
                unsigned int const dstCursor = dstRowCurs + colNumber;
                struct RGBColor * const ct = colorMap + srcrow[colNumber];
                dst.red[dstCursor] = ct->red;
                dst.grn[dstCursor] = ct->grn;
                dst.blu[dstCursor] = ct->blu;
            }
        }
    } break;
    case 16: {
        unsigned int rowNumber;

        for (rowNumber = 0; rowNumber < ysize; ++rowNumber) {
            unsigned char * const srcrow = &src[rowNumber * srcwid];
            unsigned int const dstRowCurs = rowNumber * dstwid;
            unsigned int colNumber;
            for (colNumber = 0; colNumber < xsize; ++colNumber) {
                unsigned int const dstCursor = dstRowCurs + colNumber;
                struct RGBColor const srcColor =
                    decode16(&srcrow[colNumber * 2]);
                dst.red[dstCursor] = srcColor.red << 11;
                dst.grn[dstCursor] = srcColor.grn << 11;
                dst.blu[dstCursor] = srcColor.blu << 11;
            }
        }
    } break;
    case 32: {
        unsigned int const planeSize = srcwid / 4;
        unsigned int rowNumber;

        for (rowNumber = 0; rowNumber < ysize; ++rowNumber) {
            unsigned char * const srcrow = &src[rowNumber * srcwid];
            unsigned char * const redPlane = &srcrow[planeSize * 0];
            unsigned char * const grnPlane = &srcrow[planeSize * 1];
            unsigned char * const bluPlane = &srcrow[planeSize * 2];
            unsigned int const dstRowCurs = rowNumber * dstwid;

            unsigned int colNumber;
            for (colNumber = 0; colNumber < xsize; ++colNumber) {
                unsigned int const dstCursor = dstRowCurs + colNumber;
                dst.red[dstCursor] = redPlane[colNumber] << 8;
                dst.grn[dstCursor] = grnPlane[colNumber] << 8;
                dst.blu[dstCursor] = bluPlane[colNumber] << 8;
            }
        }
    } break;
    default:
        pm_error("INTERNAL ERROR: invalid bits per pixel (%u) in "
                 "blitIdempotent()", pixSize);
    }
}



static void
doBlit(struct Rect       const srcRect, 
       struct Rect       const dstRect, 
       struct Rect       const srcBounds, 
       struct raster     const srcplane,
       struct Rect       const dstBounds, 
       struct rgbPlanes  const canvasPlanes,
       int               const pixSize, 
       int               const dstwid, 
       struct RGBColor * const color_map, 
       int               const mode) {
/*----------------------------------------------------------------------------
   Transfer some pixels from 'srcplane' to 'canvasPlanes', applying the
   transfer function 'trf'.

   'srcplane' contains the rectangle 'srcBounds' of the image.
   'canvasPlanes' contains the rectangle 'dstRect' of the image.

   Take the rectangle 'srcRect' of the source image and copy it to the
   rectangle 'dstRec' of the destination image.

   Each plane of 'canvasPlanes' is one word per pixel and contains actual
   colors, never a palette index.  It is an array in row-major order
   with 'dstwid' words per row.
-----------------------------------------------------------------------------*/
    unsigned char * src;
    struct rgbPlanes dst;
    int dstoff;
    int xsize;
    int ysize;
    transfer_func trf;

    if (verbose) {
        dumpRect("copying from:", srcRect);
        dumpRect("to:          ", dstRect);
        pm_message("a %u x %u area to a %u x %u area",
                   rectwidth(&srcRect), rectheight(&srcRect),
                   rectwidth(&dstRect), rectheight(&dstRect));
    }

    {
        unsigned int const pkpixsize = pixSize == 16 ? 2 : 1;
        unsigned int const srcRowNumber = srcRect.top - srcBounds.top;
        unsigned int const srcRowOffset =
            (srcRect.left - srcBounds.left) * pkpixsize;
        assert(srcRowNumber < srcplane.rowCount);
        assert(srcRowOffset < srcplane.rowSize);
        src = srcplane.bytes + srcRowNumber * srcplane.rowSize + srcRowOffset;
        xsize = rectwidth(&srcRect);
        ysize = rectheight(&srcRect);
    }

    dstoff = (dstRect.top - dstBounds.top) * dstwid +
        (dstRect.left - dstBounds.left);
    dst.red = canvasPlanes.red + dstoff;
    dst.grn = canvasPlanes.grn + dstoff;
    dst.blu = canvasPlanes.blu + dstoff;

    /* get rid of Text mask mode bit, if (erroneously) set */
    if ((mode & ~64) == 0)
        trf = NULL;    /* optimized srcCopy */
    else
        trf = transfer(mode & ~64);

    if (!rectsamesize(srcRect, dstRect))
        doDiffSize(srcRect, dstRect, pixSize, xsize, ysize,
                   trf, color_map, src, srcplane.rowSize, dst, dstwid);
    else {
        if (trf == NULL)
            blitIdempotent(pixSize, xsize, ysize, src, srcplane.rowSize,
                           color_map, dst, dstwid);
        else
            doSameSize(trf, pixSize, xsize, ysize, src, srcplane.rowSize,
                       color_map, dst, dstwid);
    }
}



static int
blit(struct Rect       const srcRect, 
     struct Rect       const srcBounds, 
     struct raster     const srcplane,
     struct canvas *   const canvasP,
     blitList *        const blitListP,
     int               const pixSize, 
     struct Rect       const dstRect, 
     struct Rect       const dstBounds, 
     int               const dstwid, 
     struct RGBColor * const color_map, 
     int               const mode) {
/*----------------------------------------------------------------------------
   'srcplane' contains the rectangle 'srcBounds' of the image.

   We transfer rectangle 'srcRect' from that.

   if 'blitListP' is non-null, we don't draw anything on 'canvasP'; instead,
   we add to the list *blitlistP a description of what needs to be drawn.
-----------------------------------------------------------------------------*/

    /* I can't tell what the result value of this function is supposed to mean,
       but I found several return statements that did not set it to anything,
       and several calls that examine it.  I'm guessing that "1" is the 
       appropriate thing to return in those cases, so I made it so.
       -Bryan 00.03.02
    */

    int retval;

    if (ps_text)
        retval = 1;
    else {
        /* Almost got it.  Clip source rect with source bounds.
           clip dest rect with dest bounds.
        */
        struct Rect clipsrc;
        struct Rect clipdst;

        rectinter(srcBounds, srcRect, &clipsrc);
        rectinter(dstBounds, dstRect, &clipdst);

        if (blitListP) {
            addBlitList(blitListP,
                        clipsrc, srcBounds, srcplane, pixSize,
                        clipdst, color_map, mode);

            retval = 0;
        } else {
            doBlit(clipsrc, clipdst,
                   srcBounds, srcplane, dstBounds, canvasP->planes,
                   pixSize, dstwid, color_map, mode);

            retval = 1;
        }
    }
    return retval;
}



/* allocation is same for version 1 or version 2.  We are super-duper
 * wasteful of memory for version 1 picts.  Someday, we'll separate
 * things and only allocate a byte per pixel for version 1 (or heck,
 * even only a bit, but that would require even more extra work).
 */

static void 
allocPlanes(unsigned int       const width,
            unsigned int       const height,
            struct rgbPlanes * const planesP) {

    unsigned int const planelen = width * height;

    struct rgbPlanes planes;

    MALLOCARRAY(planes.red, planelen);
    MALLOCARRAY(planes.grn, planelen);
    MALLOCARRAY(planes.blu, planelen);
    if (planes.red == NULL || planes.grn == NULL || planes.blu == NULL)
        pm_error("not enough memory to hold picture");

    /* initialize background to white */
    memset(planes.red, 255, planelen * sizeof(Word));
    memset(planes.grn, 255, planelen * sizeof(Word));
    memset(planes.blu, 255, planelen * sizeof(Word));

    *planesP = planes;
}



static void
freePlanes(struct rgbPlanes const planes) {

    free(planes.red);
    free(planes.grn);
    free(planes.blu);
}



static unsigned char
compact(Word const input) {

    return (input >> 8) & 0xff;
}



static void
reportBlitList(blitList * const blitListP) {

    if (verbose) {
        unsigned int count;
        struct blit_info * biP;

        for (count = 0, biP = blitListP->firstP; biP; biP = biP->next)
            ++count;

        pm_message("# blits: %u", count);
    }
}



static void
doBlitList(struct canvas * const canvasP,
           blitList *      const blitListP) {
/*----------------------------------------------------------------------------
   Do the list of blits *blitListP, drawing on canvas *canvasP.

   We allocate new plane data structures in *canvasP.  We assume it doesn't
   have them already.
-----------------------------------------------------------------------------*/
    struct blit_info * bi;
    int srcwidth, dstwidth, srcheight, dstheight;
    double  scale, scalelow, scalehigh;
    double  xscale = 1.0;
    double  yscale = 1.0;
    double  lowxscale, highxscale, lowyscale, highyscale;
    int     xscalecalc = 0, yscalecalc = 0;

    reportBlitList(blitListP);

    fullres = 0;

    for (bi = blitListP->firstP; bi; bi = bi->next) {
        srcwidth = rectwidth(&bi->srcRect);
        dstwidth = rectwidth(&bi->dstRect);
        srcheight = rectheight(&bi->srcRect);
        dstheight = rectheight(&bi->dstRect);
        if (srcwidth > dstwidth) {
            scalelow  = (double)(srcwidth      ) / (double)dstwidth;
            scalehigh = (double)(srcwidth + 1.0) / (double)dstwidth;
            switch (xscalecalc) {
            case 0:
                lowxscale = scalelow;
                highxscale = scalehigh;
                xscalecalc = 1;
                break;
            case 1:
                if (scalelow < highxscale && scalehigh > lowxscale) {
                    if (scalelow > lowxscale) lowxscale = scalelow;
                    if (scalehigh < highxscale) highxscale = scalehigh;
                }
                else {
                    scale = (lowxscale + highxscale) / 2.0;
                    xscale = (double)srcwidth / (double)dstwidth;
                    if (scale > xscale) xscale = scale;
                    xscalecalc = 2;
                }
                break;
            case 2:
                scale = (double)srcwidth / (double)dstwidth;
                if (scale > xscale) xscale = scale;
                break;
            }
        }

        if (srcheight > dstheight) {
            scalelow =  (double)(srcheight      ) / (double)dstheight;
            scalehigh = (double)(srcheight + 1.0) / (double)dstheight;
            switch (yscalecalc) {
            case 0:
                lowyscale = scalelow;
                highyscale = scalehigh;
                yscalecalc = 1;
                break;
            case 1:
                if (scalelow < highyscale && scalehigh > lowyscale) {
                    if (scalelow > lowyscale) lowyscale = scalelow;
                    if (scalehigh < highyscale) highyscale = scalehigh;
                }
                else {
                    scale = (lowyscale + highyscale) / 2.0;
                    yscale = (double)srcheight / (double)dstheight;
                    if (scale > yscale) yscale = scale;
                    yscalecalc = 2;
                }
                break;
            case 2:
                scale = (double)srcheight / (double)dstheight;
                if (scale > yscale) yscale = scale;
                break;
            }
        }
    }

    if (xscalecalc == 1) {
        if (1.0 >= lowxscale && 1.0 <= highxscale)
            xscale = 1.0;
        else
            xscale = lowxscale;
    }
    if (yscalecalc == 1) {
        if (1.0 >= lowyscale && 1.0 <= lowyscale)
            yscale = 1.0;
        else
            yscale = lowyscale;
    }

    if (xscale != 1.0 || yscale != 1.0) {
        struct blit_info * biP;
        
        for (biP = blitListP->firstP; biP; biP = biP->next)
            rectscale(&biP->dstRect, xscale, yscale);

        pm_message("Scaling output by %f in X and %f in Y", xscale, yscale);
        rectscale(&picFrame, xscale, yscale);
    }

    rowlen = picFrame.right  - picFrame.left;
    collen = picFrame.bottom - picFrame.top;

    allocPlanes(rowlen, collen, &canvasP->planes);

    clip_rect = picFrame;

    for (bi = blitListP->firstP; bi; bi = bi->next) {
        doBlit(bi->srcRect, bi->dstRect,
               bi->srcBounds, bi->srcplane, picFrame, canvasP->planes,
               bi->pixSize, rowlen, bi->colorMap, bi->mode);
    }
}



static void
outputPpm(FILE *           const ofP,
          struct rgbPlanes const planes) {

    unsigned int width;
    unsigned int height;
    pixel * pixelrow;
    unsigned int row;
    unsigned int srcCursor;

    stage = "writing PPM";

    assert(picFrame.right  > picFrame.left);
    assert(picFrame.bottom > picFrame.top);

    width  = picFrame.right  - picFrame.left;
    height = picFrame.bottom - picFrame.top;

    ppm_writeppminit(ofP, width, height, PPM_MAXMAXVAL, 0);
    pixelrow = ppm_allocrow(width);
    srcCursor = 0;
    for (row = 0; row < height; ++row) {
        unsigned int col;
        for (col = 0; col < width; ++col) {
            PPM_ASSIGN(pixelrow[col],
                       compact(planes.red[srcCursor]),
                       compact(planes.grn[srcCursor]),
                       compact(planes.blu[srcCursor])
                );
            ++srcCursor;
        }
        ppm_writeppmrow(ofP, pixelrow, width, PPM_MAXMAXVAL, 0);
    }
}



/*
 * All data in version 2 is 2-byte word aligned.  Odd size data
 * is padded with a null.
 */
static Word
get_op(int const version) {
    if ((align & 1) && version == 2) {
        stage = "aligning for opcode";
        readByte();
    }

    stage = "reading opcode";

    if (version == 1)
        return readByte();
    else
        return readWord();
}



static drawFn ClipRgn;

static void
ClipRgn(struct canvas * const canvasP,
        blitList *      const blitListP,
        int             const version) {

    Word const len = readWord();
        /* Length in bytes of the parameter (including this word) */

    if (len == 10) {    /* null rgn */
        /* Parameter is 2 bytes of length, 8 bytes of rectangle corners */

        /* In March 2011, I saw a supposed PICT file (reported to work with
           Apple pictureViewer) with what looked like signed numbers for the
           rectangle: (-32767,-32767), (32767, 32767).  This code has always
           assumed all words in a PICT are unsigned.  But even when I changed
           it to accept this clip rectangle, this program found the image to
           have an invalid raster.
        */

        readRect(&clip_rect);
        /* XXX should clip this by picFrame */
        if (verbose)
            dumpRect("clipping to", clip_rect);
    } else
        skip(len - 2);
}



static drawFn OpColor;

static void
OpColor(struct canvas * const canvasP,
        blitList *      const blitListP,
        int             const version) {

    op_color.red = readWord();
    op_color.grn = readWord();
    op_color.blu = readWord();
}



static void
readPixmap(struct pixMap * const p) {

    stage = "getting pixMap header";

    readRect(&p->Bounds);
    p->version    = readWord();
    p->packType   = readWord();
    p->packSize   = readLong();
    p->hRes       = readLong();
    p->vRes       = readLong();
    p->pixelType  = readWord();
    p->pixelSize  = readWord();
    p->cmpCount   = readWord();
    p->cmpSize    = readWord();
    p->planeBytes = readLong();
    p->pmTable    = readLong();
    p->pmReserved = readLong();

    if (verbose) {
        pm_message("pixelType: %d", p->pixelType);
        pm_message("pixelSize: %d", p->pixelSize);
        pm_message("cmpCount:  %d", p->cmpCount);
        pm_message("cmpSize:   %d", p->cmpSize);
        if (verbose)
            dumpRect("Bounds:", p->Bounds);
    }

    if (p->pixelType != 0)
        pm_error("sorry, I do only chunky format.  "
                 "This image has pixel type %hu", p->pixelType);
    if (p->cmpCount != 1)
        pm_error("sorry, cmpCount != 1");
    if (p->pixelSize != p->cmpSize)
        pm_error("oops, pixelSize != cmpSize");
}



static struct RGBColor*
readColorTable(void) {
    Longword ctSeed;
    Word ctFlags;
    Word ctSize;
    Word val;
    int i;
    struct RGBColor* color_table;

    stage = "getting color table info";

    ctSeed = readLong();
    ctFlags = readWord();
    ctSize = readWord();

    if (verbose) {
        pm_message("ctSeed:  %ld", ctSeed);
        pm_message("ctFlags: %d", ctFlags);
        pm_message("ctSize:  %d", ctSize);
    }

    stage = "reading color table";

    MALLOCARRAY(color_table, ctSize + 1);
    if (color_table == NULL)
        pm_error("no memory for color table");

    for (i = 0; i <= ctSize; i++) {
        val = readWord();
        /* The indices in a device color table are bogus and usually == 0.
         * so I assume we allocate up the list of colors in order.
         */
        if (ctFlags & 0x8000)
            val = i;
        if (val > ctSize)
            pm_error("pixel value greater than color table size");
        color_table[val].red = readWord();
        color_table[val].grn = readWord();
        color_table[val].blu = readWord();

        if (verbose > 1)
            pm_message("Color %3u: [%u,%u,%u]", val,
                color_table[val].red,
                color_table[val].grn,
                color_table[val].blu);
    }

    return color_table;
}



static void
readBytes(FILE *          const ifP,
          unsigned int    const n, 
          unsigned char * const buf) {

    align += n;

    if (fread(buf, n, 1, ifP) != 1)
        pm_error("EOF / read error while %s", stage);
}



static void
copyFullBytes(unsigned char * const packed,
              unsigned char * const expanded,
              unsigned int    const packedLen) {

    unsigned int i;

    for (i = 0; i < packedLen; ++i)
        expanded[i] = packed[i];
}



static void
expand4Bits(unsigned char * const packed,
            unsigned char * const expanded,
            unsigned int    const packedLen) {

    unsigned int i;
    unsigned char * dst;

    dst = &expanded[0];
    
    for (i = 0; i < packedLen; ++i) {
        *dst++ = (packed[i] >> 4) & 0x0f;
        *dst++ = (packed[i] >> 0) & 0x0f;
    }
}



static void
expand2Bits(unsigned char * const packed,
            unsigned char * const expanded,
            unsigned int    const packedLen) {
        
    unsigned int i;
    unsigned char * dst;

    dst = &expanded[0];
    
    for (i = 0; i < packedLen; ++i) {
        *dst++ = (packed[i] >> 6) & 0x03;
        *dst++ = (packed[i] >> 4) & 0x03;
        *dst++ = (packed[i] >> 2) & 0x03;
        *dst++ = (packed[i] >> 0) & 0x03;
    }
}



static void
expand1Bit(unsigned char * const packed,
           unsigned char * const expanded,
           unsigned int    const packedLen) {

    unsigned int i;
    unsigned char * dst;

    dst = &expanded[0];
    
    for (i = 0; i < packedLen; ++i) {
        *dst++ = (packed[i] >> 7) & 0x01;
        *dst++ = (packed[i] >> 6) & 0x01;
        *dst++ = (packed[i] >> 5) & 0x01;
        *dst++ = (packed[i] >> 4) & 0x01;
        *dst++ = (packed[i] >> 3) & 0x01;
        *dst++ = (packed[i] >> 2) & 0x01;
        *dst++ = (packed[i] >> 1) & 0x01;
        *dst++ = (packed[i] >> 0) & 0x01;
    }
}



static void
unpackBuf(unsigned char *  const packed, 
          unsigned int     const packedLen,
          int              const bitsPerPixel,
          unsigned char ** const expandedP,
          unsigned int *   const expandedLenP) {
/*----------------------------------------------------------------------------
   Expand the bit string 'packed', which is 'packedLen' bytes long
   into an array of bytes, with one byte per pixel.  Each 'bitsPerPixel'
   of 'packed' is a pixel.

   So e.g. if it's 4 bits per pixel and 'packed' is 0xabcdef01, we
   return 0x0a0b0c0d0e0f0001 as *expandedP.

   As a special case, if there are multiple bytes per pixel, we just
   return the exact same bit string.

   *expandedP is static storage.

   'packedLen' must not be greater than 256.
-----------------------------------------------------------------------------*/
    static unsigned char expanded[256 * 8];

    assert(packedLen <= 256);

    switch (bitsPerPixel) {
    case 8:
    case 16:
    case 32:
        assert(packedLen <= sizeof(expanded));
        copyFullBytes(packed, expanded, packedLen);
        *expandedLenP = packedLen;
        break;
    case 4:
        assert(packedLen * 2 <= sizeof(expanded));
        expand4Bits(packed, expanded, packedLen);
        *expandedLenP = packedLen * 2;
        break;
    case 2:
        assert(packedLen * 4 <= sizeof(expanded));
        expand2Bits(packed, expanded, packedLen);
        *expandedLenP = packedLen * 4;
        break;
    case 1:
        assert(packedLen * 8 <= sizeof(expanded));
        expand1Bit(packed, expanded, packedLen);
        *expandedLenP = packedLen * 8;
        break;
    default:
        pm_error("INTERNAL ERROR: bitsPerPixel = %u in unpackBuf",
                 bitsPerPixel);
    }
    *expandedP = expanded;
}



static void
unpackUncompressedBits(FILE *          const ifP,
                       struct raster   const raster,
                       unsigned int    const rowBytes,
                       unsigned int    const bitsPerPixel) {
/*----------------------------------------------------------------------------
   Read the raster from the file into 'raster'.  The data in the file is not
   compressed (but may still be packed multiple pixels per byte).

   In PICT terminology, it appears that compression is called
   "packing" and I don't know what packing is called.  But we don't
   use that confusing terminology in this program, except when talking
   to the user.
-----------------------------------------------------------------------------*/
    unsigned int rowOfRect;
    unsigned char * linebuf;

    MALLOCARRAY(linebuf, rowBytes + 100);
    if (linebuf == NULL)
        pm_error("can't allocate memory for line buffer");

    for (rowOfRect = 0; rowOfRect < raster.rowCount; ++rowOfRect) { 
        unsigned char * bytePixels;
        unsigned int expandedByteCount;
        unsigned char * rasterRow;
        unsigned int i;

        rasterRow = raster.bytes + rowOfRect * raster.rowSize;

        readBytes(ifP, rowBytes, linebuf);

        unpackBuf(linebuf, rowBytes, bitsPerPixel,
                  &bytePixels, &expandedByteCount);

        assert(expandedByteCount <= raster.rowSize);

        for (i = 0; i < expandedByteCount; ++i)
            rasterRow[i] = bytePixels[i];
    }
    free(linebuf);
}



static void
reportValidateCompressedLineLen(unsigned int const row,
                                unsigned int const linelen,
                                unsigned int const rowSize) {
/*----------------------------------------------------------------------------
   'row' is a row number in the raster.

   'linelen' is the number of bytes of PICT that the PICT says hold the
   compressed version of that row.

   'rowSize' is the number of bytes we expect the uncompressed line to
   be (includes pad pixels on the right).
-----------------------------------------------------------------------------*/
    if (verbose > 1)
        pm_message("Row %u: %u-byte compressed line", row, linelen);

    /* When the line length value is garbage, it often causes the program to
       try to read beyond EOF.  To make that failure easier to diagnose,
       we sanity check the line length now.
    */

    /* In the worst case, a pixel is represented by two bytes: a one byte
       repeat count of one followed by a one byte pixel value (the byte could
       be up to 8 pixels) or a one byte block length of one followed by the
       pixel value.  So expansion factor two.
    */

    if (linelen > rowSize * 2)
        pm_error("Invalid PICT: compressed line of %u bytes for Row %u "
                 "is too big "
                 "to represent a %u-byte padded row, even with worse case "
                 "compression.", linelen, row, rowSize);
}



static void
expandRun(unsigned char * const block,
          unsigned int    const blockLimit,
          unsigned int    const bitsPerPixel,
          unsigned char * const expanded,
          unsigned int    const expandedSize,
          unsigned int *  const blockLengthP,
          unsigned int *  const expandedByteCountP) {
/*----------------------------------------------------------------------------
   Expand a run (the data says, "repeat the next pixel N times").

   Return the expanded run as expanded[], which has room for 'expandedSize'
   elements.  Return as *expandedByteCountP the number of elements actually
   returned.
-----------------------------------------------------------------------------*/
    unsigned int const pkpixsize = bitsPerPixel == 16 ? 2 : 1;
        /* The repetition unit size, in bytes.  The run consists of this many
           bytes of packed data repeated the specified number of times.
        */

    if (1 + pkpixsize > blockLimit)
        pm_error("PICT run block runs off the end of its line.  "
                 "Invalid PICT file.");
    else {
        unsigned int const runLength = (block[0] ^ 0xff) + 2;
        
        unsigned int i;
        unsigned char * bytePixels;  /* Points to static storage */
        unsigned int expandedByteCount;
        unsigned int outputCursor;
        
        assert(block[0] & 0x80);  /* It's a run */

        if (verbose > 1)
            pm_message("Block: run of %u packed %u-byte units",
                       runLength, pkpixsize);

        unpackBuf(&block[1], pkpixsize, bitsPerPixel,
                  &bytePixels, &expandedByteCount);

        /* I assume in a legal PICT the run never has padding for the
           case that the run is at the right edge of a row and the
           remaining columns in the row don't fill whole bytes.
           E.g. if there are 25 columns left in the row and 1 bit per
           pixel, we won't see a run of 4 bytes and have to ignore the
           last 7 pixels.  Instead, we'll see a run of 3 bytes
           followed by a non-run block for the remaining pixel.

           That is what I saw in a test image.
        */
        
        if (expandedByteCount * runLength > expandedSize)
            pm_error("Invalid PICT image.  It contains a row with more pixels "
                     "than the width of the rectangle containing it, "
                     "even padded up to a "
                     "multiple of 16 pixels.  Use -verbose to see details.");
        
        outputCursor = 0;
        for (i = 0; i < runLength; ++i) {
            unsigned int j;
            for (j = 0; j < expandedByteCount; ++j)
                expanded[outputCursor++] = bytePixels[j];
        }
        *blockLengthP = 1 + pkpixsize;
        *expandedByteCountP = expandedByteCount * runLength;
    }
}



static void
copyPixelGroup(unsigned char * const block,
               unsigned int    const blockLimit,
               unsigned int    const bitsPerPixel,
               unsigned char * const dest,
               unsigned int    const destSize,
               unsigned int *  const blockLengthP,
               unsigned int *  const rasterBytesGeneratedP) {
/*----------------------------------------------------------------------------
   Copy a group of pixels (the data says, "take the following N pixels").

   Copy them (unpacked) from block block[] to dest[].

   block[] self-describes its length.  Return that length as
   *blockLengthP.

   block[] contains at most 'blockLimit' valid array elements, so if
   the length information in block[] indicates the block is larger
   than that, the block is corrupt.

   Return the number of pixels placed in dest[] as *rasterBytesGeneratedP.

   The output array dest[] has 'destSize' elements of space.  Ignore
   any pixels on the right that won't fit in that.
-----------------------------------------------------------------------------*/
    unsigned int const pkpixsize   = bitsPerPixel == 16 ? 2 : 1;
    unsigned int const groupLen    = block[0] + 1;
    unsigned int const blockLength = 1 + groupLen * pkpixsize;

    if (blockLength > blockLimit)
        pm_error("PICT non-run block (length %u) "
                 "runs off the end of its line.  "
                 "Invalid PICT file.", blockLength);
    else {
        unsigned int i;
        unsigned char * bytePixels;  /* Points to static storage */
        unsigned int bytePixelLen;
        unsigned int rasterBytesGenerated;
        
        assert(blockLimit >= 1);  /* block[0] exists */
        assert((block[0] & 0x80) == 0);  /* It's not a run */
        
        if (verbose > 1)
            pm_message("Block: %u explicit packed %u-byte units",
                       groupLen, pkpixsize);
        
        unpackBuf(&block[1], groupLen * pkpixsize, bitsPerPixel,
                  &bytePixels, &bytePixelLen);

        /* It is normal for the above to return more pixels than there
           are left in the row, because of padding.  E.g. there is one
           pixel left in the row, at one bit per pixel.  But a block
           contains full bytes, so it must contain at least 8 pixels.
           7 of them are padding, which we should ignore.

           BUT: I saw an image in which the block had _two_ data bytes
           (16 pixels) when only 1 pixel remained in the row.  I don't
           understand why, but ignoring the 15 extra seemed to work.
        */
        rasterBytesGenerated = MIN(bytePixelLen, destSize);

        for (i = 0; i < rasterBytesGenerated; ++i)
            dest[i] = bytePixels[i];
        
        *blockLengthP = blockLength;
        *rasterBytesGeneratedP = rasterBytesGenerated;
    }
}



static void
interpretOneRasterBlock(unsigned char * const block,
                        unsigned int    const blockLimit,
                        unsigned int    const bitsPerPixel,
                        unsigned char * const raster,
                        unsigned int    const rasterSize,
                        unsigned int *  const blockLengthP,
                        unsigned int *  const rasterBytesGeneratedP) {
/*----------------------------------------------------------------------------
   Return the pixels described by the PICT block block[], assuming
   the PICT format is 'bitsPerPixel' bits per pixel.

   Return them as raster[], which has 'rasterSize' elements of space.
   Return as *rasterBytesGeneratedP the number of elements actually
   returned.

   block[] self-describes its length, and we return that as
   *blockLengthP.  But there are at most 'blockLimit' bytes there, so
   if block[] claims to be longer than that, some of the block is
   missing (i.e. corrupt PICT).
-----------------------------------------------------------------------------*/
    if (block[0] & 0x80)
        expandRun(block, blockLimit, bitsPerPixel, raster, rasterSize,
                  blockLengthP, rasterBytesGeneratedP);
    else
        copyPixelGroup(block, blockLimit, bitsPerPixel, raster, rasterSize,
                       blockLengthP, rasterBytesGeneratedP);

    assert(*rasterBytesGeneratedP <= rasterSize);
}



static unsigned int const maxPixelBytesPerBlock = 1024;



static void
interpretCompressedLine(unsigned char * const linebuf,
                        unsigned int    const linelen,
                        unsigned char * const rowRaster,
                        unsigned int    const rowSize,
                        unsigned int    const bitsPerPixel) {
/*----------------------------------------------------------------------------
   linebuf[] contains 'linelen' bytes from the PICT image that represents
   one row of the image, in compressed format.  Return the
   uncompressed pixels of that row as rowRaster[].

   rowRaster[] has 'rowSize' bytes of space.  Caller ensures that
   linebuf[] does not contain more pixels than that, unless the PICT
   image from which it comes is corrupt.
-----------------------------------------------------------------------------*/
    unsigned int lineCursor;
        /* Cursor into linebuf[] -- the compressed data */
    unsigned int rasterCursor;
        /* Cursor into rowRaster[] -- the uncompressed data */

    for (lineCursor = 0, rasterCursor = 0; lineCursor < linelen; ) {
        unsigned int blockLength, rasterBytesGenerated;
        
        assert(lineCursor <= linelen);
            
        if (verbose > 2)
            pm_message("At Byte %u of line, Column %u of row",
                       lineCursor, rasterCursor);

        interpretOneRasterBlock(
            &linebuf[lineCursor], linelen - lineCursor,
            bitsPerPixel,
            &rowRaster[rasterCursor], rowSize - rasterCursor,
            &blockLength, &rasterBytesGenerated);
        
        lineCursor += blockLength;
        rasterCursor += rasterBytesGenerated;
        assert(rasterCursor <= rowSize);
    }
    if (verbose > 1)
        pm_message("Got %u pixels for row", rasterCursor);
}


/* There is some confusion about when, in PICT, a line length is one byte and
  when it is two.  An Apple document says it is two bytes when the number of
  pixels in the row, padded, is > 250.  Ppmtopict generated PICTs that way
  until January 2009.  Picttoppm assumed something similar until March 2004:
  It assumed the line length is two bytes when the number of pixels > 250 _or_
  bits per pixel > 8.  But in March 2004, Steve Summit did a bunch of
  experiments on existing PICT files and found that they all worked with the
  rule "pixels per row > 200 => 2 byte line length" and some did not work
  with the original rule.

  So in March 2004, Picttoppm changed to pixels per row > 200.  Ppmtopict
  didn't catch up until January 2009.

  http://developer.apple.com/documentation/mac/QuickDraw/QuickDraw-460.html#HEADING460-0

  Of course, neither 200 nor 250 make any logical sense.  In the worst case,
  you can represent 254 pixels of 8 bpp or less in a 255 byte line.
  In the worst case, you can represent 127 16bpp pixels in a 255 byte line.
  So with 200 being the cutoff, it's actually impossible to represent some 
  16 bpp images with 200 pixels per row.

  We have not been able to find an official spec for PICT.

  Some day, we may have to make a user option for this.
*/


static void
unpackCompressedBits(FILE *          const ifP,
                     struct raster   const raster,
                     unsigned int    const rowBytes,
                     unsigned int    const bitsPerPixel) {
/*----------------------------------------------------------------------------
   Read the raster on file *ifP and place it in 'raster'.

   The data in the file is compressed with run length encoding and
   possibly packed multiple pixels per byte as well.

   In PICT terminology, it appears that compression is called
   "packing" and I don't know what packing is called.  But we don't
   use that confusing terminology in this program, except when talking
   to the user.
-----------------------------------------------------------------------------*/
    unsigned int const llsize = rowBytes > 200 ? 2 : 1;
        /* Width in bytes of the field at the beginning of a line that tells
           how long (in bytes) the line is.  See notes above about this
           computation.
        */
    unsigned int row;
    unsigned char * linebuf;
    unsigned int linebufSize;

    linebufSize = rowBytes;
    MALLOCARRAY(linebuf, linebufSize);
    if (linebuf == NULL)
        pm_error("can't allocate memory for line buffer");

    for (row = 0; row < raster.rowCount; ++row) {
        unsigned char * const rowRaster =
            &raster.bytes[row * raster.rowSize];
        unsigned int linelen;

        if (llsize == 2)
            linelen = readWord();
        else
            linelen = readByte();

        reportValidateCompressedLineLen(row, linelen, raster.rowSize);

        if (linelen > linebufSize) {
            linebufSize = linelen;
            REALLOCARRAY(linebuf, linebufSize);
            if (linebuf == NULL)
                pm_error("can't allocate memory for line buffer");
        }
        readBytes(ifP, linelen, linebuf);

        interpretCompressedLine(linebuf, linelen, rowRaster, raster.rowSize,
                                bitsPerPixel);
    }
    free(linebuf);
}



static void
unpackbits(FILE *           const ifP,
           struct Rect *    const boundsP,
           Word             const rowBytesArg, 
           int              const bitsPerPixel,
           struct raster *  const rasterP) {

    unsigned int const rectHeight = boundsP->bottom - boundsP->top;
    unsigned int const rectWidth  = boundsP->right  - boundsP->left;
    
    struct raster raster;
    unsigned int rowBytes;

    stage = "unpacking packbits";

    if (verbose)
        pm_message("rowBytes = %u, bitsPerPixel = %d",
                   rowBytesArg, bitsPerPixel);
        
    allocateRaster(&raster, rectWidth, rectHeight, bitsPerPixel);

    rowBytes = rowBytesArg ? rowBytesArg : raster.rowSize;

    if (verbose)
        pm_message("raster.rowSize: %u bytes; file row = %u bytes",
                   raster.rowSize, rowBytes);

    if (rowBytes < 8)
        unpackUncompressedBits(ifP, raster, rowBytes, bitsPerPixel);
    else
        unpackCompressedBits(ifP, raster, rowBytes, bitsPerPixel);

    *rasterP = raster;
}



static void
interpretRowBytesWord(Word           const rowBytesWord,
                      bool *         const pixMapP,
                      unsigned int * const rowBytesP) {

    *pixMapP   = !!(rowBytesWord & 0x8000);
    *rowBytesP = rowBytesWord & 0x7fff;

    if (verbose)
        pm_message("PCT says %s, %u bytes per row",
                   *pixMapP ? "pixmap" : "bitmap", *rowBytesP);
}



/* this just skips over a version 2 pattern.  Probably will return
 * a pattern in the fabled complete version.
 */
static void
readPattern(void) {

    Word PatType;

    stage = "Reading a pattern";

    PatType = readWord();

    switch (PatType) {
    case 2:
        skip(8); /* old pattern data */
        skip(5); /* RGB for pattern */
        break;
    case 1: {
        Word rowBytesWord;
        bool pixMap;
        unsigned int rowBytes;
        struct pixMap p;
        struct raster raster;
        struct RGBColor * ct;

        skip(8); /* old pattern data */
        rowBytesWord = readWord();
        interpretRowBytesWord(rowBytesWord, &pixMap, &rowBytes);
        readPixmap(&p);
        ct = readColorTable();
        unpackbits(ifp, &p.Bounds, rowBytes, p.pixelSize, &raster);
        freeRaster(raster);
        free(ct);
    } break;
    default:
        pm_error("unknown pattern type in readPattern");
    }
}



/* these 3 do nothing but skip over their data! */

static drawFn BkPixPat;

static void
BkPixPat(struct canvas * const canvasP,
         blitList *      const blitListP,
         int             const version) {

    readPattern();
}



static drawFn PnPixPat;

static void
PnPixPat(struct canvas * const canvasP,
         blitList *      const blitListP,
         int             const version) {

    readPattern();
}



static drawFn FillPixPat;

static void
FillPixPat(struct canvas * const canvasP,
           blitList *      const blitListP,
           int             const version) {

    readPattern();
}



static void
read8x8Pattern(struct Pattern * const pat) {
    unsigned char buf[8];
    unsigned char * exp;
    unsigned int len;
    unsigned int expandedLen;
    unsigned int i;

    len = 8;  /* initial value */
    readBytes(ifp, len, buf);
    if (verbose) {
        pm_message("pattern: %02x%02x%02x%02x",
                   buf[0], buf[1], buf[2], buf[3]);
        pm_message("pattern: %02x%02x%02x%02x",
            buf[4], buf[5], buf[6], buf[7]);
    }
    unpackBuf(buf, len, 1, &exp, &expandedLen);
    for (i = 0; i < 64; ++i)
        pat->pix[i] = exp[i];
}



static drawFn BkPat;

static void 
BkPat(struct canvas * const canvasP,
      blitList *      const blitListP,
      int             const version) {

    read8x8Pattern(&bkpat);
}



static drawFn PnPat;

static void 
PnPat(struct canvas * const canvasP,
      blitList *      const blitListP,
      int             const version) {

    read8x8Pattern(&pen_pat);
}



static drawFn FillPat;

static void 
FillPat(struct canvas * const canvasP,
        blitList *      const blitListP,
        int             const version) {

    read8x8Pattern(&fillpat);
}



static drawFn PnSize;

static void 
PnSize(struct canvas * const canvasP,
       blitList *      const blitListP,
       int             const version) {

    pen_height = readWord();
    pen_width = readWord();
    if (verbose)
        pm_message("pen size %d x %d", pen_width, pen_height);
}



static drawFn PnSize;

static void 
PnMode(struct canvas * const canvasP,
       blitList *      const blitListP,
       int             const version) {

    pen_mode = readWord();

    if (pen_mode >= 8 && pen_mode < 15)
        pen_mode -= 8;
    if (verbose)
        pm_message("pen transfer mode = %s",
            const_name(transfer_name, pen_mode));
    
    pen_trf = transfer(pen_mode);
}



static void 
readRgb(struct RGBColor * const rgb) {
    rgb->red = readWord();
    rgb->grn = readWord();
    rgb->blu = readWord();
}



static drawFn RGBFgCol;

static void 
RGBFgCol(struct canvas * const canvasP,
         blitList *      const blitListP,
         int             const version) {

    readRgb(&foreground);
    if (verbose)
        pm_message("foreground now [%d,%d,%d]", 
            foreground.red, foreground.grn, foreground.blu);
}



static drawFn RGBBkCol;

static void 
RGBBkCol(struct canvas * const canvasP,
         blitList *      const blitListP,
         int             const version) {

    readRgb(&background);
    if (verbose)
        pm_message("background now [%d,%d,%d]", 
            background.red, background.grn, background.blu);
}



#define PIXEL_INDEX(x,y) ((y) - picFrame.top) * rowlen + (x) - picFrame.left

static void 
draw_pixel(struct canvas *   const canvasP,
           int               const x, 
           int               const y, 
           struct RGBColor * const clr, 
           transfer_func           trf) {

    if (x < clip_rect.left || x >= clip_rect.right ||
        y < clip_rect.top  || y >= clip_rect.bottom) {
    } else {
        unsigned int const i = PIXEL_INDEX(x, y);

        struct RGBColor dst;

        dst.red = canvasP->planes.red[i];
        dst.grn = canvasP->planes.grn[i];
        dst.blu = canvasP->planes.blu[i];
 
        (*trf)(clr, &dst);

        canvasP->planes.red[i] = dst.red;
        canvasP->planes.grn[i] = dst.grn;
        canvasP->planes.blu[i] = dst.blu;
    }
}



static void 
draw_pen_rect(struct canvas * const canvasP,
              struct Rect *   const r) {

    int const rowadd = rowlen - (r->right - r->left);

    int i;
    int x, y;
    struct RGBColor dst;

    i = PIXEL_INDEX(r->left, r->top);  /* initial value */
    
    for (y = r->top; y < r->bottom; y++) {
        for (x = r->left; x < r->right; x++) {
            dst.red = canvasP->planes.red[i];
            dst.grn = canvasP->planes.grn[i];
            dst.blu = canvasP->planes.blu[i];

            if (pen_pat.pix[(x & 7) + (y & 7) * 8])
                (*pen_trf)(&black, &dst);
            else
                (*pen_trf)(&white, &dst);

            canvasP->planes.red[i] = dst.red;
            canvasP->planes.grn[i] = dst.grn;
            canvasP->planes.blu[i] = dst.blu;

            i++;
        }
        i += rowadd;
    }
}



static void 
draw_pen(struct canvas * const canvasP,
         int             const x, 
         int             const y) {

    struct Rect penrect;

    penrect.left = x;
    penrect.right = x + pen_width;
    penrect.top = y;
    penrect.bottom = y + pen_height;

    rectinter(penrect, clip_rect, &penrect);

    draw_pen_rect(canvasP, &penrect);
}

/*
 * Digital Line Drawing
 * by Paul Heckbert
 * from "Graphics Gems", Academic Press, 1990
 */

/*
 * digline: draw digital line from (x1,y1) to (x2,y2),
 * calling a user-supplied procedure at each pixel.
 * Does no clipping.  Uses Bresenham's algorithm.
 *
 * Paul Heckbert    3 Sep 85
 */
static void 
scan_line(struct canvas * const canvasP,
          short           const x1, 
          short           const y1, 
          short           const x2, 
          short           const y2) {

    int d, x, y, ax, ay, sx, sy, dx, dy;

    if (!(pen_width == 0 && pen_height == 0)) {

        dx = x2-x1;  ax = ABS(dx)<<1;  sx = SGN(dx);
        dy = y2-y1;  ay = ABS(dy)<<1;  sy = SGN(dy);

        x = x1;
        y = y1;
        if (ax>ay) {        /* x dominant */
            d = ay-(ax>>1);
            for (;;) {
                draw_pen(canvasP, x, y);
                if (x==x2) return;
                if ((x > rowlen) && (sx > 0)) return;
                if (d>=0) {
                    y += sy;
                    d -= ax;
                }
                x += sx;
                d += ay;
            }
        }
        else {          /* y dominant */
            d = ax-(ay>>1);
            for (;;) {
                draw_pen(canvasP, x, y);
                if (y==y2) return;
                if ((y > collen) && (sy > 0)) return;
                if (d>=0) {
                    x += sx;
                    d -= ay;
                }
                y += sy;
                d += ax;
            }
        }
    }
}



static drawFn Line;

static void 
Line(struct canvas * const canvasP,
     blitList *      const blitListP,
     int             const version) {

  struct Point p1;
  readPoint(&p1);
  readPoint(&current);
  if (verbose)
    pm_message("(%d,%d) to (%d, %d)",
           p1.x,p1.y,current.x,current.y);
  scan_line(canvasP, p1.x,p1.y,current.x,current.y);
}



static drawFn LineFrom;

static void 
LineFrom(struct canvas * const canvasP,
         blitList *      const blitListP,
         int             const version) {

    struct Point p1;
    readPoint(&p1);
    if (verbose)
        pm_message("(%d,%d) to (%d, %d)", current.x, current.y, p1.x, p1.y);

    if (!blitListP)
        scan_line(canvasP, current.x, current.y, p1.x, p1.y);

    current.x = p1.x;
    current.y = p1.y;
}



static drawFn ShortLine;

static void 
ShortLine(struct canvas * const canvasP,
          blitList *      const blitListP,
          int             const version) {

    struct Point p1;
    readPoint(&p1);
    readShortPoint(&current);
    if (verbose)
        pm_message("(%d,%d) delta (%d, %d)", p1.x, p1.y, current.x, current.y);
    current.x += p1.x;
    current.y += p1.y;
    
    if (!blitListP)
        scan_line(canvasP, p1.x, p1.y, current.x, current.y);
}



static drawFn ShortLineFrom;

static void 
ShortLineFrom(struct canvas * const canvasP,
              blitList *      const blitListP,
              int             const version) {

    struct Point p1;
    readShortPoint(&p1);
    if (verbose)
        pm_message("(%d,%d) delta (%d, %d)",
                   current.x,current.y,p1.x,p1.y);
    p1.x += current.x;
    p1.y += current.y;
    if (!blitListP)
        scan_line(canvasP, current.x, current.y, p1.x, p1.y);
    current.x = p1.x;
    current.y = p1.y;
}



static void 
do_paintRect(struct canvas * const canvasP,
             struct Rect     const prect) {

    struct Rect rect;
  
    if (verbose)
        dumpRect("painting", prect);

    rectinter(clip_rect, prect, &rect);

    draw_pen_rect(canvasP, &rect);
}



static drawFn paintRect;

static void 
paintRect(struct canvas * const canvasP,
          blitList *      const blitListP,
          int             const version) {

    readRect(&cur_rect);
    if (!blitListP)
        do_paintRect(canvasP, cur_rect);
}



static drawFn paintSameRect;

static void 
paintSameRect(struct canvas * const canvasP,
              blitList *      const blitListP,
              int             const version) {

    if (!blitListP)
        do_paintRect(canvasP, cur_rect);
}



static void 
do_frameRect(struct canvas * const canvasP,
             struct Rect     const rect) {

    if (verbose)
        dumpRect("framing", rect);

    if (pen_width > 0 && pen_height > 0) {
        unsigned int x, y;

        for (x = rect.left; x <= rect.right - pen_width; x += pen_width) {
            draw_pen(canvasP, x, rect.top);
            draw_pen(canvasP, x, rect.bottom - pen_height);
        }
        
        for (y = rect.top; y <= rect.bottom - pen_height ; y += pen_height) {
            draw_pen(canvasP, rect.left, y);
            draw_pen(canvasP, rect.right - pen_width, y);
        }
    }
}



static drawFn frameRect;

static void 
frameRect(struct canvas * const canvasP,
          blitList *      const blitListP,
          int             const version) {

    readRect(&cur_rect);
    if (!blitListP)
        do_frameRect(canvasP, cur_rect);
}



static drawFn frameSameRect;

static void 
frameSameRect(struct canvas * const canvasP,
              blitList *      const blitListP,
              int             const version) {

    if (!blitListP)
        do_frameRect(canvasP, cur_rect);
}



/* a stupid shell sort - I'm so embarrassed  */

static void 
poly_sort(int const sort_index, struct Point points[]) {
  int d, i, j, temp;

  /* initialize and set up sort interval */
  d = 4;
  while (d<=sort_index) d <<= 1;
  d -= 1;

  while (d > 1) {
    d >>= 1;
    for (j = 0; j <= (sort_index-d); j++) {
      for(i = j; i >= 0; i -= d) {
    if ((points[i+d].y < points[i].y) ||
        ((points[i+d].y == points[i].y) &&
         (points[i+d].x <= points[i].x))) {
      /* swap x1,y1 with x2,y2 */
      temp = points[i].y;
      points[i].y = points[i+d].y;
      points[i+d].y = temp;
      temp = points[i].x;
      points[i].x = points[i+d].x;
      points[i+d].x = temp;
    }
      }
    }
  }
}



/* Watch out for the lack of error checking in the next two functions ... */

static void 
scan_poly(struct canvas * const canvasP,
          int             const np, 
          struct Point          pts[]) {

  int dx,dy,dxabs,dyabs,i,scan_index,j,k,px,py;
  int sdx,sdy,x,y,toggle,old_sdy,sy0;

  /* This array needs to be at least as large as the largest dimension of
     the bounding box of the poly (but I don't check for overflows ...) */
  struct Point coord[5000];

  scan_index = 0;

  /* close polygon */
  px = pts[np].x = pts[0].x;
  py = pts[np].y = pts[0].y;

  /*  This section draws the polygon and stores all the line points
   *  in an array. This doesn't work for concave or non-simple polys.
   */
  /* are y levels same for first and second points? */
  if (pts[1].y == pts[0].y) {
    coord[scan_index].x = px;
    coord[scan_index].y = py;
    scan_index++;
  }

#define sign(x) ((x) > 0 ? 1 : ((x)==0 ? 0:(-1)) )   

  old_sdy = sy0 = sign(pts[1].y - pts[0].y);
  for (j=0; j<np; j++) {
    /* x,y difference between consecutive points and their signs  */
    dx = pts[j+1].x - pts[j].x;
    dy = pts[j+1].y - pts[j].y;
    sdx = SGN(dx);
    sdy = SGN(dy);
    dxabs = abs(dx);
    dyabs = abs(dy);
    x = y = 0;

    if (dxabs >= dyabs)
      {
    for (k=0; k < dxabs; k++) {
      y += dyabs;
      if (y >= dxabs) {
        y -= dxabs;
        py += sdy;
        if (old_sdy != sdy) {
          old_sdy = sdy;
          scan_index--;
        }
        coord[scan_index].x = px+sdx;
        coord[scan_index].y = py;
        scan_index++;
      }
      px += sdx;
      draw_pen(canvasP, px, py);
    }
      }
    else
      {
    for (k=0; k < dyabs; k++) {
      x += dxabs;
      if (x >= dyabs) {
        x -= dyabs;
        px += sdx;
      }
      py += sdy;
      if (old_sdy != sdy) {
        old_sdy = sdy;
        if (sdy != 0) scan_index--;
      }
      draw_pen(canvasP, px,py);
      coord[scan_index].x = px;
      coord[scan_index].y = py;
      scan_index++;
    }
      }
  }

  /* after polygon has been drawn now fill it */

  scan_index--;
  if (sy0 + sdy == 0) scan_index--;

  poly_sort(scan_index, coord);
  
  toggle = 0;
  for (i = 0; i < scan_index; i++) {
    if ((coord[i].y == coord[i+1].y) && (toggle == 0))
      {
    for (j = coord[i].x; j <= coord[i+1].x; j++)
      draw_pen(canvasP, j, coord[i].y);
    toggle = 1;
      }
    else
      toggle = 0;
  }
}
  


static drawFn paintPoly;
  
static void 
paintPoly(struct canvas * const canvasP,
          blitList *      const blitListP,
          int             const version) {

  struct Rect bb;
  struct Point pts[100];
  int i, np = (readWord() - 10) >> 2;

  readRect(&bb);
  for (i=0; i<np; ++i)
    readPoint(&pts[i]);

  /* scan convert poly ... */
  if (!blitListP)
      scan_poly(canvasP, np, pts);
}



static drawFn PnLocHFrac;

static void 
PnLocHFrac(struct canvas * const canvasP,
           blitList *      const blitListP,
           int             const version) {

    Word frac = readWord();

    if (verbose)
        pm_message("PnLocHFrac = %d", frac);
}



static drawFn TxMode;

static void 
TxMode(struct canvas * const canvasP,
       blitList *      const blitListP,
       int             const version) {

    text_mode = readWord();

    if (text_mode >= 8 && text_mode < 15)
        text_mode -= 8;
    if (verbose)
        pm_message("text transfer mode = %s",
            const_name(transfer_name, text_mode));
    
    /* ignore the text mask bit 'cause we don't handle it yet */
    text_trf = transfer(text_mode & ~64);
}



static drawFn TxFont;

static void 
TxFont(struct canvas * const canvasP,
       blitList *      const blitListP,
       int             const version) {

    text_font = readWord();
    if (verbose)
        pm_message("text font %s", const_name(font_name, text_font));
}



static drawFn TxFace;

static void 
TxFace(struct canvas * const canvasP,
       blitList *      const blitListP,
       int             const version) {

    text_face = readByte();
    if (verbose)
        pm_message("text face %d", text_face);
}



static drawFn TxSize;

static void 
TxSize(struct canvas * const canvasP,
       blitList *      const blitListP,
       int             const version) {

    text_size = readWord();
    if (verbose)
        pm_message("text size %d", text_size);
}



static void
skip_text(blitList * const blitListP) {

    skip(readByte());

    blitListP->unblittableText = true;
}



static int 
abs_value(int const x) {
    if (x < 0)
        return -x;
    else
        return x;
}



static struct font* 
get_font(int const font, 
         int const size, 
         int const style) {

    int closeness, bestcloseness;
    struct fontinfo* fi, *best;

    best = 0;
    for (fi = fontlist; fi; fi = fi->next) {
        closeness = abs_value(fi->font - font) * 10000 +
            abs_value(fi->size - size) * 100 +
            abs_value(fi->style - style);
        if (!best || closeness < bestcloseness) {
            best = fi;
            bestcloseness = closeness;
        }
    }

    if (best) {
        if (best->loaded)
            return best->loaded;

        if ((best->loaded = pbm_loadbdffont(best->filename)))
            return best->loaded;
    }

    /* It would be better to go looking for the nth best font, really */
    return 0;
}



/* This only does 0, 90, 180 and 270 degree rotations */

static void 
rotate(int * const x, 
       int * const y) {
    int tmp;

    if (ps_rotation >= 315 || ps_rotation <= 45)
        return;

    *x -= ps_cent_x;
    *y -= ps_cent_y;

    if (ps_rotation > 45 && ps_rotation < 135) {
        tmp = *x;
        *x = *y;
        *y = tmp;
    }
    else if (ps_rotation >= 135 && ps_rotation < 225) {
        *x = -*x;
    }
    else if (ps_rotation >= 225 && ps_rotation < 315) {
        tmp = *x;
        *x = *y;
        *y = -tmp;
    }
    *x += ps_cent_x;
    *y += ps_cent_y;
}



static void
do_ps_text(struct canvas * const canvasP,
           Word            const tx, 
           Word            const ty) {

    int len, width, i, w, h, x, y, rx, ry, o;
    Byte str[256], ch;
    struct glyph* glyph;

    current.x = tx;
    current.y = ty;

    if (!ps_cent_set) {
        ps_cent_x += tx;
        ps_cent_y += ty;
        ps_cent_set = 1;
    }

    len = readByte();

    /* XXX this width calculation is not completely correct */
    width = 0;
    for (i = 0; i < len; i++) {
        ch = str[i] = readByte();
        if (tfont->glyph[ch])
            width += tfont->glyph[ch]->xadd;
    }

    if (verbose) {
        str[len] = '\0';
        pm_message("ps text: %s", str);
    }

    /* XXX The width is calculated in order to do different justifications.
     * However, I need the width of original text to finish the job.
     * In other words, font metrics for Quickdraw fonts
     */

    x = tx;

    for (i = 0; i < len; i++) {
        if (!(glyph = tfont->glyph[str[i]]))
            continue;
        
        y = ty - glyph->height - glyph->y;
        for (h = 0; h < glyph->height; h++) {
            for (w = 0; w < glyph->width; w++) {
                rx = x + glyph->x + w;
                ry = y;
                rotate(&rx, &ry);
                if ((rx >= picFrame.left) && (rx < picFrame.right) &&
                    (ry >= picFrame.top) && (ry < picFrame.bottom))
                {
                    o = PIXEL_INDEX(rx, ry);
                    if (glyph->bmap[h * glyph->width + w]) {
                        canvasP->planes.red[o] = foreground.red;
                        canvasP->planes.grn[o] = foreground.grn;
                        canvasP->planes.blu[o] = foreground.blu;
                    }
                }
            }
            y++;
        }
        x += glyph->xadd;
    }
}



static void
do_text(struct canvas *  const canvasP,
        blitList *       const blitListP,
        Word             const startx, 
        Word             const starty) {

    if (blitListP)
        skip_text(blitListP);
    else {
        if (!(tfont = get_font(text_font, text_size, text_face)))
            tfont = pbm_defaultfont("bdf");

        if (ps_text)
            do_ps_text(canvasP, startx, starty);
        else {
            int len;
            Word x, y;

            x = startx;
            y = starty;
            for (len = readByte(); len > 0; --len) {
                struct glyph* const glyph = tfont->glyph[readByte()];
                if (glyph) {
                    int dy;
                    int h;
                    for (h = 0, dy = y - glyph->height - glyph->y;
                         h < glyph->height; 
                         ++h, ++dy) {
                        int w;
                        for (w = 0; w < glyph->width; ++w) {
                            struct RGBColor * const colorP = 
                                glyph->bmap[h * glyph->width + w] ?
                                &black : &white;
                            draw_pixel(canvasP,
                                       x + w + glyph->x, dy, colorP, text_trf);
                        }
                    }
                    x += glyph->xadd;
                }
            }
            current.x = x;
            current.y = y;
        }
    }
}



static drawFn LongText;

static void
LongText(struct canvas * const canvasP,
         blitList *      const blitListP,
         int             const version) {

    struct Point p;

    readPoint(&p);

    do_text(canvasP, blitListP, p.x, p.y);
}



static drawFn DHText;

static void
DHText(struct canvas * const canvasP,
       blitList *      const blitListP,
       int             const version) {

    current.x += readByte();

    do_text(canvasP, blitListP, current.x, current.y);
}



static drawFn DVText;

static void
DVText(struct canvas * const canvasP,
       blitList *      const blitListP,
       int             const version) {

    current.y += readByte();

    do_text(canvasP, blitListP, current.x, current.y);
}



static drawFn DHDVText;

static void
DHDVText(struct canvas * const canvasP,
         blitList *      const blitListP,
         int             const version) {
    Byte dh, dv;

    dh = readByte();
    dv = readByte();

    if (verbose)
        pm_message("dh, dv = %d, %d", dh, dv);

    current.x += dh;
    current.y += dv;

    do_text(canvasP, blitListP, current.x, current.y);
}



/*
 * This could use readPixmap, but I'm too lazy to hack readPixmap.
 */

static void
directBits(struct canvas * const canvasP,
           blitList *      const blitListP,
           unsigned int    const pictVersion, 
           bool            const skipRegion) {

    struct pixMap   p;
    struct Rect     srcRect;
    struct Rect     dstRect;
    struct raster   raster;
    Word            mode;

    /* skip fake len, and fake EOF */
    skip(4);    /* Ptr baseAddr == 0x000000ff */
    readWord();    /* version */
    readRect(&p.Bounds);
    p.packType = readWord();
    p.packSize = readLong();
    p.hRes = readLong();
    p.vRes = readLong();
    p.pixelType = readWord();
    p.pixelSize = readWord();
    p.pixelSize = readWord();    /* XXX twice??? */
    p.cmpCount = readWord();
    p.cmpSize = readWord();
    p.planeBytes = readLong();
    p.pmTable = readLong();
    p.pmReserved = readLong();

    readRect(&srcRect);
    if (verbose)
        dumpRect("source rectangle:", srcRect);

    readRect(&dstRect);
    if (verbose)
        dumpRect("destination rectangle:", dstRect);

    mode = readWord();
    if (verbose)
        pm_message("transfer mode = %s", const_name(transfer_name, mode));

    if (skipRegion) 
        skip_poly_or_region(canvasP, blitListP, pictVersion);

    unpackbits(ifp, &p.Bounds, 0, p.pixelSize, &raster);

    blit(srcRect, p.Bounds, raster, canvasP, blitListP, p.pixelSize,
         dstRect, picFrame, rowlen, NULL, mode);

    freeRaster(raster);
}



#define SKIP_REGION_TRUE TRUE
#define SKIP_REGION_FALSE FALSE

static drawFn DirectBitsRect;

static void
DirectBitsRect(struct canvas * const canvasP,
               blitList *      const blitListP,
               int             const version) {

    directBits(canvasP, blitListP, version, SKIP_REGION_FALSE);
}



static drawFn DirectBitsRgn;

static void
DirectBitsRgn(struct canvas * const canvasP,
              blitList *      const blitListP,
              int             const version) {

    directBits(canvasP, blitListP, version, SKIP_REGION_TRUE);
}



static void
do_pixmap(struct canvas * const canvasP,
          blitList *      const blitListP,
          int             const version, 
          Word            const rowBytes, 
          int             const is_region) {
/*----------------------------------------------------------------------------
   Do a paletted image.
-----------------------------------------------------------------------------*/
    Word mode;
    struct pixMap p;
    struct raster raster;
    struct RGBColor * color_table;
    struct Rect srcRect;
    struct Rect dstRect;

    readPixmap(&p);

    if (verbose)
        pm_message("%u x %u paletted image",
                   p.Bounds.right - p.Bounds.left,
                   p.Bounds.bottom - p.Bounds.top);

    color_table = readColorTable();

    readRect(&srcRect);

    if (verbose)
        dumpRect("source rectangle:", srcRect);

    readRect(&dstRect);

    if (verbose)
        dumpRect("destination rectangle:", dstRect);

    mode = readWord();

    if (verbose)
        pm_message("transfer mode = %s", const_name(transfer_name, mode));

    if (is_region)
        skip_poly_or_region(canvasP, blitListP, version);

    stage = "unpacking rectangle";

    unpackbits(ifp, &p.Bounds, rowBytes, p.pixelSize, &raster);

    blit(srcRect, p.Bounds, raster, canvasP, blitListP, 8,
         dstRect, picFrame, rowlen, color_table, mode);

    free(color_table);
    freeRaster(raster);
}



static void
do_bitmap(FILE *          const ifP,
          struct canvas * const canvasP,
          blitList *      const blitListP,
          int             const version, 
          int             const rowBytes, 
          int             const is_region) {
/*----------------------------------------------------------------------------
   Do a bitmap.  That's one bit per pixel, 0 is white, 1 is black.

   Read the raster from file 'ifP'.
-----------------------------------------------------------------------------*/
    struct Rect Bounds;
    struct Rect srcRect;
    struct Rect dstRect;
    Word mode;
    struct raster raster;
        /* This raster contains padding on the right to make a multiple
           of 16 pixels per row.
        */
    static struct RGBColor color_table[] = { 
        {65535L, 65535L, 65535L}, {0, 0, 0} };

    readRect(&Bounds);
    readRect(&srcRect);
    readRect(&dstRect);
    mode = readWord();
    if (verbose)
        pm_message("transfer mode = %s", const_name(transfer_name, mode));

    if (is_region)
        skip_poly_or_region(canvasP, blitListP, version);

    stage = "unpacking rectangle";

    unpackbits(ifP, &Bounds, rowBytes, 1, &raster);

    blit(srcRect, Bounds, raster, canvasP, blitListP, 8,
         dstRect, picFrame, rowlen, color_table, mode);

    freeRaster(raster);
}



static drawFn BitsRect;

static void
BitsRect(struct canvas * const canvasP,
         blitList *      const blitListP,
         int             const version) {

    Word rowBytesWord;
    bool pixMap;
    unsigned int rowBytes;

    stage = "Reading rowBytes word for bitsrect";
    rowBytesWord = readWord();

    interpretRowBytesWord(rowBytesWord, &pixMap, &rowBytes);

    if (pixMap)
        do_pixmap(canvasP, blitListP, version, rowBytes, 0);
    else
        do_bitmap(ifp, canvasP, blitListP, version, rowBytes, 0);
}



static drawFn BitsRegion;

static void
BitsRegion(struct canvas * const canvasP,
           blitList *      const blitListP,
           int             const version) {
    
    Word rowBytesWord;
    bool pixMap;
    unsigned int rowBytes;

    stage = "Reading rowBytes for bitsregion";
    rowBytesWord = readWord();

    interpretRowBytesWord(rowBytesWord, &pixMap, &rowBytes);

    if (pixMap)
        do_pixmap(canvasP, blitListP, version, rowBytes, 1);
    else
        do_bitmap(ifp, canvasP, blitListP, version, rowBytes, 1);
}



 /*
  * See http://developer.apple.com/techpubs/mac/QuickDraw/QuickDraw-461.html
  * for opcode description
  */
static struct opdef const optable[] = {
/* 0x00 */  { "NOP", 0, NULL, "nop" },
/* 0x01 */  { "ClipRgn", NA, ClipRgn, "clip region" },
/* 0x02 */  { "BkPat", 8, BkPat, "background pattern" },
/* 0x03 */  { "TxFont", 2, TxFont, "text font (word)" },
/* 0x04 */  { "TxFace", 1, TxFace, "text face (byte)" },
/* 0x05 */  { "TxMode", 2, TxMode, "text mode (word)" },
/* 0x06 */  { "SpExtra", 4, NULL, "space extra (fixed point)" },
/* 0x07 */  { "PnSize", 4, PnSize, "pen size (point)" },
/* 0x08 */  { "PnMode", 2, PnMode, "pen mode (word)" },
/* 0x09 */  { "PnPat", 8, PnPat, "pen pattern" },
/* 0x0a */  { "FillPat", 8, FillPat, "fill pattern" },
/* 0x0b */  { "OvSize", 4, NULL, "oval size (point)" },
/* 0x0c */  { "Origin", 4, NULL, "dh, dv (word)" },
/* 0x0d */  { "TxSize", 2, TxSize, "text size (word)" },
/* 0x0e */  { "FgColor", 4, NULL, "foreground color (longword)" },
/* 0x0f */  { "BkColor", 4, NULL, "background color (longword)" },
/* 0x10 */  { "TxRatio", 8, NULL, "numerator (point), denominator (point)" },
/* 0x11 */  { "Version", 1, NULL, "version (byte)" },
/* 0x12 */  { "BkPixPat", NA, BkPixPat, "color background pattern" },
/* 0x13 */  { "PnPixPat", NA, PnPixPat, "color pen pattern" },
/* 0x14 */  { "FillPixPat", NA, FillPixPat, "color fill pattern" },
/* 0x15 */  { "PnLocHFrac", 2, PnLocHFrac, "fractional pen position" },
/* 0x16 */  { "ChExtra", 2, NULL, "extra for each character" },
/* 0x17 */  RESERVED_OP(0),
/* 0x18 */  RESERVED_OP(0),
/* 0x19 */  RESERVED_OP(0),
/* 0x1a */  { "RGBFgCol", RGB_LEN, RGBFgCol, "RGB foreColor" },
/* 0x1b */  { "RGBBkCol", RGB_LEN, RGBBkCol, "RGB backColor" },
/* 0x1c */  { "HiliteMode", 0, NULL, "hilite mode flag" },
/* 0x1d */  { "HiliteColor", RGB_LEN, NULL, "RGB hilite color" },
/* 0x1e */  { "DefHilite", 0, NULL, "Use default hilite color" },
/* 0x1f */  { "OpColor", NA, OpColor, "RGB OpColor for arithmetic modes" },
/* 0x20 */  { "Line", 8, Line, "pnLoc (point), newPt (point)" },
/* 0x21 */  { "LineFrom", 4, LineFrom, "newPt (point)" },
/* 0x22 */  { "ShortLine", 6, ShortLine, 
              "pnLoc (point, dh, dv (-128 .. 127))" },
/* 0x23 */  { "ShortLineFrom", 2, ShortLineFrom, "dh, dv (-128 .. 127)" },
/* 0x24 */  RESERVED_OP(WORD_LEN),
/* 0x25 */  RESERVED_OP(WORD_LEN),
/* 0x26 */  RESERVED_OP(WORD_LEN),
/* 0x27 */  RESERVED_OP(WORD_LEN),
/* 0x28 */  { "LongText", NA, LongText, 
              "txLoc (point), count (0..255), text" },
/* 0x29 */  { "DHText", NA, DHText, "dh (0..255), count (0..255), text" },
/* 0x2a */  { "DVText", NA, DVText, "dv (0..255), count (0..255), text" },
/* 0x2b */  { "DHDVText", NA, DHDVText, 
              "dh, dv (0..255), count (0..255), text" },
/* 0x2c */  RESERVED_OP(WORD_LEN),
/* 0x2d */  RESERVED_OP(WORD_LEN),
/* 0x2e */  RESERVED_OP(WORD_LEN),
/* 0x2f */  RESERVED_OP(WORD_LEN),
/* 0x30 */  { "frameRect", 8, frameRect, "rect" },
/* 0x31 */  { "paintRect", 8, paintRect, "rect" },
/* 0x32 */  { "eraseRect", 8, NULL, "rect" },
/* 0x33 */  { "invertRect", 8, NULL, "rect" },
/* 0x34 */  { "fillRect", 8, NULL, "rect" },
/* 0x35 */  RESERVED_OP(8),
/* 0x36 */  RESERVED_OP(8),
/* 0x37 */  RESERVED_OP(8),
/* 0x38 */  { "frameSameRect", 0, frameSameRect, "rect" },
/* 0x39 */  { "paintSameRect", 0, paintSameRect, "rect" },
/* 0x3a */  { "eraseSameRect", 0, NULL, "rect" },
/* 0x3b */  { "invertSameRect", 0, NULL, "rect" },
/* 0x3c */  { "fillSameRect", 0, NULL, "rect" },
/* 0x3d */  RESERVED_OP(0),
/* 0x3e */  RESERVED_OP(0),
/* 0x3f */  RESERVED_OP(0),
/* 0x40 */  { "frameRRect", 8, NULL, "rect" },
/* 0x41 */  { "paintRRect", 8, NULL, "rect" },
/* 0x42 */  { "eraseRRect", 8, NULL, "rect" },
/* 0x43 */  { "invertRRect", 8, NULL, "rect" },
/* 0x44 */  { "fillRRrect", 8, NULL, "rect" },
/* 0x45 */  RESERVED_OP(8),
/* 0x46 */  RESERVED_OP(8),
/* 0x47 */  RESERVED_OP(8),
/* 0x48 */  { "frameSameRRect", 0, NULL, "rect" },
/* 0x49 */  { "paintSameRRect", 0, NULL, "rect" },
/* 0x4a */  { "eraseSameRRect", 0, NULL, "rect" },
/* 0x4b */  { "invertSameRRect", 0, NULL, "rect" },
/* 0x4c */  { "fillSameRRect", 0, NULL, "rect" },
/* 0x4d */  RESERVED_OP(0),
/* 0x4e */  RESERVED_OP(0),
/* 0x4f */  RESERVED_OP(0),
/* 0x50 */  { "frameOval", 8, NULL, "rect" },
/* 0x51 */  { "paintOval", 8, NULL, "rect" },
/* 0x52 */  { "eraseOval", 8, NULL, "rect" },
/* 0x53 */  { "invertOval", 8, NULL, "rect" },
/* 0x54 */  { "fillOval", 8, NULL, "rect" },
/* 0x55 */  RESERVED_OP(8),
/* 0x56 */  RESERVED_OP(8),
/* 0x57 */  RESERVED_OP(8),
/* 0x58 */  { "frameSameOval", 0, NULL, "rect" },
/* 0x59 */  { "paintSameOval", 0, NULL, "rect" },
/* 0x5a */  { "eraseSameOval", 0, NULL, "rect" },
/* 0x5b */  { "invertSameOval", 0, NULL, "rect" },
/* 0x5c */  { "fillSameOval", 0, NULL, "rect" },
/* 0x5d */  RESERVED_OP(0),
/* 0x5e */  RESERVED_OP(0),
/* 0x5f */  RESERVED_OP(0),
/* 0x60 */  { "frameArc", 12, NULL, "rect, startAngle, arcAngle" },
/* 0x61 */  { "paintArc", 12, NULL, "rect, startAngle, arcAngle" },
/* 0x62 */  { "eraseArc", 12, NULL, "rect, startAngle, arcAngle" },
/* 0x63 */  { "invertArc", 12, NULL, "rect, startAngle, arcAngle" },
/* 0x64 */  { "fillArc", 12, NULL, "rect, startAngle, arcAngle" },
/* 0x65 */  RESERVED_OP(12),
/* 0x66 */  RESERVED_OP(12),
/* 0x67 */  RESERVED_OP(12),
/* 0x68 */  { "frameSameArc", 4, NULL, "rect, startAngle, arcAngle" },
/* 0x69 */  { "paintSameArc", 4, NULL, "rect, startAngle, arcAngle" },
/* 0x6a */  { "eraseSameArc", 4, NULL, "rect, startAngle, arcAngle" },
/* 0x6b */  { "invertSameArc", 4, NULL, "rect, startAngle, arcAngle" },
/* 0x6c */  { "fillSameArc", 4, NULL, "rect, startAngle, arcAngle" },
/* 0x6d */  RESERVED_OP(4),
/* 0x6e */  RESERVED_OP(4),
/* 0x6f */  RESERVED_OP(4),
/* 0x70 */  { "framePoly", NA, skip_poly_or_region, "poly" },
/* 0x71 */  { "paintPoly", NA, paintPoly, "poly" },
/* 0x72 */  { "erasePoly", NA, skip_poly_or_region, "poly" },
/* 0x73 */  { "invertPoly", NA, skip_poly_or_region, "poly" },
/* 0x74 */  { "fillPoly", NA, skip_poly_or_region, "poly" },
/* 0x75 */  RESERVED_OP_F(skip_poly_or_region),
/* 0x76 */  RESERVED_OP_F(skip_poly_or_region),
/* 0x77 */  RESERVED_OP_F(skip_poly_or_region),
/* 0x78 */  { "frameSamePoly", 0, NULL, "poly (NYI)" },
/* 0x79 */  { "paintSamePoly", 0, NULL, "poly (NYI)" },
/* 0x7a */  { "eraseSamePoly", 0, NULL, "poly (NYI)" },
/* 0x7b */  { "invertSamePoly", 0, NULL, "poly (NYI)" },
/* 0x7c */  { "fillSamePoly", 0, NULL, "poly (NYI)" },
/* 0x7d */  RESERVED_OP(0),
/* 0x7e */  RESERVED_OP(0),
/* 0x7f */  RESERVED_OP(0),
/* 0x80 */  { "frameRgn", NA, skip_poly_or_region, "region" },
/* 0x81 */  { "paintRgn", NA, skip_poly_or_region, "region" },
/* 0x82 */  { "eraseRgn", NA, skip_poly_or_region, "region" },
/* 0x83 */  { "invertRgn", NA, skip_poly_or_region, "region" },
/* 0x84 */  { "fillRgn", NA, skip_poly_or_region, "region" },
/* 0x85 */  RESERVED_OP_F(skip_poly_or_region),
/* 0x86 */  RESERVED_OP_F(skip_poly_or_region),
/* 0x87 */  RESERVED_OP_F(skip_poly_or_region),
/* 0x88 */  { "frameSameRgn", 0, NULL, "region (NYI)" },
/* 0x89 */  { "paintSameRgn", 0, NULL, "region (NYI)" },
/* 0x8a */  { "eraseSameRgn", 0, NULL, "region (NYI)" },
/* 0x8b */  { "invertSameRgn", 0, NULL, "region (NYI)" },
/* 0x8c */  { "fillSameRgn", 0, NULL, "region (NYI)" },
/* 0x8d */  RESERVED_OP(0),
/* 0x8e */  RESERVED_OP(0),
/* 0x8f */  RESERVED_OP(0),
/* 0x90 */  { "BitsRect", NA, BitsRect, "copybits, rect clipped" },
/* 0x91 */  { "BitsRgn", NA, BitsRegion, "copybits, rgn clipped" },
/* 0x92 */  RESERVED_OP(WORD_LEN),
/* 0x93 */  RESERVED_OP(WORD_LEN),
/* 0x94 */  RESERVED_OP(WORD_LEN),
/* 0x95 */  RESERVED_OP(WORD_LEN),
/* 0x96 */  RESERVED_OP(WORD_LEN),
/* 0x97 */  RESERVED_OP(WORD_LEN),
/* 0x98 */  { "PackBitsRect", NA, BitsRect, "packed copybits, rect clipped" },
/* 0x99 */  { "PackBitsRgn", NA, BitsRegion, "packed copybits, rgn clipped" },
/* 0x9a */  { "DirectBitsRect", NA, DirectBitsRect, 
              "PixMap, srcRect, dstRect, int copymode, PixData" },
/* 0x9b */  { "DirectBitsRgn", NA, DirectBitsRgn, 
              "PixMap, srcRect, dstRect, int copymode, maskRgn, PixData" },
/* 0x9c */  RESERVED_OP(WORD_LEN),
/* 0x9d */  RESERVED_OP(WORD_LEN),
/* 0x9e */  RESERVED_OP(WORD_LEN),
/* 0x9f */  RESERVED_OP(WORD_LEN),
/* 0xa0 */  { "ShortComment", 2, ShortComment, "kind (word)" },
/* 0xa1 */  { "LongComment", NA, LongComment, 
              "kind (word), size (word), data" }
};



static void
processOpcode(Word            const opcode, 
              struct canvas * const canvasP,
              blitList *      const blitListP,
              unsigned int    const version) {

    if (opcode < 0xa2) {
        stage = optable[opcode].name;
        if (verbose) {
            if (streq(stage, "reserved"))
                pm_message("reserved opcode=0x%x", opcode);
            else
                pm_message("Opcode: %s", optable[opcode].name);
        }

        if (optable[opcode].impl != NULL)
            (*optable[opcode].impl)(canvasP, blitListP, version);
        else if (optable[opcode].len >= 0)
            skip(optable[opcode].len);
        else {
            /* It's a special length code */
            switch (optable[opcode].len) {
            case WORD_LEN: {
                Word const len = readWord();
                skip(len);
            } break;
            default:
                pm_error("can't do length %d", optable[opcode].len);
            }
        }
    } else if (opcode == 0xc00) {
        if (verbose)
            pm_message("HeaderOp");
        stage = "HeaderOp";
        skip(24);
    } else if (opcode >= 0xa2 && opcode <= 0xaf) {
        stage = "skipping reserved";
        if (verbose)
            pm_message("%s 0x%x", stage, opcode);
        skip(readWord());
    } else if (opcode >= 0xb0 && opcode <= 0xcf) {
        /* just a reserved opcode, no data */
        if (verbose)
            pm_message("reserved 0x%x", opcode);
    } else if (opcode >= 0xd0 && opcode <= 0xfe) {
        stage = "skipping reserved";
        if (verbose)
            pm_message("%s 0x%x", stage, opcode);
        skip(readLong());
    } else if (opcode >= 0x100 && opcode <= 0x7fff) {
        stage = "skipping reserved";
        if (verbose)
            pm_message("%s 0x%x", stage, opcode);
        skip((opcode >> 7) & 255);
    } else if (opcode >= 0x8000 && opcode <= 0x80ff) {
        /* just a reserved opcode, no data */
        if (verbose)
            pm_message("reserved 0x%x", opcode);
    } else if (opcode >= 0x8100) {
        stage = "skipping reserved";
        if (verbose)
            pm_message("%s 0x%x", stage, opcode);
        skip(readLong());
    } else
        pm_error("This program does not understand opcode 0x%04x", opcode);
}



static void
interpretPict(FILE * const ofP) {

    Byte ch;
    Word picSize;
    Word opcode;
    unsigned int version;
    unsigned int i;
    struct canvas canvas;
    blitList blitList;

    initBlitList(&blitList);

    for (i = 0; i < 64; i++)
        pen_pat.pix[i] = bkpat.pix[i] = fillpat.pix[i] = 1;
    pen_width = pen_height = 1;
    pen_mode = 0; /* srcCopy */
    pen_trf = transfer(pen_mode);
    text_mode = 0; /* srcCopy */
    text_trf = transfer(text_mode);

    stage = "Reading picture size";
    picSize = readWord();

    if (verbose)
        pm_message("picture size = %u (0x%x)", picSize, picSize);

    stage = "reading picture frame";
    readRect(&picFrame);

    if (verbose) {
        dumpRect("Picture frame:", picFrame);
        pm_message("Picture size is %u x %u",
                   picFrame.right - picFrame.left,
                   picFrame.bottom - picFrame.top);
    }

    if (!fullres) {
        rowlen = picFrame.right  - picFrame.left;
        collen = picFrame.bottom - picFrame.top;

        allocPlanes(rowlen, collen, &canvas.planes);

        clip_rect = picFrame;
    }

    while ((ch = readByte()) == 0)
        ;
    if (ch != 0x11)
        pm_error("No version number");

    version = readByte();

    switch (version) {
    case 1:
        break;
    case 2: {
        unsigned char const subcode = readByte();
        if (subcode != 0xff)
            pm_error("The only Version 2 PICT images this program "
                     "undertands are subcode 0xff.  This image has "
                     "subcode 0x%02x", subcode);
    } break;
    default:
        pm_error("Unrecognized PICT version %u", version);
    }

    if (verbose)
        pm_message("PICT version %u", version);

    while((opcode = get_op(version)) != 0xff)
        processOpcode(opcode, &canvas, fullres ? &blitList : NULL, version);
    
    if (fullres) {
        if (blitList.unblittableText)
            pm_message("Warning: text is omitted from the output because "
                       "we don't know how to do text with -fullres.");
        doBlitList(&canvas, &blitList);
    }
    outputPpm(ofP, canvas.planes);

    freePlanes(canvas.planes);
}



static void
loadDefaultFontDir(void) {
/*----------------------------------------------------------------------------
   Load the fonts from the font directory file "fontdir" (in the current
   directory), if it exists.
-----------------------------------------------------------------------------*/
    struct stat statbuf;
    int rc;

    rc = stat("fontdir", &statbuf);
    
    if (rc == 0)
        load_fontdir("fontdir");
}



int
main(int argc, char * argv[]) {
    int argn;
    int header;
    const char* const usage =
"[-verbose] [-fullres] [-noheader] [-quickdraw] [-fontdir file] [pictfile]";

    ppm_init( &argc, argv );

    argn = 1;
    verbose = 0;
    fullres = 0;
    header = 1;
    recognize_comment = 1;

    while (argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0') {
        if (pm_keymatch(argv[argn], "-verbose", 2))
            verbose++;
        else if (pm_keymatch(argv[argn], "-fullres", 3))
            fullres = 1;
        else if (pm_keymatch(argv[argn], "-noheader", 2))
            header = 0;
        else if (pm_keymatch(argv[argn], "-quickdraw", 2))
            recognize_comment = 0;
        else if (pm_keymatch(argv[argn], "-fontdir", 3)) {
            argn++;
            if (!argv[argn])
                pm_usage(usage);
            else
                load_fontdir(argv[argn]);
        }
        else
            pm_usage(usage);
        ++argn;
    }

    if (argn < argc) {
        ifp = pm_openr(argv[argn]);
        ++argn;
    } else
        ifp = stdin;

    if (argn != argc)
        pm_usage(usage);

    loadDefaultFontDir();

    if (header) {
        stage = "Reading 512 byte header";
        /* Note that the "header" in PICT is entirely comment! */
        skip(512);
    }

    interpretPict(stdout);

    pm_close(stdout);

    return 0;
}
