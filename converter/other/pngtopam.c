/*
** Copyright (C) 1995,1998 by Alexander Lehmann <alex@hal.rhein-main.de>
**                        and Willem van Schaik <willem@schaik.com>
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
** modeled after giftopnm by David Koblas and
** with lots of bits pasted from libpng.txt by Guy Eric Schalnat
*/

#ifndef PNMTOPNG_WARNING_LEVEL
#  define PNMTOPNG_WARNING_LEVEL 0   /* use 0 for backward compatibility, */
#endif                               /*  2 for warnings (1 == error) */


#include <assert.h>
#include <math.h>
#include <float.h>
#include <png.h>
/* Due to a design error in png.h, you must not #include <setjmp.h> before
   <png.h>.  If you do, png.h won't compile.
*/
#include <setjmp.h>
#include <zlib.h>


#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pam.h"
#include "pngx.h"

enum AlphaHandling {ALPHA_NONE, ALPHA_ONLY, ALPHA_MIX, ALPHA_IN};

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char *inputFilespec;  /* '-' if stdin */
    unsigned int verbose;
    enum AlphaHandling alpha;
    const char * background;
    float gamma;  /* -1.0 means unspecified */
    const char * text;
    unsigned int time;
    unsigned int byrow;
};


static bool verbose;



static void
parseCommandLine(int                  argc, 
                 const char **        argv,
                 struct CmdlineInfo * cmdlineP ) {
/*----------------------------------------------------------------------------
   Parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.  

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int alphaSpec, alphapamSpec, mixSpec,
        backgroundSpec, gammaSpec, textSpec;

    MALLOCARRAY(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "verbose",     OPT_FLAG,   NULL,                  
            &cmdlineP->verbose,       0);
    OPTENT3(0, "alpha",       OPT_FLAG,   NULL,                  
            &alphaSpec,               0);
    OPTENT3(0, "alphapam",    OPT_FLAG,   NULL,                  
            &alphapamSpec,            0);
    OPTENT3(0, "mix",         OPT_FLAG,   NULL,                  
            &mixSpec,                 0);
    OPTENT3(0, "background",  OPT_STRING, &cmdlineP->background,
            &backgroundSpec,          0);
    OPTENT3(0, "gamma",       OPT_FLOAT,  &cmdlineP->gamma,
            &gammaSpec,               0);
    OPTENT3(0, "text",        OPT_STRING, &cmdlineP->text,
            &textSpec,                0);
    OPTENT3(0, "time",        OPT_FLAG,   NULL,                  
            &cmdlineP->time,          0);
    OPTENT3(0, "byrow",       OPT_FLAG,   NULL,                  
            &cmdlineP->byrow,         0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */


    if (alphaSpec + mixSpec + alphapamSpec > 1)
        pm_error("You cannot specify more than one of -alpha -alphapam -mix");
    else if (alphaSpec)
        cmdlineP->alpha = ALPHA_ONLY;
    else if (mixSpec)
        cmdlineP->alpha = ALPHA_MIX;
    else if (alphapamSpec)
        cmdlineP->alpha = ALPHA_IN;
    else
        cmdlineP->alpha = ALPHA_NONE;

    if (backgroundSpec && !mixSpec)
        pm_error("-background is useless without -mix");

    if (!backgroundSpec)
        cmdlineP->background = NULL;

    if (!gammaSpec)
        cmdlineP->gamma = -1.0;

    if (!textSpec)
        cmdlineP->text = NULL;

    if (argc-1 < 1)
        cmdlineP->inputFilespec = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFilespec = argv[1];
    else
        pm_error("Program takes at most one argument: input file name.  "
            "you specified %d", argc-1);
}



typedef struct {
/*----------------------------------------------------------------------------
   A color in a format compatible with the PNG library.

   Note that the PNG library declares types png_color and png_color_16
   which are similar.
-----------------------------------------------------------------------------*/
    png_uint_16 r;
    png_uint_16 g;
    png_uint_16 b;
} pngcolor;



static pngcolor
pngcolorFrom16(png_color_16 const arg) {

    pngcolor retval;

    retval.r = arg.red;
    retval.g = arg.green;
    retval.b = arg.blue;

    return retval;
}



static pngcolor
pngcolorFromByte(png_color const arg) {

    pngcolor retval;

    retval.r = arg.red;
    retval.g = arg.green;
    retval.b = arg.blue;

    return retval;
}



static bool
pngColorEqual(pngcolor const comparand,
              pngcolor const comparator) {

    return (comparand.r == comparator.r
            && comparand.g == comparator.g
            && comparand.b == comparator.b);
}



static png_uint_16
gammaCorrect(png_uint_16 const v,
             float       const g,
             png_uint_16 const maxval) {

    if (g != -1.0)
        return (png_uint_16)
            ROUNDU(pow((double) v / maxval, (1.0 / g)) * maxval);
    else
        return v;
}



static pngcolor
gammaCorrectColor(pngcolor    const color,
                  double      const gamma,
                  png_uint_16 const maxval) {

    pngcolor retval;

    retval.r = gammaCorrect(color.r, gamma, maxval);
    retval.g = gammaCorrect(color.g, gamma, maxval);
    retval.b = gammaCorrect(color.b, gamma, maxval);

    return retval;
}



static void
verifyFileIsPng(FILE *   const ifP,
                size_t * const consumedByteCtP) {

    unsigned char buffer[4];
    size_t bytesRead;

    bytesRead = fread(buffer, 1, sizeof(buffer), ifP);
    if (bytesRead != sizeof(buffer))
        pm_error("input file is empty or too short");

    if (png_sig_cmp(buffer, (png_size_t) 0, (png_size_t) sizeof(buffer)) != 0)
        pm_error("input file is not a PNG file "
                 "(does not have the PNG signature in its first 4 bytes)");
    else
        *consumedByteCtP = bytesRead;
}



static unsigned int
computePngLineSize(struct pngx * const pngxP) {

    unsigned int const bytesPerSample =
        pngx_bitDepth(pngxP) == 16 ? 2 : 1;

    unsigned int samplesPerPixel;

    switch (pngx_colorType(pngxP)) {
    case PNG_COLOR_TYPE_GRAY_ALPHA: samplesPerPixel = 2; break;
    case PNG_COLOR_TYPE_RGB:        samplesPerPixel = 3; break;
    case PNG_COLOR_TYPE_RGB_ALPHA:  samplesPerPixel = 4; break;
    default:                        samplesPerPixel = 1;
    }

    if (UINT_MAX / bytesPerSample / samplesPerPixel < pngx_imageWidth(pngxP))
        pm_error("Width %u of PNG is uncomputably large",
                 pngx_imageWidth(pngxP));
       
    return pngx_imageWidth(pngxP) * bytesPerSample * samplesPerPixel;
}



static void
allocPngRaster(struct pngx * const pngxP,
               png_byte ***  const pngImageP) {

    unsigned int const lineSize = computePngLineSize(pngxP);

    png_byte ** pngImage;
    unsigned int row;

    MALLOCARRAY(pngImage, pngx_imageHeight(pngxP));

    if (pngImage == NULL)
        pm_error("couldn't allocate index space for %u PNG raster rows.  "
                 "Try -byrow, which needs only 1 row of buffer space.  ",
                 pngx_imageHeight(pngxP));

    for (row = 0; row < pngx_imageHeight(pngxP); ++row) {
        MALLOCARRAY(pngImage[row], lineSize);
        if (pngImage[row] == NULL)
            pm_error("couldn't allocate space for %uth row of PNG raster.  "
                     "Try -byrow, which needs only 1 row of buffer space.  ",
                     row);
    }
    *pngImageP = pngImage;
}



static void
freePngRaster(png_byte **   const pngRaster,
              struct pngx * const pngxP) {

    unsigned int row;

    for (row = 0; row < pngx_imageHeight(pngxP); ++row)
        free(pngRaster[row]);

    free(pngRaster);
}



typedef struct {
/*----------------------------------------------------------------------------
   This is an object for reading the raster of the PNG, a row at a time.
-----------------------------------------------------------------------------*/
    struct pngx * pngxP;
    png_byte **   pngRaster;
        /* The entire raster of the PNG.  Null if this is a
           row-at-a-time object.  Constant.

           We give a pointer into this to the user.
        */
    png_byte * rowBuf;
        /* The buffer in which we put the most recently read row.
           Null if this is an all-at-once object.  Constant.

           We give a pointer into this to the user.
        */
    unsigned int  nextRowNum;
        /* The number of the next row to be read from this object. */

} Reader;



static Reader *
reader_createAllAtOnce(struct pngx * const pngxP,
                       FILE *        const ifP) {
/*----------------------------------------------------------------------------
   Create a Reader object that uses libpng's all-at-once raster reading
   interface (libpng calls this the "high level" interface).

   The Reader object reads the PNG at construction time, stores the entire
   raster, and hands it out as you call reader_read().
-----------------------------------------------------------------------------*/
    Reader * readerP;

    MALLOCVAR_NOFAIL(readerP);

    readerP->pngxP = pngxP;

    allocPngRaster(pngxP, &readerP->pngRaster);

    readerP->rowBuf = NULL;

    png_read_image(pngxP->png_ptr, readerP->pngRaster);

    readerP->nextRowNum = 0;

    return readerP;
}



static Reader *
reader_createRowByRow(struct pngx * const pngxP,
                      FILE *        const ifP) {
/*----------------------------------------------------------------------------
   Create a Reader object that uses libpng's one-row-at-a-time raster reading
   interface (libpng calls this the "low level" interface).

   The Reader object reads from the PNG file, via libpng, as its client
   requests the rows.
-----------------------------------------------------------------------------*/
    Reader * readerP;

    MALLOCVAR_NOFAIL(readerP);

    readerP->pngxP = pngxP;

    readerP->pngRaster = NULL;

    MALLOCARRAY(readerP->rowBuf, computePngLineSize(pngxP)); 

    if (!readerP->rowBuf)
        pm_error("Could not allocate %u bytes for a PNG row buffer",
                 computePngLineSize(pngxP));

    readerP->nextRowNum = 0;

    if (pngx_interlaceType(pngxP) != PNG_INTERLACE_NONE)
        pm_message("WARNING: this is an interlaced PNG.  The PAM output "
                   "will be interlaced.  To get proper output, "
                   "don't use -byrow");

    return readerP;
}



static void
reader_destroy(Reader * const readerP) {

    if (readerP->pngRaster)
        freePngRaster(readerP->pngRaster, readerP->pngxP);
   
    if (readerP->rowBuf)
        free(readerP->rowBuf);

    free(readerP);
}



static png_byte *
reader_read(Reader * const readerP) {
/*----------------------------------------------------------------------------
   Return a pointer to the next row of the raster.

   The pointer is into storage owned by this object.  It is good until
   the next read from the object, while the object exists.
-----------------------------------------------------------------------------*/
    png_byte * retval;

    if (readerP->pngRaster) {
        if (readerP->nextRowNum >= pngx_imageHeight(readerP->pngxP))
            retval = NULL;
        else
            retval = readerP->pngRaster[readerP->nextRowNum];
    } else {
        png_read_row(readerP->pngxP->png_ptr, readerP->rowBuf, NULL);
        retval = readerP->rowBuf;
    }

    ++readerP->nextRowNum;

    return retval;
}



static void
readPngInit(struct pngx * const pngxP,
            FILE *        const ifP) {

    size_t sigByteCt;
            
    verifyFileIsPng(ifP, &sigByteCt);

    /* Declare that we already read the signature bytes */
    pngx_setSigBytes(pngxP, (unsigned int)sigByteCt);

    png_init_io(pngxP->png_ptr, ifP);

    png_read_info(pngxP->png_ptr, pngxP->info_ptr);

    if (pngx_bitDepth(pngxP) < 8)
        pngx_setPacking(pngxP);
}



static void
readPngTerm(struct pngx * const pngxP) {

    png_read_end(pngxP->png_ptr, pngxP->info_ptr);
    
    /* Note that some of info_ptr is not defined until png_read_end() 
       completes.  That's because it comes from chunks that are at the
       end of the stream.  In particular, comment and time chunks may
       be at the end.  Furthermore, they may be in both places, in
       which case info_ptr contains different information before and
       after png_read_end().
    */
}



static png_uint_16
getPngVal(const png_byte ** const pp,
          int               const bitDepth) {

    png_uint_16 c;
    
    if (bitDepth == 16)
        c = *(*pp)++ << 8;
    else
        c = 0;

    c |= *(*pp)++;
    
    return c;
}



static bool
isGrayscale(pngcolor const color) {

    return color.r == color.g && color.r == color.b;
}



static sample
alphaMix(png_uint_16 const foreground,
         png_uint_16 const background,
         png_uint_16 const alpha,
         sample      const maxval) {

    double const opacity      = (double)alpha / maxval;
    double const transparency = 1.0 - opacity;

    return ROUNDU(foreground * opacity + background * transparency);
}



static void
setTuple(const struct pam *  const pamP,
         tuple               const tuple,
         pngcolor            const foreground,
         pngcolor            const background,
         enum AlphaHandling  const alphaHandling,
         const struct pngx * const pngxP,
         png_uint_16         const alpha) {

    if (alphaHandling == ALPHA_ONLY)
        tuple[0] = alpha;
    else if (alphaHandling == ALPHA_NONE ||
             (alphaHandling == ALPHA_MIX && alpha == pngxP->maxval)) {
        if (pamP->depth < 3)
            tuple[0] = foreground.r;
        else {
            tuple[PAM_RED_PLANE] = foreground.r;
            tuple[PAM_GRN_PLANE] = foreground.g;
            tuple[PAM_BLU_PLANE] = foreground.b;
        }
    } else if (alphaHandling == ALPHA_IN) {
        if (pamP->depth < 4) {
            tuple[0] = foreground.r;
            tuple[PAM_GRAY_TRN_PLANE] = alpha;
        } else {
            tuple[PAM_RED_PLANE] = foreground.r;
            tuple[PAM_GRN_PLANE] = foreground.g;
            tuple[PAM_BLU_PLANE] = foreground.b;
            tuple[PAM_TRN_PLANE] = alpha;
        }    
    } else {
        assert(alphaHandling == ALPHA_MIX);

        if (pamP->depth < 3)
            tuple[0] =
                alphaMix(foreground.r, background.r, alpha, pngxP->maxval);
        else {
            tuple[PAM_RED_PLANE] =
                alphaMix(foreground.r, background.r, alpha, pngxP->maxval);
            tuple[PAM_GRN_PLANE] =
                alphaMix(foreground.g, background.g, alpha, pngxP->maxval);
            tuple[PAM_BLU_PLANE] =
                alphaMix(foreground.b, background.b, alpha, pngxP->maxval);
        }
    }
}



static bool
isColor(png_color const c) {

    return c.red != c.green || c.green != c.blue;
}



static void
saveText(struct pngx * const pngxP,
         FILE *        const tfP) {

    struct pngx_text const text = pngx_text(pngxP);

    unsigned int i;

    for (i = 0 ; i < text.size; ++i) {
        unsigned int j;
        j = 0;

        while (text.line[i].key[j] != '\0' &&
               text.line[i].key[j] != ' ')
            ++j;    

        if (text.line[i].key[j] != ' ') {
            fprintf(tfP, "%s", text.line[i].key);
            for (j = strlen (text.line[i].key); j < 15; ++j)
                putc(' ', tfP);
        } else {
            fprintf(tfP, "\"%s\"", text.line[i].key);
            for (j = strlen (text.line[i].key); j < 13; ++j)
                putc(' ', tfP);
        }
        putc(' ', tfP); /* at least one space between key and text */
    
        for (j = 0; j < text.line[i].text_length; ++j) {
            putc(text.line[i].text[j], tfP);
            if (text.line[i].text[j] == '\n') {
                unsigned int k;
                for (k = 0; k < 16; ++k)
                    putc(' ', tfP);
            }
        }
        putc('\n', tfP);
    }
}



static void
showTime(struct pngx * const pngxP) {

    if (pngx_chunkIsPresent(pngxP, PNG_INFO_tIME)) {
        png_time const modTime = pngx_time(pngxP);

        static const char * const month[] = {
            "", "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December"
        };

        pm_message("modification time: %02d %s %d %02d:%02d:%02d",
                   modTime.day,
                   month[modTime.month],
                   modTime.year,
                   modTime.hour,
                   modTime.minute,
                   modTime.second);
    }
}



static void
dumpTypeAndFilter(struct pngx * const pngxP) {

    const char * typeString;
    const char * filterString;

    switch (pngx_colorType(pngxP)) {
    case PNG_COLOR_TYPE_GRAY:
        typeString = "gray";
        break;
        
    case PNG_COLOR_TYPE_GRAY_ALPHA:
        typeString = "gray+alpha";
        break;
        
    case PNG_COLOR_TYPE_PALETTE:
        typeString = "palette";
        break;

    case PNG_COLOR_TYPE_RGB:
        typeString = "truecolor";
        break;

    case PNG_COLOR_TYPE_RGB_ALPHA:
        typeString = "truecolor+alpha";
        break;
    }

    switch (pngx_filterType(pngxP)) {
    case PNG_FILTER_TYPE_BASE:
        pm_asprintf(&filterString, "base filter");
        break;
    default:
        pm_asprintf(&filterString, "unknown filter type %d", 
                    pngx_filterType(pngxP));
    }

    pm_message("%s, %s, %s",
               typeString,
               pngx_interlaceType(pngxP) ? 
               "Adam7 interlaced" : "not interlaced",
               filterString);

    pm_strfree(filterString);
}



static void
dumpPngInfo(struct pngx * const pngxP) {

    pm_message("reading a %u x %u image, %u bit%s",
               pngx_imageWidth(pngxP),
               pngx_imageHeight(pngxP),
               pngx_bitDepth(pngxP),
               pngx_bitDepth(pngxP) > 1 ? "s" : "");

    dumpTypeAndFilter(pngxP);

    if (pngx_chunkIsPresent(pngxP, PNG_INFO_bKGD)) {
        png_color_16 const background = pngx_bkgd(pngxP);

        pm_message("background {index, gray, red, green, blue} = "
                   "{%d, %d, %d, %d, %d}",
                   background.index,
                   background.gray,
                   background.red,
                   background.green,
                   background.blue);
    }

    if (pngx_chunkIsPresent(pngxP, PNG_INFO_tRNS))
        pm_message("tRNS chunk (transparency): %u entries",
                   pngx_trns(pngxP).numTrans);
    else
        pm_message("tRNS chunk (transparency): not present");

    if (pngx_chunkIsPresent(pngxP, PNG_INFO_gAMA))
        pm_message("gAMA chunk (image gamma): gamma = %4.2f",
                   pngx_gama(pngxP));
    else
        pm_message("gAMA chunk (image gamma): not present");
    
    if (pngx_chunkIsPresent(pngxP, PNG_INFO_sBIT))
        pm_message("sBIT chunk: present");
    else
        pm_message("sBIT chunk: not present");

    if (pngx_chunkIsPresent(pngxP, PNG_INFO_cHRM))
        pm_message("cHRM chunk: present");
    else
        pm_message("cHRM chunk: not present");

    if (pngx_chunkIsPresent(pngxP, PNG_INFO_PLTE))
        pm_message("PLTE chunk: %d entries", pngx_plte(pngxP).size);
    else
        pm_message("PLTE chunk: not present");

    if (pngx_chunkIsPresent(pngxP, PNG_INFO_bKGD))
        pm_message("bKGD chunk: present");
    else
        pm_message("bKGD chunk: not present");

    if (pngx_chunkIsPresent(pngxP, PNG_INFO_hIST))
        pm_message("hIST chunk: present");
    else
        pm_message("hIST chunk: not present");

    if (pngx_chunkIsPresent(pngxP, PNG_INFO_pHYs))
        pm_message("pHYs chunk: present");
    else
        pm_message("pHYs chunk: not present");

    if (pngx_chunkIsPresent(pngxP, PNG_INFO_oFFs))
        pm_message("oFFs chunk: present");
    else
        pm_message("oFFs chunk: not present");

    if (pngx_chunkIsPresent(pngxP, PNG_INFO_tIME))
        pm_message("tIME chunk: present");
    else
        pm_message("tIME chunk: not present");

    if (pngx_chunkIsPresent(pngxP, PNG_INFO_pCAL))
        pm_message("pCAL chunk: present");
    else
        pm_message("pCAL chunk: not present");

    if (pngx_chunkIsPresent(pngxP, PNG_INFO_sRGB))
        pm_message("sRGB chunk: present");
    else
        pm_message("sRGB chunk: not present");
}



static const png_color_16
transColor(struct pngx * const pngxP) {

    struct pngx_trns const trans = pngx_trns(pngxP);

    assert(pngx_chunkIsPresent(pngxP, PNG_INFO_tRNS));
    
    return trans.transColor;
}



static bool
isTransparentColor(pngcolor      const color,
                   struct pngx * const pngxP,
                   double        const totalgamma) {
/*----------------------------------------------------------------------------
   Return TRUE iff pixels of color 'color' are supposed to be transparent
   everywhere they occur.  Assume it's an RGB image.

   'color' has been gamma-corrected and 'totalgamma' is the gamma value that
   was used for that (we need to know that because *pngxP identifies the
   color that is supposed to be transparent in _not_ gamma-corrected form!).
-----------------------------------------------------------------------------*/
    bool retval;

    if (pngx_chunkIsPresent(pngxP, PNG_INFO_tRNS)) {
        png_color_16 const transColor16 = transColor(pngxP);

        /* It seems odd that libpng lets you get gamma-corrected pixel
           values, but not gamma-corrected transparency or background
           values.  But as that is the case, we have to gamma-correct
           the transparency values.

           Note that because we compare the gamma-corrected values and
           there may be many-to-one mapping of uncorrected to corrected
           values, more pixels may be transparent than what the user
           intended.

           We could fix this by not letting libpng gamma-correct the
           pixels, and just do it ourselves.
        */
    
        switch (pngx_colorType(pngxP)) {
        case PNG_COLOR_TYPE_GRAY:
            retval = color.r == gammaCorrect(transColor16.gray, totalgamma,
                                             pngxP->maxval);
            break;
        default: {
            pngcolor const transColor = pngcolorFrom16(transColor16);
            retval = pngColorEqual(color,
                                   gammaCorrectColor(transColor, totalgamma,
                                                     pngxP->maxval));
        }
        }
    } else 
        retval = FALSE;

    return retval;
}



static void
setupGammaCorrection(struct pngx * const pngxP,
                     float         const displaygamma,
                     float *       const totalgammaP) {

    if (displaygamma == -1.0)
        *totalgammaP = -1.0;
    else {
        float imageGamma;
        if (pngx_chunkIsPresent(pngxP, PNG_INFO_gAMA))
            imageGamma = pngx_gama(pngxP);
        else {
            if (verbose)
                pm_message("PNG doesn't specify image gamma.  Assuming 1.0");
            imageGamma = 1.0;
        }

        if (fabs(displaygamma * imageGamma - 1.0) < .01) {
            *totalgammaP = -1.0;
            if (verbose)
                pm_message("image gamma %4.2f matches "
                           "display gamma %4.2f.  No conversion.",
                           imageGamma, displaygamma);
        } else {
            pngx_setGamma(pngxP, displaygamma, imageGamma);
            *totalgammaP = imageGamma * displaygamma;
            /* In case of gamma-corrections, sBIT's as in the
               PNG-file are not valid anymore 
            */
            pngx_removeChunk(pngxP, PNG_INFO_sBIT);
            if (verbose)
                pm_message("image gamma is %4.2f, "
                           "converted for display gamma of %4.2f",
                           imageGamma, displaygamma);
        }
    }
}



static bool
paletteHasPartialTransparency(struct pngx * const pngxP) {

    bool retval;

    if (pngx_colorType(pngxP) == PNG_COLOR_TYPE_PALETTE) {
        if (pngx_chunkIsPresent(pngxP, PNG_INFO_tRNS)) {
            struct pngx_trns const trans = pngx_trns(pngxP);

            bool foundGray;
            unsigned int i;
            
            for (i = 0, foundGray = FALSE;
                 i < trans.numTrans && !foundGray;
                 ++i) {
                if (trans.trans[i] != 0 && trans.trans[i] != pngxP->maxval) {
                    foundGray = TRUE;
                }
            }
            retval = foundGray;
        } else
            retval = FALSE;
    } else
        retval = FALSE;

    return retval;
}



static void
getComponentSbitFg(struct pngx * const pngxP,
                   png_byte *    const fgSbitP,
                   bool *        const notUniformP) {

    png_color_8 const sigBit = pngx_sbit(pngxP);

    assert(pngx_chunkIsPresent(pngxP, PNG_INFO_sBIT));

    if (pngx_colorType(pngxP) == PNG_COLOR_TYPE_RGB ||
        pngx_colorType(pngxP) == PNG_COLOR_TYPE_RGB_ALPHA ||
        pngx_colorType(pngxP) == PNG_COLOR_TYPE_PALETTE) {

        if (sigBit.red == sigBit.blue &&
            sigBit.red == sigBit.green) {
            *notUniformP = false;
            *fgSbitP     = sigBit.red;
        } else
            *notUniformP = true;
    } else {
        /* It has only a gray channel so it's obviously uniform */
        *notUniformP = false;
        *fgSbitP     = sigBit.gray;
    }
}



static void
getComponentSbit(struct pngx *       const pngxP,
                 enum AlphaHandling  const alphaHandling,
                 png_byte *          const componentSbitP,
                 bool *              const notUniformP) {

    assert(pngx_chunkIsPresent(pngxP, PNG_INFO_sBIT));

    switch (alphaHandling) {

    case ALPHA_ONLY: {
        /* We care only about the alpha channel, so the uniform Sbit is
           the alpha Sbit
        */
        *notUniformP = false;
        *componentSbitP = pngx_sbit(pngxP).alpha;
    } break;
    case ALPHA_NONE:
    case ALPHA_MIX:
        /* We aren't going to produce an alpha channel, so we care only
           about the uniformity of the foreground channels.
        */
        getComponentSbitFg(pngxP, componentSbitP, notUniformP);
        break;
    case ALPHA_IN: {
        /* We care about both the foreground and the alpha */
        bool fgNotUniform;
        png_byte fgSbit;
        
        getComponentSbitFg(pngxP, &fgSbit, &fgNotUniform);

        if (fgNotUniform)
            *notUniformP = true;
        else {
            if (fgSbit == pngx_sbit(pngxP).alpha) {
                *notUniformP    = false;
                *componentSbitP = fgSbit;
            } else
                *notUniformP = true;
        }
    } break;
    }
}

                 

static void
shiftPalette(struct pngx * const pngxP,
             unsigned int  const shift) {
/*----------------------------------------------------------------------------
   Shift every component of every color in the PNG palette right by
   'shift' bits because sBIT chunk says only those are significant.
-----------------------------------------------------------------------------*/
    if (shift > 7)
        pm_error("Invalid PNG: paletted image can't have "
                 "more than 8 significant bits per component, "
                 "but sBIT chunk says %u bits",
                 shift);
    else {
        struct pngx_plte const palette = pngx_plte(pngxP);
        
        unsigned int i;
        
        for (i = 0; i < palette.size; ++i) {
            palette.palette[i].red   >>= (8 - shift);
            palette.palette[i].green >>= (8 - shift);
            palette.palette[i].blue  >>= (8 - shift);
        }
    }
}



static void
computeMaxvalFromSbit(struct pngx *       const pngxP,
                      enum AlphaHandling  const alphaHandling,
                      png_uint_16 *       const maxvalP,
                      bool *              const succeededP,
                      int *               const errorLevelP) {

    /* sBIT handling is very tricky. If we are extracting only the
       image, we can use the sBIT info for grayscale and color images,
       if the three values agree. If we extract the transparency/alpha
       mask, sBIT is irrelevant for trans and valid for alpha. If we
       mix both, the multiplication may result in values that require
       the normal bit depth, so we will use the sBIT info only for
       transparency, if we know that only solid and fully transparent
       is used 
    */

    bool notUniform;
        /* The sBIT chunk says the number of significant high-order bits
           in each component varies among the components we care about.
        */
    png_byte componentSigBit;
        /* The number of high-order significant bits in each RGB component.
           Meaningless if they aren't all the same (i.e. 'notUniform')
        */

    getComponentSbit(pngxP, alphaHandling, &componentSigBit, &notUniform);

    if (notUniform) {
        pm_message("This program cannot handle "
                   "different bit depths for color channels");
        pm_message("writing file with %u bit resolution",
                   pngx_bitDepth(pngxP));
        *succeededP = false;
        *errorLevelP = PNMTOPNG_WARNING_LEVEL;
    } else if (componentSigBit > 15) {
        pm_message("Invalid PNG: says %u significant bits for a component; "
                   "max possible is 16.  Ignoring sBIT chunk.",
                   componentSigBit);
        *succeededP = false;
        *errorLevelP = PNMTOPNG_WARNING_LEVEL;
    } else {
        if (alphaHandling == ALPHA_MIX &&
            (pngx_colorType(pngxP) == PNG_COLOR_TYPE_RGB_ALPHA ||
             pngx_colorType(pngxP) == PNG_COLOR_TYPE_GRAY_ALPHA ||
             paletteHasPartialTransparency(pngxP)))
            *succeededP = false;
        else {
            if (componentSigBit < pngx_bitDepth(pngxP)) {
                pm_message("Image has fewer significant bits, "
                           "writing file with %u bits", componentSigBit);
                *maxvalP = (1l << componentSigBit) - 1;
                *succeededP = true;
                
                if (pngx_colorType(pngxP) == PNG_COLOR_TYPE_PALETTE)
                    shiftPalette(pngxP, componentSigBit);
                else
                    pngx_setShift(pngxP, pngx_sbit(pngxP));
            } else
                *succeededP = false;
        }
    }
}



static void
setupSignificantBits(struct pngx *       const pngxP,
                     enum AlphaHandling  const alphaHandling,
                     int *               const errorLevelP) {
/*----------------------------------------------------------------------------
  Figure out what maxval is used in the PNG described by *pngxP, with 'alpha'
  telling which information in the PNG we care about (image or alpha mask).
  Update *pngxP with that information.

  Return the result as *maxvalP.

  Also set up *pngxP for the corresponding significant bits.
-----------------------------------------------------------------------------*/
    bool gotItFromSbit;
    
    if (pngx_chunkIsPresent(pngxP, PNG_INFO_sBIT))
        computeMaxvalFromSbit(pngxP, alphaHandling,
                              &pngxP->maxval, &gotItFromSbit, errorLevelP);
    else
        gotItFromSbit = false;

    if (!gotItFromSbit) {
        if (pngx_colorType(pngxP) == PNG_COLOR_TYPE_PALETTE) {
            if (alphaHandling == ALPHA_ONLY) {
                if (pngx_colorType(pngxP) == PNG_COLOR_TYPE_GRAY ||
                    pngx_colorType(pngxP) == PNG_COLOR_TYPE_RGB)
                    /* The alpha mask will be all opaque, so maxval 1
                       is plenty
                    */
                    pngxP->maxval = 1;
                else if (paletteHasPartialTransparency(pngxP))
                    /* Use same maxval as PNG transparency palette for
                       simplicity
                    */
                    pngxP->maxval = 255;
                else
                    /* A common case, so we conserve bits */
                    pngxP->maxval = 1;
            } else
                /* Use same maxval as PNG palette for simplicity */
                pngxP->maxval = 255;
        } else {
            pngxP->maxval = (1l << pngx_bitDepth(pngxP)) - 1;
        }
    }
}



static bool
imageHasColor(struct pngx * const pngxP) {

    bool retval;

    if (pngx_colorType(pngxP) == PNG_COLOR_TYPE_GRAY ||
        pngx_colorType(pngxP) == PNG_COLOR_TYPE_GRAY_ALPHA)

        retval = FALSE;
    else if (pngx_colorType(pngxP) == PNG_COLOR_TYPE_PALETTE) {
        struct pngx_plte const palette = pngx_plte(pngxP);

        bool foundColor;
        unsigned int i;
            
        for (i = 0, foundColor = FALSE;
             i < palette.size && !foundColor;
             ++i) {
            if (isColor(palette.palette[i]))
                foundColor = TRUE;
        }
        retval = foundColor;
    } else
        retval = TRUE;

    return retval;
}



static void
determineOutputType(struct pngx *       const pngxP,
                    enum AlphaHandling  const alphaHandling,
                    pngcolor            const bgColor,
                    xelval              const maxval,
                    int *               const formatP,
                    unsigned int *      const depthP,
                    char *              const tupleType) {

    if (alphaHandling == ALPHA_ONLY) {
        /* The output is a old style pseudo-PNM transparency image */
        *depthP = 1;
        *formatP = maxval > 1 ? PGM_FORMAT : PBM_FORMAT;
    } else {            
        /* The output is a normal Netpbm image */
        bool const outputIsColor =
            imageHasColor(pngxP) || !isGrayscale(bgColor);

        if (alphaHandling == ALPHA_IN) {
            *formatP = PAM_FORMAT;
            if (outputIsColor) {
                *depthP = 4;
                strcpy(tupleType, "RGB_ALPHA");
            } else {
                *depthP = 1;
                strcpy(tupleType, "GRAYSCALE_ALPHA");
            }
        } else {
            if (outputIsColor) {
                *formatP = PPM_FORMAT;
                *depthP = 3;
            } else {
                *depthP = 1;
                *formatP = maxval > 1 ? PGM_FORMAT : PBM_FORMAT;
            }
        }
    }
}



static void
getBackgroundColor(struct pngx * const pngxP,
                   const char *  const requestedColor,
                   float         const totalgamma,
                   xelval        const maxval,
                   pngcolor *    const bgColorP) {
/*----------------------------------------------------------------------------
   Figure out what the background color should be.  If the user requested
   a particular color ('requestedColor' not null), that's the one.
   Otherwise, if the PNG specifies a background color, that's the one.
   And otherwise, it's white.
-----------------------------------------------------------------------------*/
    if (requestedColor) {
        /* Background was specified from the command-line; we always
           use that.  I chose to do no gamma-correction in this case;
           which is a bit arbitrary.  
        */
        pixel const backcolor = ppm_parsecolor(requestedColor, maxval);

        bgColorP->r = PPM_GETR(backcolor);
        bgColorP->g = PPM_GETG(backcolor);
        bgColorP->b = PPM_GETB(backcolor);

    } else if (pngx_chunkIsPresent(pngxP, PNG_INFO_bKGD)) {
        /* Didn't manage to get libpng to work (bugs?) concerning background
           processing, therefore we do our own.
        */
        png_color_16 const background = pngx_bkgd(pngxP);
        switch (pngx_colorType(pngxP)) {
        case PNG_COLOR_TYPE_GRAY:
        case PNG_COLOR_TYPE_GRAY_ALPHA:
            bgColorP->r = bgColorP->g = bgColorP->b = 
                gammaCorrect(background.gray, totalgamma, pngxP->maxval);
            break;
        case PNG_COLOR_TYPE_PALETTE: {
            struct pngx_plte const palette = pngx_plte(pngxP);
            png_color const rawBgcolor = 
                palette.palette[background.index];
            *bgColorP = gammaCorrectColor(pngcolorFromByte(rawBgcolor),
                                          totalgamma, pngxP->maxval);
        }
        break;
        case PNG_COLOR_TYPE_RGB:
        case PNG_COLOR_TYPE_RGB_ALPHA: {
            png_color_16 const rawBgcolor = background;
            
            *bgColorP = gammaCorrectColor(pngcolorFrom16(rawBgcolor),
                                          totalgamma, pngxP->maxval);
        }
        break;
        }
    } else 
        /* when no background given, we use white [from version 2.37] */
        bgColorP->r = bgColorP->g = bgColorP->b = maxval;
}



static void
warnNonsquarePixels(struct pngx * const pngxP,
                    int *         const errorLevelP) {

    if (pngx_chunkIsPresent(pngxP, PNG_INFO_pHYs)) {
        float const r =
            (float)pngx_xPixelsPerMeter(pngxP) / pngx_yPixelsPerMeter(pngxP);

        if (r != 1.0) {
            pm_message ("warning - non-square pixels; "
                        "to fix do a 'pamscale -%cscale %g'",
                        r < 1.0 ? 'x' : 'y',
                        r < 1.0 ? 1.0 / r : r );
            *errorLevelP = PNMTOPNG_WARNING_LEVEL;
        }
    }
}



static png_uint_16
paletteAlpha(struct pngx * const pngxP,
             png_uint_16   const index,
             sample        const maxval) {

    png_uint_16 retval;

    if (pngx_chunkIsPresent(pngxP, PNG_INFO_tRNS)) {
        struct pngx_trns const trans = pngx_trns(pngxP);

        if (index < trans.numTrans)
            retval = trans.trans[index];
        else
            retval = maxval;
    } else
        retval = maxval;

    return retval;
}



#define GET_PNG_VAL(p) getPngVal(&(p), pngx_colorType(pngxP))



static void
makeTupleRow(const struct pam *  const pamP,
             const tuple *       const tuplerow,
             struct pngx *       const pngxP,
             const png_byte *    const pngRasterRow,
             pngcolor            const bgColor,
             enum AlphaHandling  const alphaHandling,
             double              const totalgamma) {
/*----------------------------------------------------------------------------
   Convert a raster row as supplied by libpng, at 'pngRasterRow' and
   described by *pngxP, to a libpam-style tuple row at 'tupleRow'.

   Where the raster says the pixel isn't opaque, we either include that
   opacity information in the output pixel or we mix the pixel with background
   color 'bgColor', as directed by 'alphaHandling'.  Or, if 'alphaHandling'
   says so, we may produce an output row of _only_ the transparency
   information.
-----------------------------------------------------------------------------*/
    const png_byte * pngPixelP;
    unsigned int col;

    pngPixelP = &pngRasterRow[0];  /* initial value */
    for (col = 0; col < pngx_imageWidth(pngxP); ++col) {
        switch (pngx_colorType(pngxP)) {
        case PNG_COLOR_TYPE_GRAY: {
            pngcolor fgColor;
            fgColor.r = fgColor.g = fgColor.b = GET_PNG_VAL(pngPixelP);
            setTuple(pamP, tuplerow[col], fgColor, bgColor, alphaHandling,
                     pngxP,
                     isTransparentColor(fgColor, pngxP, totalgamma) ?
                     0 : pngxP->maxval);
        }
        break;

        case PNG_COLOR_TYPE_GRAY_ALPHA: {
            pngcolor fgColor;
            png_uint_16 alpha;

            fgColor.r = fgColor.g = fgColor.b = GET_PNG_VAL(pngPixelP);
            alpha = GET_PNG_VAL(pngPixelP);
            setTuple(pamP, tuplerow[col], fgColor, bgColor,
                     alphaHandling, pngxP, alpha);
        }
        break;

        case PNG_COLOR_TYPE_PALETTE: {
            png_uint_16      const index        = GET_PNG_VAL(pngPixelP);
            struct pngx_plte const palette      = pngx_plte(pngxP);
            png_color        const paletteColor = palette.palette[index];

            pngcolor fgColor;

            fgColor.r = paletteColor.red;
            fgColor.g = paletteColor.green;
            fgColor.b = paletteColor.blue;

            setTuple(pamP, tuplerow[col], fgColor, bgColor, alphaHandling,
                     pngxP, paletteAlpha(pngxP, index, pngxP->maxval));
        }
        break;
                
        case PNG_COLOR_TYPE_RGB: {
            pngcolor fgColor;

            fgColor.r = GET_PNG_VAL(pngPixelP);
            fgColor.g = GET_PNG_VAL(pngPixelP);
            fgColor.b = GET_PNG_VAL(pngPixelP);
            setTuple(pamP, tuplerow[col], fgColor, bgColor, alphaHandling,
                     pngxP,
                     isTransparentColor(fgColor, pngxP, totalgamma) ?
                     0 : pngxP->maxval);
        }
        break;

        case PNG_COLOR_TYPE_RGB_ALPHA: {
            pngcolor fgColor;
            png_uint_16 alpha;

            fgColor.r = GET_PNG_VAL(pngPixelP);
            fgColor.g = GET_PNG_VAL(pngPixelP);
            fgColor.b = GET_PNG_VAL(pngPixelP);
            alpha     = GET_PNG_VAL(pngPixelP);
            setTuple(pamP, tuplerow[col], fgColor, bgColor,
                     alphaHandling, pngxP, alpha);
        }
        break;

        default:
            pm_error("unknown PNG color type: %d",
                     pngx_colorType(pngxP));
        }
    }
}



static void
reportOutputFormat(const struct pam * const pamP) {

    switch (pamP->format) {

    case PBM_FORMAT:
        pm_message("Writing a PBM file");
        break;
    case PGM_FORMAT:
        pm_message("Writing a PGM file with maxval %lu", pamP->maxval);
        break;
    case PPM_FORMAT:
        pm_message("Writing a PPM file with maxval %lu", pamP->maxval);
        break;
    case PAM_FORMAT:
        pm_message("Writing a PAM file with tuple type %s, maxval %lu",
                   pamP->tuple_type, pamP->maxval);
        break;
    default:
        assert(false); /* Every possible value handled above */
    }
}
    


static void
writeNetpbm(struct pam *        const pamP,
            struct pngx *       const pngxP,
            Reader *            const rasterReaderP,
            pngcolor            const bgColor,
            enum AlphaHandling  const alphaHandling,
            double              const totalgamma) {
/*----------------------------------------------------------------------------
   Write a Netpbm image of either the image or the alpha mask, according to
   'alphaHandling' that is in the PNG image described by *pngxP, reading
   its raster with the raster reader object *rasterReaderP.

   *pamP describes the required output image and is consistent with
   *pngInfoP.

   Use background color 'bgColor' in the output if the PNG is such that a
   background color is needed.
-----------------------------------------------------------------------------*/
    tuple * tuplerow;
    unsigned int row;

    if (verbose)
        reportOutputFormat(pamP);

    pnm_writepaminit(pamP);

    tuplerow = pnm_allocpamrow(pamP);

    for (row = 0; row < pngx_imageHeight(pngxP); ++row) {
        png_byte * const pngRow = reader_read(rasterReaderP);

        assert(pngRow);

        makeTupleRow(pamP, tuplerow, pngxP, pngRow, bgColor,
                     alphaHandling, totalgamma);

        pnm_writepamrow(pamP, tuplerow);
    }
    pnm_freepamrow(tuplerow);
}



static void 
convertpng(FILE *             const ifP, 
           FILE *             const tfP, 
           struct CmdlineInfo const cmdline,
           int *              const errorLevelP) {

    Reader * rasterReaderP;
    pngcolor bgColor;
    float totalgamma;
    struct pam pam;
    jmp_buf jmpbuf;
    struct pngx * pngxP;

    *errorLevelP = 0;

    if (setjmp(jmpbuf))
        pm_error ("setjmp returns error condition");

    pngx_create(&pngxP, PNGX_READ, &jmpbuf);

    readPngInit(pngxP, ifP);

    if (verbose)
        dumpPngInfo(pngxP);

    rasterReaderP = cmdline.byrow ? 
        reader_createRowByRow(pngxP, ifP) : reader_createAllAtOnce(pngxP, ifP);

    if (cmdline.time)
        showTime(pngxP);
    if (tfP)
        saveText(pngxP, tfP);

    warnNonsquarePixels(pngxP, errorLevelP);

    setupGammaCorrection(pngxP, cmdline.gamma, &totalgamma);

    setupSignificantBits(pngxP, cmdline.alpha, errorLevelP);

    getBackgroundColor(pngxP, cmdline.background, totalgamma, pngxP->maxval,
                       &bgColor);
  
    pam.size        = sizeof(pam);
    pam.len         = PAM_STRUCT_SIZE(tuple_type);
    pam.file        = stdout;
    pam.plainformat = 0;
    pam.height      = pngx_imageHeight(pngxP);
    pam.width       = pngx_imageWidth(pngxP);
    pam.maxval      = pngxP->maxval;

    determineOutputType(pngxP, cmdline.alpha, bgColor, pngxP->maxval,
                        &pam.format, &pam.depth, pam.tuple_type);

    writeNetpbm(&pam, pngxP, rasterReaderP, bgColor,
                cmdline.alpha, totalgamma);

    reader_destroy(rasterReaderP);

    readPngTerm(pngxP);

    fflush(stdout);

    pngx_destroy(pngxP);
}



int 
main(int argc, const char *argv[]) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    FILE * tfP;
    int errorLevel;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    verbose = cmdline.verbose;

    ifP = pm_openr(cmdline.inputFilespec);

    if (cmdline.text)
        tfP = pm_openw(cmdline.text);
    else
        tfP = NULL;

    convertpng(ifP, tfP, cmdline, &errorLevel);

    if (tfP)
        pm_close(tfP);

    pm_close(ifP);
    pm_close(stdout);

    return errorLevel;
}
