/*
 * Author:      Burkhard Neidecker-Lutz
 *              Digital CEC Karlsruhe
 *      neideck@nestvx.enet.dec.com

 Copyright (c) Digital Equipment Corporation, 1992

 Permission to use, copy, modify, and distribute this software and its
 documentation for any purpose and without fee is hereby granted,
 provided that the above copyright notice appear in all copies and that
 both that copyright notice and this permission notice appear in
 supporting documentation, and that the name of Digital Equipment
 Corporation not be used in advertising or publicity pertaining to
 distribution of the software without specific, written prior
 permission. Digital Equipment Corporation makes no representations
 about the suitability of this software for any purpose. It is provided
 "as is" without express or implied warranty.

 Version: 1.0           30.07.92

*/

#include <string.h>
#include <assert.h>

#include "mallocvar.h"
#include "pnm.h"
#include "bitreverse.h"


/* The structure we use to convey all sorts of "magic" data to the DDIF */
/* header write procedure.                      */

typedef struct {
    int width;
    int height;
    int h_res;            /* Resolutions in dpi for bounding box */
    int v_res;
    int bits_per_pixel;
    int bytes_per_line;
    int spectral;         /* 2 == monochrome, 5 == rgb        */
    int components;
    int bits_per_component;
    int polarity;         /* zeromin == 2 , zeromax == 1      */
} imageparams;



/* ASN.1 basic encoding rules magic number */

/*  Abstract Syntax Notation One (ASN.1)   */
/*  https://en.wikipedia.org/wiki/ASN.1    */
/*  Basic Encoding Rules (BER):            */
/*  https://en.wikipedia.org/wiki/X.690    */

#define UNIVERSAL 0
#define APPLICATION 1
#define CONTEXT 2
#define PRIVATE 3

#define PRIM 0
#define CONS 1



static imageparams
ddifImageParams(int          const format,
                unsigned int const cols,
                unsigned int const rows,
                int          const horizontalResolution,
                int          const verticalResolution) {

    imageparams img;

    img.width  = cols;
    img.height = rows;
    img.h_res  = horizontalResolution;
    img.v_res  = verticalResolution;

    switch (PNM_FORMAT_TYPE(format)) {
    case PBM_TYPE:
        img.bits_per_pixel = 1;
        img.bytes_per_line = (cols + 7) / 8;
        img.spectral = 2;
        img.components = 1;
        img.bits_per_component = 1;
        img.polarity = 1;
        break;
    case PGM_TYPE:
        img.bytes_per_line = cols;
        img.bits_per_pixel = 8;
        img.spectral = 2;
        img.components = 1;
        img.bits_per_component = 8;
        img.polarity = 2;
        break;
    case PPM_TYPE:
        img.bytes_per_line = 3 * cols;
        img.bits_per_pixel = 24;
        img.spectral = 5;
        img.components = 3;
        img.bits_per_component = 8;
        img.polarity = 2;
        break;
    default:
        pm_error("INTERNAL ERROR: impossible Netpbm image format %d", format);
    }

    if (img.bytes_per_line > INT_MAX / img.height)
        pm_error("Input image too large for computation");

    return img;
}



static void
tag(unsigned char ** const pP,
    int              const cl,
    int              const constructed,
    unsigned int     const t0) {
/*----------------------------------------------------------------------------
  Emit an ASN tag of the specified class and tag number.  This is used in
  conjunction with the wr_xxx routines that follow to construct the various
  ASN.1 entities.  Writing each entity is a two-step process, where first the
  tag is written and then the length and value.

   All of these routines take a pointer to a pointer into an output buffer in
   the first argument and update it accordingly.
-----------------------------------------------------------------------------*/
    int const tag_first = (cl << 6) | constructed << 5;

    unsigned int stack[10];
    int sp;
    unsigned char * p;

    p = *pP;  /* initial value */

    if (t0 < 31) {         /* Short tag form   */
        *p++ = tag_first | t0;
    } else {          /* Long tag form */
        unsigned int t;

        *p++ = tag_first | 31;

        for (t = t0, sp = 0; t > 0; t >>= 7) {
            stack[sp++] = t & 0x7f;
        }

        while (--sp > 0) {  /* Tag values with continuation bits */
            *p++ = stack[sp] | 0x80;
        }
        *p++ = stack[0];    /* Last tag portion without continuation bit */
    }
    *pP = p;
}



static void
ind(unsigned char ** const pP) {

/*----------------------------------------------------------------------------
  Emit indefinite length encoding
-----------------------------------------------------------------------------*/
    unsigned char * p;

    p = *pP;  /* initial value */

    *p++ = 0x80;

    *pP = p;
}



static void
wrNull(unsigned char ** const pP) {
/*----------------------------------------------------------------------------
  Emit ASN.1 NULL
-----------------------------------------------------------------------------*/
    unsigned char * p;

    p = *pP;  /* initial value */

    *p++ = 0;

    *pP = p;
}



static void
wrLength(unsigned char ** const pP,
         int              const amount) {
/*----------------------------------------------------------------------------
  Emit ASN.1 length only into buffer, no data
-----------------------------------------------------------------------------*/
    int length;
    unsigned int mask;
    unsigned char * p;

    p = *pP;  /* initial value */

    length = 4;  /* initial value */
    mask = 0xff000000;  /* initial value */

    if (amount < 128) {
        *p++ = amount;
    } else {          /* > 127 */
        while (!(amount & mask)) {  /* Search for first non-zero byte */
            mask >>= 8;
            --length;
        }

        *p++ = length | 0x80;       /* Number of length bytes */

        while (--length >= 0) {     /* Put length bytes   */
            *p++ =(amount >> (8*length)) & 0xff;
        }

    }
    *pP = p;
}



static void
wrInt(unsigned char ** const pP,
      int              const val) {
/*----------------------------------------------------------------------------
  BER encode an integer and write it's length and value
-----------------------------------------------------------------------------*/
    unsigned char * p;

    p = *pP;  /* initial value */

    if (val == 0) {
        *p++ = 1;               /* length */
        *p++ = 0;               /* value  */
    } else {
        int length;

        length = 4;  /* initial value */
        {
            /* Find the smallest representation */
            int const sign = val < 0 ? 0xff : 0x00;   /* Sign bits */

            unsigned int mask;

            for (mask  = 0xffu << 24; (val & mask) == sign; mask >>=8 )
                --length;
        }
        {
            int const sign = (0x80 << ((length-1)*8)) & val;

            if (((val < 0) && !sign) || ((val > 0) && sign)) { /* Sign error */
                ++length;
            }
        }
        *p++ = length;          /* length */
        while (--length >= 0) {
            *p++ = (val >> (8*length)) & 0xff;
        }
    }
    *pP = p;
}



static void
eoc(unsigned char ** const pP) {
/*----------------------------------------------------------------------------
  Emit and End Of Coding sequence
-----------------------------------------------------------------------------*/
    unsigned char * p;

    p = *pP; /* initial value */

    *p++ = 0;
    *p++ = 0;

    *pP = p;
}



static void
wrString(unsigned char ** const pP,
         const char *     const val) {
/*----------------------------------------------------------------------------
  Emit a simple string
-----------------------------------------------------------------------------*/
    size_t const length = strlen(val);

    unsigned char * p;

    p = *pP; /* initial value */

    if (length > 127)
        pm_error("Program does not know how to encode a string "
                 "longer than 127 characters.  "
                 "Image requires one that is %d characters",
                 (unsigned int) length);

    *p++ = (unsigned char)length;
    {
        const char * valCursor;
        for (valCursor = val; *valCursor; ++valCursor)
            *p++ = *valCursor;
    }
    *pP = p;
}



static void
emitIsolatin1(unsigned char ** const pP,
              const char *     const val) {
/*----------------------------------------------------------------------------
   Emit a ISOLATIN-1 string
-----------------------------------------------------------------------------*/
    size_t const length = strlen(val) + 1;
        /* One NULL byte and charset leader */

    unsigned char * p = *pP;

    if (length > 127)
        pm_error("Program does not know how to encode a string "
                 "longer than 127 characters.  "
                 "Image requires one that is %u characters",
                 (unsigned int)length);

    *p++ = (unsigned char)length;
    *p++ = 1;             /* ISO LATIN-1 */
    {
        const char * valCursor;
        for (valCursor = val; *valCursor; ++valCursor)
            *p++ = *valCursor;
    }
    *pP = p;
}



static void
computeBoundingBox(int   const width,
                   int   const height,
                   int   const hRes,
                   int   const vRes,
                   int * const boundingXP,
                   int * const boundingYP) {
/*----------------------------------------------------------------------------
  Calculate the bounding box from the resolutions.
-----------------------------------------------------------------------------*/
    if (width / hRes > INT_MAX / 1200)
        pm_error("Product of input image width %d and "
                 "horizontal resolution %d too large for computation",
                 width, hRes);
    else
        *boundingXP = ((int) (1200 * ((double) (width) / hRes)));

    if (height / vRes > INT_MAX / 1200)
        pm_error("Product of image height %d and "
                 "vertical resolution %d too large for computation",
                 height, vRes);
    else
        *boundingYP = ((int) (1200 * ((double) (height) / vRes)));
}



static void
writeHeader(FILE *              const ofP,
            const imageparams * const imgP) {
/*----------------------------------------------------------------------------
  Write the DDIF grammar onto "file" up to the actual starting location of the
  image data.  The "ip" structure needs to be set to the right values.  A lot
  of the values here are hardcoded to be just right for the bit grammars that
  the Netpbm formats want.
-----------------------------------------------------------------------------*/
    int const maxheadersize = 300;

    unsigned char * buffer;  /* malloc'ed */
    unsigned char * p;       /* pointer into 'buffer' */
    int headersize;
    int boundingX;
    int boundingY;
    int i;

    MALLOCARRAY_NOFAIL(buffer, maxheadersize * 2);

    computeBoundingBox(imgP->width, imgP->height, imgP->h_res, imgP->v_res,
                       &boundingX, &boundingY);

    /* This is gross. The entire DDIF grammar is constructed by hand. */

    /*  function            bytes    count                     */
    /*                                                         */
    /*  tag();              3        1     first tag(), t0>31  */
    /*  tag();              1       73     69 if PBM, PGM      */
    /*  wrInt();            2-5     38     most are 2,3 bytes  */
    /*                                     34 if PBM,PGM       */
    /*  ind();              1       31                         */
    /*  eoc();              2       27                         */
    /*  wrString();         3,5      2                         */
    /*  emitIsolatin1();    5,21     2                         */
    /*  wrLength();         2-5      1                         */

    p = &buffer[0];

    tag(&p,PRIVATE,CONS,16383); ind(&p);      /* DDIF Document */
    tag(&p,CONTEXT,CONS, 0); ind(&p);        /* Document Descriptor */
    tag(&p,CONTEXT,PRIM, 0); wrInt(&p,1);  /* Major Version */
    tag(&p,CONTEXT,PRIM, 1); wrInt(&p,3);  /* Minor Version */
    tag(&p,CONTEXT,PRIM, 2); wrString(&p,"PBM+"); /* Product Identifier */
    tag(&p,CONTEXT,CONS, 3); ind(&p);       /* Product Name */
    tag(&p,PRIVATE,PRIM, 9); emitIsolatin1(&p,"PBMPLUS Writer V1.0");
    eoc(&p);
    eoc(&p);                 /* Document Descriptor */
    tag(&p,CONTEXT,CONS, 1); ind(&p);        /* Document Header     */
    tag(&p,CONTEXT,CONS, 3); ind(&p);       /* Version */
    tag(&p,PRIVATE,PRIM, 9); emitIsolatin1(&p,"1.0");
    eoc(&p);
    eoc(&p);                 /* Document Header */
    tag(&p,CONTEXT,CONS, 2); ind(&p);        /* Document Content    */
    tag(&p,APPLICATION,CONS,2); ind(&p);    /* Segment Primitive    */
    eoc(&p);
    tag(&p,APPLICATION,CONS,2); ind(&p);    /* Segment  */
    tag(&p,CONTEXT,CONS, 3); ind(&p);      /* Segment Specific Attributes */
    tag(&p,CONTEXT,PRIM, 2); wrString(&p,"$I");  /* Category */
    tag(&p,CONTEXT,CONS,22); ind(&p);     /* Image Attributes */
    tag(&p,CONTEXT,CONS, 0); ind(&p);    /* Image Presentation Attributes */
    tag(&p,CONTEXT,PRIM, 1); wrInt(&p,0);  /* Pixel Path */
    tag(&p,CONTEXT,PRIM, 2); wrInt(&p,270); /* Line Progression */
    tag(&p,CONTEXT,CONS, 3); ind(&p);   /* Pixel Aspect Ratio */
    tag(&p,CONTEXT,PRIM, 0); wrInt(&p,1); /* PP Pixel Dist */
    tag(&p,CONTEXT,PRIM, 1); wrInt(&p,1); /* LP Pixel Dist */
    eoc(&p);                /* Pixel Aspect Ratio */
    tag(&p,CONTEXT,PRIM, 4); wrInt(&p,imgP->polarity);
        /* Brightness Polarity */
    tag(&p,CONTEXT,PRIM, 5); wrInt(&p,1);  /* Grid Type    */
    tag(&p,CONTEXT,PRIM, 7); wrInt(&p,imgP->spectral);  /* Spectral Mapping */
    tag(&p,CONTEXT,CONS,10); ind(&p);   /* Pixel Group Info */
    tag(&p,CONTEXT,PRIM, 0); wrInt(&p,1); /* Pixel Group Size */
    tag(&p,CONTEXT,PRIM, 1); wrInt(&p,1); /* Pixel Group Order */
    eoc(&p);                /* Pixel Group Info */
    eoc(&p);                     /* Image Presentation Attributes */
    tag(&p,CONTEXT,CONS, 1); ind(&p);    /* Component Space Attributes */
    tag(&p,CONTEXT,PRIM, 0); wrInt(&p,1);  /* Component Space Organization */
    tag(&p,CONTEXT,PRIM, 1); wrInt(&p,1);  /* Planes per Pixel */
    tag(&p,CONTEXT,PRIM, 2); wrInt(&p,1);  /* Plane Significance   */
    tag(&p,CONTEXT,PRIM, 3); wrInt(&p,imgP->components);
        /* Number of Components    */
    tag(&p,CONTEXT,CONS, 4); ind(&p);   /* Bits per Component   */
    for (i = 0; i < imgP->components; i++) {
        tag(&p,UNIVERSAL,PRIM,2); wrInt(&p,imgP->bits_per_component);
    }
    eoc(&p);                /* Bits per Component   */
    tag(&p,CONTEXT,CONS, 5); ind(&p);   /* Component Quantization Levels */
    for (i = 0; i < imgP->components; i++) {
        tag(&p,UNIVERSAL,PRIM,2); wrInt(&p,1 << imgP->bits_per_component);
    }
    eoc(&p);                /* Component Quantization Levels */
    eoc(&p);                 /* Component Space Attributes */
    eoc(&p);                  /* Image Attributes */
    tag(&p,CONTEXT,CONS,23); ind(&p);     /* Frame Parameters */
    tag(&p,CONTEXT,CONS, 1); ind(&p);    /* Bounding Box */
    tag(&p,CONTEXT,CONS, 0); ind(&p);   /* lower-left   */
    tag(&p,CONTEXT,CONS, 0); ind(&p);  /* XCoordinate  */
    tag(&p,CONTEXT,PRIM, 0); wrInt(&p,0);
    eoc(&p);                           /* XCoordinate  */
    tag(&p,CONTEXT,CONS, 1); ind(&p);  /* YCoordinate  */
    tag(&p,CONTEXT,PRIM, 0); wrInt(&p,0);
    eoc(&p);                               /* YCoordinate  */
    eoc(&p);                /* lower left */
    tag(&p,CONTEXT,CONS, 1); ind(&p);       /* upper right */
    tag(&p,CONTEXT,CONS, 0); ind(&p);      /* XCoordinate  */
    tag(&p,CONTEXT,PRIM, 0); wrInt(&p,boundingX);
    eoc(&p);               /* XCoordinate  */
    tag(&p,CONTEXT,CONS, 1); ind(&p);      /* YCoordinate  */
    tag(&p,CONTEXT,PRIM, 0); wrInt(&p,boundingY);
    eoc(&p);                   /* YCoordinate  */
    eoc(&p);                            /* upper right */
    eoc(&p);                 /* Bounding Box */
    tag(&p,CONTEXT,CONS, 4); ind(&p);    /* Frame Position */
    tag(&p,CONTEXT,CONS, 0); ind(&p);   /* XCoordinate  */
    tag(&p,CONTEXT,PRIM, 0); wrInt(&p,0);
    eoc(&p);                /* XCoordinate  */
    tag(&p,CONTEXT,CONS, 1); ind(&p);   /* YCoordinate  */
    tag(&p,CONTEXT,PRIM, 0); wrInt(&p,0);
    eoc(&p);                    /* YCoordinate  */
    eoc(&p);                 /* Frame Position */
    eoc(&p);                  /* Frame Parameters */
    eoc(&p);                        /* Segment Specific Attributes */
    eoc(&p);                    /* Segment */
    tag(&p,APPLICATION,CONS,17); ind(&p);   /* Image Data Descriptor */
    tag(&p,UNIVERSAL,CONS,16); ind(&p);    /* Sequence */
    tag(&p,CONTEXT,CONS, 0); ind(&p);     /* Image Coding Attributes */
    tag(&p,CONTEXT,PRIM, 1); wrInt(&p,imgP->width); /* Pixels per Line    */
    tag(&p,CONTEXT,PRIM, 2); wrInt(&p,imgP->height);  /* Number of Lines  */
    tag(&p,CONTEXT,PRIM, 3); wrInt(&p,2);   /* Compression Type */
    tag(&p,CONTEXT,PRIM, 5); wrInt(&p,0);   /* Data Offset  */
    tag(&p,CONTEXT,PRIM, 6); wrInt(&p,imgP->bits_per_pixel); /* Pixel Stride */
    tag(&p,CONTEXT,PRIM, 7); wrInt(&p,imgP->bytes_per_line * 8);
        /* Scanline Stride    */
    tag(&p,CONTEXT,PRIM, 8); wrInt(&p,1);   /* Bit Order        */
    tag(&p,CONTEXT,PRIM, 9); wrInt(&p,imgP->bits_per_pixel);
        /* Planebits per Pixel */
    tag(&p,CONTEXT,CONS,10); ind(&p);    /* Byteorder Info   */
    tag(&p,CONTEXT,PRIM, 0); wrInt(&p,1);  /* Byte Unit        */
    tag(&p,CONTEXT,PRIM, 1); wrInt(&p,1);  /* Byte Order   */
    eoc(&p);                 /* Byteorder Info   */
    tag(&p,CONTEXT,PRIM,11); wrInt(&p,3);   /* Data Type        */
    eoc(&p);                              /* Image Coding Attributes */
    tag(&p,CONTEXT,PRIM, 1); wrLength(&p,imgP->bytes_per_line*imgP->height);
        /* Component Plane Data */

    headersize = p - buffer;

    assert(headersize <= maxheadersize);

    {
        size_t bytesWritten;

        bytesWritten = fwrite(buffer, 1, headersize, ofP);

        if (bytesWritten != headersize) {
            pm_error("Write of %u-byte header failed.  Errno='%s'",
                     headersize, strerror(errno));
        }
    }

    free(buffer);
}



static void
writeTrailer(FILE * const ofP) {
/*----------------------------------------------------------------------------
  Write all the closing brackets of the DDIF grammar that are missing.
-----------------------------------------------------------------------------*/
    int const targetsize = 12;
    int const buffersize = targetsize * 3;

    unsigned char * buffer;  /* malloc'ed */
    unsigned char * p;       /* pointer into 'buffer' */
    int trailersize;

    MALLOCARRAY_NOFAIL(buffer, buffersize);

    p = &buffer[0];

    eoc(&p);                        /* Sequence */
    eoc(&p);                     /* Image Data Descriptor */
    tag(&p,APPLICATION,PRIM,1); wrNull(&p);     /* End Segment */
    tag(&p,APPLICATION,PRIM,1); wrNull(&p);     /* End Segment */
    eoc(&p);                  /* Document Content */
    eoc(&p);                   /* DDIF Document */

    /*  function            bytes    count */
    /*                                     */
    /*  tag();              1        2     */
    /*  eoc();              2        4     */
    /*  wr_null()           1        2     */
    /*                                     */
    /*  total 12 bytes  (targetsize=12)    */

    trailersize = p - buffer;

    if (trailersize != targetsize)  {
        free(buffer);
        pm_error("Abnormal trailer size %d bytes.  Should be %d",
                 trailersize, targetsize);
    }

    {
        size_t bytesWritten;
        bytesWritten = fwrite(buffer, 1, trailersize, ofP);

        if (bytesWritten != trailersize) {
            pm_error("Write of %u-byte header failed.  Errno='%s'",
                     trailersize, strerror(errno));
        }
    }

    free(buffer);
}



static void
convertPbmRaster(FILE *          const ifP,
                 int             const format,
                 unsigned int    const cols,
                 unsigned int    const rows,
                 FILE *          const ofP,
                 unsigned int    const bytesPerLine,
                 unsigned char * const data) {

    unsigned int row;

    for (row = 0; row < rows; ++row) {

        unsigned int byteCt;
        size_t bytesWritten;

        pbm_readpbmrow_packed(ifP, data, cols, format);

        for (byteCt=0; byteCt < bytesPerLine; ++byteCt)
            data[byteCt] = bitreverse[data[byteCt]];

        bytesWritten =  fwrite(data, 1, bytesPerLine, ofP);
        if (bytesWritten != bytesPerLine)
            pm_error("File write error on Row %u", row);
    }
}



static void
convertPgmRaster(FILE *          const ifP,
                 int             const format,
                 xelval          const maxval,
                 unsigned int    const cols,
                 unsigned int    const rows,
                 FILE *          const ofP,
                 unsigned int    const bytesPerLine,
                 unsigned char * const data) {

    gray * const pixels = pgm_allocrow(cols);

    unsigned int row;

    for (row = 0; row < rows; ++row) {
        unsigned char * p;
        unsigned int col;
        size_t bytesWritten;

        p = &data[0];

        pgm_readpgmrow(ifP, pixels, cols, maxval, format);

        for (col = 0; col < cols; ++col)
            *p++ = (unsigned char) pixels[col];

        bytesWritten = fwrite(data, 1, bytesPerLine, ofP);
        if (bytesWritten != bytesPerLine)
            pm_error("File write error on Row %u", row);
    }
    pgm_freerow(pixels);
}



static void
convertPpmRaster(FILE *          const ifP,
                 int             const format,
                 xelval          const maxval,
                 unsigned int    const cols,
                 unsigned int    const rows,
                 FILE *          const ofP,
                 unsigned int    const bytesPerLine,
                 unsigned char * const data) {

    pixel * const pixels = ppm_allocrow(cols);

    unsigned int row;

    for (row = 0; row < rows; ++row) {
        unsigned char * p;
        unsigned int col;
        size_t bytesWritten;

        p = &data[0];

        ppm_readppmrow(ifP, pixels, cols, maxval, format);

        for (col = 0; col < cols; ++col) {
            *p++ = PPM_GETR(pixels[col]);
            *p++ = PPM_GETG(pixels[col]);
            *p++ = PPM_GETB(pixels[col]);
        }
        bytesWritten =  fwrite(data, 1, bytesPerLine, ofP);
        if (bytesWritten != bytesPerLine)
            pm_error("File write error on Row %u", row);
    }
    ppm_freerow(pixels);
}



static void
convertRaster(FILE *       const ifP,
              int          const format,
              xelval       const maxval,
              unsigned int const cols,
              unsigned int const rows,
              FILE *       const ofP,
              unsigned int const bytesPerLine) {

    unsigned char * data;

    MALLOCARRAY(data, bytesPerLine);

    if (data == NULL)
        pm_error("Couldn't allocate %u-byte line buffer", bytesPerLine);

    switch (PNM_FORMAT_TYPE(format)) {
    case PBM_TYPE:
        convertPbmRaster(ifP, format, cols, rows, ofP, bytesPerLine, data);
        break;
    case PGM_TYPE:
        convertPgmRaster(ifP, format, maxval, cols, rows, ofP, bytesPerLine,
                         data);
        break;
    case PPM_TYPE:
        convertPpmRaster(ifP, format, maxval, cols, rows, ofP, bytesPerLine,
                         data);
        break;
    default:
        pm_error("INTERNAL ERROR: impossible format value");
    }

    free(data);
}



struct CmdlineInfo {
    const char * inFileNm;
    const char * outFileNm;
    int          horizontalResolution;
    int          verticalResolution;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {

    /* Implementation note: We cannot use shhopt to process the command
       line because of the nonstandard syntax for the -resolution option
    */
    const char     * const usage = "[-resolution x y] [pnmfile [ddiffile]]";

    int argn;

    cmdlineP->horizontalResolution = 75;  /* initial value */
    cmdlineP->verticalResolution   = 75;  /* initial value */

    for (argn = 1;argn < argc && argv[argn][0] == '-';argn++) {
        int arglen = strlen(argv[argn]);

        if (!strncmp (argv[argn],"-resolution", arglen)) {
            if (argn + 2 < argc) {
                cmdlineP->horizontalResolution = atoi(argv[argn+1]);
                cmdlineP->verticalResolution  = atoi(argv[argn+2]);
                argn += 2;
                continue;
            } else {
                pm_usage(usage);
            }
        } else {
            pm_usage(usage);
        }
    }

    if (cmdlineP->horizontalResolution <= 0 ||
        cmdlineP->verticalResolution <= 0)
        pm_error("Unreasonable resolution values: %d x %d",
                 cmdlineP->horizontalResolution, cmdlineP->verticalResolution);

    if (argn == argc - 2) {
        cmdlineP->inFileNm = argv[argn];
        cmdlineP->outFileNm = argv[argn+1];
    } else if (argn == argc - 1) {
        cmdlineP->inFileNm = argv[argn];
        cmdlineP->outFileNm = "-";
    } else {
        cmdlineP->inFileNm  = "-";
        cmdlineP->outFileNm = "-";
    }
}



int
main(int argc, const char *argv[]) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    FILE * ofP;
    int rows, cols;
    xelval maxval;
    int format;
    imageparams img;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inFileNm);
    ofP = pm_openw(cmdline.outFileNm);

    pnm_readpnminit(ifP, &cols, &rows, &maxval, &format);

    img = ddifImageParams(format, cols, rows,
                          cmdline.horizontalResolution,
                          cmdline.verticalResolution);

    writeHeader(ofP, &img);

    convertRaster(ifP, format, maxval, cols, rows, ofP, img.bytes_per_line);

    pm_close(ifP);

    writeTrailer(ofP);

    pm_close(ofP);

    return 0;
}



