/*=============================================================================
                                  jpegxltopam
===============================================================================
  Read a JPEG XL image and produce a PAM image.

  By Bryan Henderson, October 2026.  Contributed to the public domain by its
  author.
===============================================================================
  Design notes:

  This program reads the entire image into memory at once (with 32 bits per
  sample).  It ought to be possible to read a row at a time and have libjxl
  convert in chunks, but libjxl does not appear to have that capability.
=============================================================================*/
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jxl/decode.h>
#include <jxl/types.h>


#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pam.h"


static JxlMemoryManager * const memoryManagerP = NULL;



struct CmdlineInfo {
    const char * inputFileName;
    unsigned int verbose;
};



static void
parseCommandLine(int                  argc,
                 const char **        argv,
                 struct CmdlineInfo * const cmdlineP) {

    optEntry * option_def;
    optStruct3 opt;
    unsigned int option_def_index = 0;  /* incremented by OPTENT3 */

    MALLOCARRAY(option_def, 100);

    OPTENT3(0,  "verbose",      OPT_FLAG,       NULL,
            &cmdlineP->verbose,        0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* we have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* we have no parms that are negative numbers */

    /* uses and sets argc, argv, and some of *cmdlineP and others */
    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileName = argv[1];
    else
        pm_error("Program takes at most one argument: input file name.");

    free(option_def);
}



static void
readFile(FILE *           const ifP,
         unsigned char ** const jxlImageP,
         size_t *         const jxlImageLenP) {

    unsigned char * buffer;   /* malloced */
    size_t          bufferSz; /* Allocated size of 'buffer' */
    size_t          dataLen;  /* Number of bytes in the buffer now */
    bool            eof;      /* We have read through EOF */

    /* Start with 4K buffer and iterate, doubling the buffer in each
       interation untile the whole file is read.
    */

    for (bufferSz = 0, buffer = NULL, eof = false, dataLen = 0; !eof; ) {
        size_t const newBufferSz = MAX(4096, bufferSz * 2);

        size_t bytesReadCt;

        buffer = realloc(buffer, newBufferSz);

        if (!buffer) {
            pm_error("Could not allocate %u bytes for input I/O buffer",
                     (unsigned)newBufferSz);
        }

        bytesReadCt = fread(&buffer[bufferSz], 1, newBufferSz - bufferSz, ifP);

        if (ferror(ifP)) {
            pm_error("Failed reading up to %u bytes from input file "
                     "at position %u",
                     (unsigned)(newBufferSz - bufferSz), (unsigned)bufferSz);
        }

        dataLen += bytesReadCt;

        if (bufferSz + bytesReadCt < newBufferSz)
            eof = true;

        bufferSz = newBufferSz;
    }
    *jxlImageP    = buffer;
    *jxlImageLenP = dataLen;
}



static void
getMetadata(JxlDecoder *   const decoderP,
            unsigned int * const colsP,
            unsigned int * const rowsP) {

    JxlBasicInfo     basicInfo;
    JxlDecoderStatus rc;

    rc = JxlDecoderGetBasicInfo(decoderP, &basicInfo);

    if (rc != JXL_DEC_SUCCESS)
        pm_error("JxlDecoderGetBasicInfo failed");
    else {
        *colsP = basicInfo.xsize;
        *rowsP = basicInfo.ysize;
    }
}



static void
makeOutputBuffer(JxlDecoder * const decoderP,
                 float **     const pixelsP) {

    JxlPixelFormat const pixelFormat = {4,
                                        JXL_TYPE_FLOAT,
                                        JXL_NATIVE_ENDIAN,
                                        0};

    unsigned char *  outbuf;
    size_t           bufferSz;
    JxlDecoderStatus rc;

    rc = JxlDecoderImageOutBufferSize(decoderP, &pixelFormat,
                                      &bufferSz);

    if (rc != JXL_DEC_SUCCESS)
        pm_error("JxlDecoderImageOutBufferSize failed");

    /* The output buffer is sized for a 'float' for each sample in
       the image, i.e. 4 * 4 * width * height.  The 4's come from
       'pixelFormat'' the width and height from the header of the
       JPEG-XL image.
    */

    MALLOCARRAY(outbuf, bufferSz);

    if (!outbuf) {
        pm_error("Failed to allocate %u bytes for a libjxl "
                 "output buffer", (unsigned)bufferSz);
    }
    rc = JxlDecoderSetImageOutBuffer(decoderP, &pixelFormat,
                                     outbuf, bufferSz);

    if (rc != JXL_DEC_SUCCESS)
        pm_error("JxlDecoderSetImageOutBuffer failed");


    *pixelsP = (float *)outbuf;
}



static void
decodeJxl(const unsigned char * const jxlImage,
          size_t                const jxlImageLen,
          unsigned int *        const rowsP,
          unsigned int *        const colsP,
          float **              const pixelsP) {
/*----------------------------------------------------------------------------
   Decode the first image in the JPEG-XL stream 'jxlImage', which is
   'jxlImageLen' bytes long.

   Return the dimensions of that image as *rowsP and *colsP and its
   raster as *pixelsP.

   *pixelsP is an array of normalized (range 0..1) sample values in row,
   column, plane order, with the planes being red, green, blue, and alpha.
-----------------------------------------------------------------------------*/
    JxlDecoder * const decoderP = JxlDecoderCreate(memoryManagerP);

    if (!decoderP)
        pm_error("JxlDecoderCreate failed");
    else {
        bool             gotBasicInfo;
        bool             gotWholeImg;
        JxlDecoderStatus rc;

        rc = JxlDecoderSubscribeEvents(
            decoderP, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE);

        if (rc != JXL_DEC_SUCCESS)
            pm_error("JxlDecoderSubscribeEvents failed");

        rc = JxlDecoderSetInput(decoderP, jxlImage, jxlImageLen);

        if (rc != JXL_DEC_SUCCESS)
            pm_error("JxlDecoderSetInput failed");

        JxlDecoderCloseInput(decoderP);

        for (gotBasicInfo = false, gotWholeImg = false;  !gotWholeImg;) {
            JxlDecoderStatus rc;

            rc = JxlDecoderProcessInput(decoderP);

            if (rc == JXL_DEC_ERROR)
                pm_error("JxlDecoderProcessInput failed");
            else if (rc == JXL_DEC_NEED_MORE_INPUT) {
                pm_error("Invalid input; "
                         "ends in the middle of a JPEG XL image");
            } else if (rc == JXL_DEC_BASIC_INFO) {
                gotBasicInfo = true;
            } else if (rc == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
                makeOutputBuffer(decoderP, pixelsP);
            } else if (rc == JXL_DEC_FULL_IMAGE || rc == JXL_DEC_SUCCESS) {
                /* N.B. FULL_IMAGE means we got one full image, but there may
                   be more in the stream.  SUCCESS means we have exhausted the
                   stream.
                */
                gotWholeImg = true;
            } else {
                pm_error("Internal error: impossible return code %d "
                         "from JxlDecoderProcessInput", rc);
            }
        }

        if (gotBasicInfo) {
            getMetadata(decoderP, colsP, rowsP);
        } else {
            pm_error("Internal error: JxlDecoderProcessInput did not "
                     "produce basic image info");
        }
    }
    JxlDecoderDestroy(decoderP);
}



static void
writePam(const float * const pixels,
         unsigned int  const width,
         unsigned int  const height,
         FILE *        const ofP) {

    const char *       comment = "Produced by jpegxltopam";  /* constant */
    unsigned int const depth  = 4;
    pixval       const maxval = 255;

    struct pam outPam;
    tuple *      tuplerow;
    unsigned int row;

    outPam.size             = sizeof(struct pam);
    outPam.len              = PAM_STRUCT_SIZE(comment_p);
    outPam.file             = stdout;
    outPam.format           = PAM_FORMAT;
    outPam.plainformat      = 0;
    outPam.width            = width;
    outPam.height           = height;
    outPam.depth            = depth;
    outPam.maxval           = maxval;
    outPam.bytes_per_sample = pnm_bytespersample(maxval);
    sprintf(outPam.tuple_type, PAM_PPM_ALPHA_TUPLETYPE);
    outPam.allocation_depth = depth;
    outPam.comment_p        = &comment;

    pnm_writepaminit(&outPam);

    tuplerow = pnm_allocpamrow(&outPam);

    for (row = 0; row < height; ++row) {
        unsigned int col;
        for (col = 0; col < width; ++col) {
            unsigned int plane;
            for (plane = 0; plane < depth; ++ plane) {
                float const normalizedSample =
                    pixels[(row * width + col) * depth + plane];

                tuplerow[col][plane] =
                    MIN(maxval, (sample)(normalizedSample * maxval + 0.5));
            }
        }
        pnm_writepamrow(&outPam, tuplerow);
    }
    pnm_freepamrow(tuplerow);

}



static void
jpegxltopam(FILE *             const ifP,
            FILE *             const ofP,
            struct CmdlineInfo const cmdline) {

    unsigned char * jxlImage;
    size_t jxlImageLen;
    unsigned int rows, cols;
    float * pixels;  /* malloced */
        /* The raster, in row-major order with 4 floats in range 0..1 per
           pixel: red, green, blue, alpha

           Ergo size is rows * cols * 4
        */

    readFile(ifP, &jxlImage, &jxlImageLen);

    if (cmdline.verbose)
        pm_message("Input image is %u bytes", (unsigned) jxlImageLen);

    decodeJxl(jxlImage, jxlImageLen, &rows, &cols, &pixels);

    if (cmdline.verbose)
        pm_message("Image is %u columns x %u rows", cols, rows);

    writePam(pixels, cols, rows, ofP);
}



int
main(int           argc,
     const char ** argv) {

    FILE * ifP;
    struct CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    jpegxltopam(ifP, stdout, cmdline);

    pm_close(ifP);

    return 0;
}



