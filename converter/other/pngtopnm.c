/*
** pngtopnm.c -
** read a Portable Network Graphics file and produce a PNM.
**
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

#include <math.h>
#include <float.h>
#include <png.h>    /* includes zlib.h and setjmp.h */
#define VERSION "2.37.4 (5 December 1999) +netpbm"

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pnm.h"

enum alpha_handling {ALPHA_NONE, ALPHA_ONLY, ALPHA_MIX};

struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char *inputFilespec;  /* '-' if stdin */
    unsigned int verbose;
    enum alpha_handling alpha;
    const char * background;
    float gamma;  /* -1.0 means unspecified */
    const char * text;
    unsigned int time;
};


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


static png_uint_16 maxval;
static bool verbose;


static void
parseCommandLine(int                  argc, 
                 const char **        argv,
                 struct cmdlineInfo * cmdlineP ) {
/*----------------------------------------------------------------------------
   Parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.  

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int alphaSpec, mixSpec, backgroundSpec, gammaSpec, textSpec;

    MALLOCARRAY(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "verbose",     OPT_FLAG,   NULL,                  
            &cmdlineP->verbose,       0);
    OPTENT3(0, "alpha",       OPT_FLAG,   NULL,                  
            &alphaSpec,               0);
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

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */


    if (alphaSpec && mixSpec)
        pm_error("You cannot specify both -alpha and -mix");
    else if (alphaSpec)
        cmdlineP->alpha = ALPHA_ONLY;
    else if (mixSpec)
        cmdlineP->alpha = ALPHA_MIX;
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



static void
pngtopnmErrorHandler(png_structp     const png_ptr,
                     png_const_charp const msg) {

    jmp_buf * jmpbufP;

    /* this function, aside from the extra step of retrieving the "error
       pointer" (below) and the fact that it exists within the application
       rather than within libpng, is essentially identical to libpng's
       default error handler.  The second point is critical:  since both
       setjmp() and longjmp() are called from the same code, they are
       guaranteed to have compatible notions of how big a jmp_buf is,
       regardless of whether _BSD_SOURCE or anything else has (or has not)
       been defined.
    */

    pm_message("fatal libpng error: %s", msg);

    jmpbufP = png_get_error_ptr(png_ptr);

    if (!jmpbufP) {
        /* we are completely hosed now */
        pm_error("EXTREMELY fatal error: jmpbuf unrecoverable; terminating.");
    }

    longjmp(*jmpbufP, 1);
}



struct pngx {
    png_structp png_ptr;
    png_infop info_ptr;
};



static void
pngx_createRead(struct pngx ** const pngxPP,
                jmp_buf *      const jmpbufP) {

    struct pngx * pngxP;

    MALLOCVAR(pngxP);

    if (!pngxP)
        pm_error("Failed to allocate memory for PNG object");
    else {
        pngxP->png_ptr = png_create_read_struct(
            PNG_LIBPNG_VER_STRING,
            jmpbufP, pngtopnmErrorHandler, NULL);

        if (!pngxP->png_ptr)
            pm_error("cannot allocate main libpng structure (png_ptr)");
        else {
            pngxP->info_ptr = png_create_info_struct(pngxP->png_ptr);

            if (!pngxP->info_ptr)
                pm_error("cannot allocate libpng info structure (info_ptr)");
            else
                *pngxPP = pngxP;
        }
    }
}



static void
pngx_destroy(struct pngx * const pngxP) {

    png_destroy_read_struct(&pngxP->png_ptr, &pngxP->info_ptr, NULL);

    free(pngxP);
}



static bool
pngx_chunkIsPresent(struct pngx * const pngxP,
                    uint32_t      const chunkType) {

    return png_get_valid(pngxP->png_ptr, pngxP->info_ptr, chunkType);
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
        pngxP->info_ptr->bit_depth == 16 ? 2 : 1;

    unsigned int samplesPerPixel;

    switch (pngxP->info_ptr->color_type) {
    case PNG_COLOR_TYPE_GRAY_ALPHA: samplesPerPixel = 2; break;
    case PNG_COLOR_TYPE_RGB:        samplesPerPixel = 3; break;
    case PNG_COLOR_TYPE_RGB_ALPHA:  samplesPerPixel = 4; break;
    default:                        samplesPerPixel = 1;
    }

    if (UINT_MAX / bytesPerSample / samplesPerPixel < pngxP->info_ptr->width)
        pm_error("Width %u of PNG is uncomputably large",
                 (unsigned int)pngxP->info_ptr->width);
       
    return pngxP->info_ptr->width * bytesPerSample * samplesPerPixel;
}



static void
allocPngRaster(struct pngx * const pngxP,
               png_byte ***  const pngImageP) {

    unsigned int const lineSize = computePngLineSize(pngxP);

    png_byte ** pngImage;
    unsigned int row;

    MALLOCARRAY(pngImage, pngxP->info_ptr->height);

    if (pngImage == NULL)
        pm_error("couldn't allocate space for %u PNG raster rows",
                 (unsigned int)pngxP->info_ptr->height);

    for (row = 0; row < pngxP->info_ptr->height; ++row) {
        MALLOCARRAY(pngImage[row], lineSize);
        if (pngImage[row] == NULL)
            pm_error("couldn't allocate space for %uth row of PNG raster",
                     row);
    }
    *pngImageP = pngImage;
}



static void
freePngRaster(png_byte **   const pngRaster,
              struct pngx * const pngxP) {

    unsigned int row;

    for (row = 0; row < pngxP->info_ptr->height; ++row)
        free(pngRaster[row]);

    free(pngRaster);
}



static void
readPng(struct pngx * const pngxP,
        FILE *        const ifP,
        png_byte ***  const pngRasterP) {

    size_t sigByteCt;
    png_byte ** pngRaster;
            
    verifyFileIsPng(ifP, &sigByteCt);

    /* Declare that we already read the signature bytes */
    png_set_sig_bytes(pngxP->png_ptr, (int)sigByteCt);

    png_init_io(pngxP->png_ptr, ifP);

    png_read_info(pngxP->png_ptr, pngxP->info_ptr);

    allocPngRaster(pngxP, &pngRaster);

    if (pngxP->info_ptr->bit_depth < 8)
        png_set_packing(pngxP->png_ptr);

    png_read_image(pngxP->png_ptr, pngRaster);

    png_read_end(pngxP->png_ptr, pngxP->info_ptr);

    /* Note that some of info_ptr is not defined until png_read_end() 
       completes.  That's because it comes from chunks that are at the
       end of the stream.
    */

    *pngRasterP = pngRaster;
}



static png_uint_16
get_png_val(const png_byte ** const pp,
            int               const bit_depth) {

    png_uint_16 c;
    
    if (bit_depth == 16)
        c = (*((*pp)++)) << 8;
    else
        c = 0;

    c |= (*((*pp)++));
    
    return c;
}



static bool
isGrayscale(pngcolor const color) {

    return color.r == color.g && color.r == color.b;
}



static void 
setXel(xel *               const xelP, 
       pngcolor            const foreground,
       pngcolor            const background,
       enum alpha_handling const alpha_handling,
       png_uint_16         const alpha) {

    if (alpha_handling == ALPHA_ONLY) {
        PNM_ASSIGN1(*xelP, alpha);
    } else {
        if ((alpha_handling == ALPHA_MIX) && (alpha != maxval)) {
            double const opacity      = (double)alpha / maxval;
            double const transparency = 1.0 - opacity;

            pngcolor mix;

            mix.r = foreground.r * opacity + background.r * transparency + 0.5;
            mix.g = foreground.g * opacity + background.g * transparency + 0.5;
            mix.b = foreground.b * opacity + background.b * transparency + 0.5;
            PPM_ASSIGN(*xelP, mix.r, mix.g, mix.b);
        } else
            PPM_ASSIGN(*xelP, foreground.r, foreground.g, foreground.b);
    }
}



static png_uint_16
gamma_correct(png_uint_16 const v,
              float       const g) {

    if (g != -1.0)
        return (png_uint_16) ROUNDU(pow((double) v / maxval, (1.0 / g)) *
                                    maxval);
    else
        return v;
}



static bool
iscolor(png_color const c) {

    return c.red != c.green || c.green != c.blue;
}



static void
saveText(struct pngx * const pngxP,
         FILE *        const tfP) {

    png_info * const info_ptr = pngxP->info_ptr;

    unsigned int i;

    for (i = 0 ; i < info_ptr->num_text; ++i) {
        unsigned int j;
        j = 0;

        while (info_ptr->text[i].key[j] != '\0' &&
               info_ptr->text[i].key[j] != ' ')
            ++j;    

        if (info_ptr->text[i].key[j] != ' ') {
            fprintf(tfP, "%s", info_ptr->text[i].key);
            for (j = strlen (info_ptr->text[i].key); j < 15; ++j)
                putc(' ', tfP);
        } else {
            fprintf(tfP, "\"%s\"", info_ptr->text[i].key);
            for (j = strlen (info_ptr->text[i].key); j < 13; ++j)
                putc(' ', tfP);
        }
        putc(' ', tfP); /* at least one space between key and text */
    
        for (j = 0; j < info_ptr->text[i].text_length; ++j) {
            putc(info_ptr->text[i].text[j], tfP);
            if (info_ptr->text[i].text[j] == '\n') {
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

    static const char * const month[] = {
        "", "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };

    if (pngxP->info_ptr->valid & PNG_INFO_tIME) {
        pm_message("modification time: %02d %s %d %02d:%02d:%02d",
                   pngxP->info_ptr->mod_time.day,
                   month[pngxP->info_ptr->mod_time.month],
                   pngxP->info_ptr->mod_time.year,
                   pngxP->info_ptr->mod_time.hour,
                   pngxP->info_ptr->mod_time.minute,
                   pngxP->info_ptr->mod_time.second);
    }
}



static void
dumpPngInfo(struct pngx * const pngxP) {

    png_info * const info_ptr = pngxP->info_ptr;
    const char *type_string;
    const char *filter_string;

    switch (info_ptr->color_type) {
      case PNG_COLOR_TYPE_GRAY:
        type_string = "gray";
        break;

      case PNG_COLOR_TYPE_GRAY_ALPHA:
        type_string = "gray+alpha";
        break;

      case PNG_COLOR_TYPE_PALETTE:
        type_string = "palette";
        break;

      case PNG_COLOR_TYPE_RGB:
        type_string = "truecolor";
        break;

      case PNG_COLOR_TYPE_RGB_ALPHA:
        type_string = "truecolor+alpha";
        break;
    }

    switch (info_ptr->filter_type) {
    case PNG_FILTER_TYPE_BASE:
        asprintfN(&filter_string, "base filter");
        break;
    default:
        asprintfN(&filter_string, "unknown filter type %d", 
                  info_ptr->filter_type);
    }

    pm_message("reading a %ldw x %ldh image, %d bit%s",
               info_ptr->width, info_ptr->height,
               info_ptr->bit_depth, info_ptr->bit_depth > 1 ? "s" : "");
    pm_message("%s, %s, %s",
               type_string,
               info_ptr->interlace_type ? 
               "Adam7 interlaced" : "not interlaced",
               filter_string);
    pm_message("background {index, gray, red, green, blue} = "
               "{%d, %d, %d, %d, %d}",
               info_ptr->background.index,
               info_ptr->background.gray,
               info_ptr->background.red,
               info_ptr->background.green,
               info_ptr->background.blue);

    strfree(filter_string);

    if (info_ptr->valid & PNG_INFO_tRNS)
        pm_message("tRNS chunk (transparency): %u entries",
                   info_ptr->num_trans);
    else
        pm_message("tRNS chunk (transparency): not present");

    if (info_ptr->valid & PNG_INFO_gAMA)
        pm_message("gAMA chunk (image gamma): gamma = %4.2f", info_ptr->gamma);
    else
        pm_message("gAMA chunk (image gamma): not present");

    if (info_ptr->valid & PNG_INFO_sBIT)
        pm_message("sBIT chunk: present");
    else
        pm_message("sBIT chunk: not present");

    if (info_ptr->valid & PNG_INFO_cHRM)
        pm_message("cHRM chunk: present");
    else
        pm_message("cHRM chunk: not present");

    if (info_ptr->valid & PNG_INFO_PLTE)
        pm_message("PLTE chunk: %d entries", info_ptr->num_palette);
    else
        pm_message("PLTE chunk: not present");

    if (info_ptr->valid & PNG_INFO_bKGD)
        pm_message("bKGD chunk: present");
    else
        pm_message("bKGD chunk: not present");

    if (info_ptr->valid & PNG_INFO_PLTE)
        pm_message("hIST chunk: present");
    else
        pm_message("hIST chunk: not present");

    if (info_ptr->valid & PNG_INFO_pHYs)
        pm_message("pHYs chunk: present");
    else
        pm_message("pHYs chunk: not present");

    if (info_ptr->valid & PNG_INFO_oFFs)
        pm_message("oFFs chunk: present");
    else
        pm_message("oFFs chunk: not present");

    if (info_ptr->valid & PNG_INFO_tIME)
        pm_message("tIME chunk: present");
    else
        pm_message("tIME chunk: not present");

    if (info_ptr->valid & PNG_INFO_pCAL)
        pm_message("pCAL chunk: present");
    else
        pm_message("pCAL chunk: not present");

    if (info_ptr->valid & PNG_INFO_sRGB)
        pm_message("sRGB chunk: present");
    else
        pm_message("sRGB chunk: not present");
}



static const png_color_16 *
transColor(struct pngx * const pngxP) {

    png_bytep trans;
    int numTrans;
    png_color_16 * transColor;

    assert(chunkIsPresent(PNG_INFO_tRNS));
    
    png_get_tRNS(pngxP->png_ptr, pngxP->info_ptr,
                 &trans, &numTrans, &transColor);

    return transColor;
}



static bool
isTransparentColor(pngcolor      const color,
                   struct pngx * const pngxP,
                   double        const totalgamma) {
/*----------------------------------------------------------------------------
   Return TRUE iff pixels of color 'color' are supposed to be transparent
   everywhere they occur.  Assume it's an RGB image.

   'color' has been gamma-corrected.
-----------------------------------------------------------------------------*/
    bool retval;

    if (pngx_chunkIsPresent(PNG_INFO_tRNS)) {
        const png_color_16 * const transColorP = transColor(pngxP);

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
    
        switch (pngxP->info_ptr->color_type) {
        case PNG_COLOR_TYPE_GRAY:
            retval = color.r == gamma_correct(transColorP->gray, totalgamma);
            break;
        default:
            retval = 
                color.r == gamma_correct(transColorP->red,   totalgamma) &&
                color.g == gamma_correct(transColorP->green, totalgamma) &&
                color.b == gamma_correct(transColorP->blue,  totalgamma);
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
        if (pngxP->info_ptr->valid & PNG_INFO_gAMA)
            imageGamma = pngxP->info_ptr->gamma;
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
            png_set_gamma(pngxP->png_ptr, displaygamma, imageGamma);
            *totalgammaP = imageGamma * displaygamma;
            /* in case of gamma-corrections, sBIT's as in the
               PNG-file are not valid anymore 
            */
            pngxP->info_ptr->valid &= ~PNG_INFO_sBIT;
            if (verbose)
                pm_message("image gamma is %4.2f, "
                           "converted for display gamma of %4.2f",
                           imageGamma, displaygamma);
        }
    }
}



static bool
paletteHasPartialTransparency(png_info * const info_ptr) {

    bool retval;

    if (info_ptr->color_type == PNG_COLOR_TYPE_PALETTE) {
        if (info_ptr->valid & PNG_INFO_tRNS) {
            bool foundGray;
            unsigned int i;
            
            for (i = 0, foundGray = FALSE;
                 i < info_ptr->num_trans && !foundGray;
                 ++i) {
                if (info_ptr->trans[i] != 0 &&
                    info_ptr->trans[i] != maxval) {
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
setupSignificantBits(struct pngx *       const pngxP,
                     enum alpha_handling const alpha,
                     png_uint_16 *       const maxvalP,
                     int *               const errorLevelP) {
/*----------------------------------------------------------------------------
  Figure out what maxval would best express the information in the PNG
  described by *pngxP, with 'alpha' telling which information in the PNG we
  care about (image or alpha mask).

  Return the result as *maxvalP.
-----------------------------------------------------------------------------*/
    png_info * const info_ptr = pngxP->info_ptr;

    /* Initial assumption of maxval */
    if (info_ptr->color_type == PNG_COLOR_TYPE_PALETTE) {
        if (alpha == ALPHA_ONLY) {
            if (info_ptr->color_type == PNG_COLOR_TYPE_GRAY ||
                info_ptr->color_type == PNG_COLOR_TYPE_RGB)
                /* The alpha mask will be all opaque, so maxval 1 is plenty */
                *maxvalP = 1;
            else if (paletteHasPartialTransparency(info_ptr))
                /* Use same maxval as PNG transparency palette for simplicity*/
                *maxvalP = 255;
            else
                /* A common case, so we conserve bits */
                *maxvalP = 1;
        } else
            /* Use same maxval as PNG palette for simplicity */
            *maxvalP = 255;
    } else {
        *maxvalP = (1l << info_ptr->bit_depth) - 1;
    }

    /* sBIT handling is very tricky. If we are extracting only the
       image, we can use the sBIT info for grayscale and color images,
       if the three values agree. If we extract the transparency/alpha
       mask, sBIT is irrelevant for trans and valid for alpha. If we
       mix both, the multiplication may result in values that require
       the normal bit depth, so we will use the sBIT info only for
       transparency, if we know that only solid and fully transparent
       is used 
    */
    
    if (info_ptr->valid & PNG_INFO_sBIT) {
        switch (alpha) {
        case ALPHA_MIX:
            if (info_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
                info_ptr->color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
                break;
            if (info_ptr->color_type == PNG_COLOR_TYPE_PALETTE &&
                (info_ptr->valid & PNG_INFO_tRNS)) {

                bool trans_mix;
                unsigned int i;
                trans_mix = TRUE;
                for (i = 0; i < info_ptr->num_trans; ++i)
                    if (info_ptr->trans[i] != 0 && info_ptr->trans[i] != 255) {
                        trans_mix = FALSE;
                        break;
                    }
                if (!trans_mix)
                    break;
            }

            /* else fall though to normal case */

        case ALPHA_NONE:
            if ((info_ptr->color_type == PNG_COLOR_TYPE_PALETTE ||
                 info_ptr->color_type == PNG_COLOR_TYPE_RGB ||
                 info_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA) &&
                (info_ptr->sig_bit.red != info_ptr->sig_bit.green ||
                 info_ptr->sig_bit.red != info_ptr->sig_bit.blue) &&
                alpha == ALPHA_NONE) {
                pm_message("This program cannot handle "
                           "different bit depths for color channels");
                pm_message("writing file with %d bit resolution",
                           info_ptr->bit_depth);
                *errorLevelP = PNMTOPNG_WARNING_LEVEL;
            } else {
                if ((info_ptr->color_type == PNG_COLOR_TYPE_PALETTE) &&
                    (info_ptr->sig_bit.red < 255)) {
                    unsigned int i;
                    for (i = 0; i < info_ptr->num_palette; ++i) {
                        info_ptr->palette[i].red   >>=
                            (8 - info_ptr->sig_bit.red);
                        info_ptr->palette[i].green >>=
                            (8 - info_ptr->sig_bit.green);
                        info_ptr->palette[i].blue  >>=
                            (8 - info_ptr->sig_bit.blue);
                    }
                    *maxvalP = (1l << info_ptr->sig_bit.red) - 1;
                    if (verbose)
                        pm_message ("image has fewer significant bits, "
                                    "writing file with %d bits per channel", 
                                    info_ptr->sig_bit.red);
                } else
                    if ((info_ptr->color_type == PNG_COLOR_TYPE_RGB ||
                         info_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA) &&
                        (info_ptr->sig_bit.red < info_ptr->bit_depth)) {
                        png_set_shift(pngxP->png_ptr, &(info_ptr->sig_bit));
                        *maxvalP = (1l << info_ptr->sig_bit.red) - 1;
                        if (verbose)
                            pm_message("image has fewer significant bits, "
                                       "writing file with %d "
                                       "bits per channel", 
                                       info_ptr->sig_bit.red);
                    } else 
                        if ((info_ptr->color_type == PNG_COLOR_TYPE_GRAY ||
                             info_ptr->color_type ==
                                 PNG_COLOR_TYPE_GRAY_ALPHA) &&
                            (info_ptr->sig_bit.gray < info_ptr->bit_depth)) {
                            png_set_shift(pngxP->png_ptr, &info_ptr->sig_bit);
                            *maxvalP = (1l << info_ptr->sig_bit.gray) - 1;
                            if (verbose)
                                pm_message("image has fewer significant bits, "
                                           "writing file with %d bits",
                                           info_ptr->sig_bit.gray);
                        }
            }
            break;

        case ALPHA_ONLY:
            if ((info_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
                 info_ptr->color_type == PNG_COLOR_TYPE_GRAY_ALPHA) && 
                (info_ptr->sig_bit.gray < info_ptr->bit_depth)) {
                png_set_shift(pngxP->png_ptr, &info_ptr->sig_bit);
                if (verbose)
                    pm_message ("image has fewer significant bits, "
                                "writing file with %d bits", 
                                info_ptr->sig_bit.alpha);
                *maxvalP = (1l << info_ptr->sig_bit.alpha) - 1;
            }
            break;

        }
    }
}



static bool
imageHasColor(struct pngx * const pngxP) {

    bool retval;

    if (pngxP->info_ptr->color_type == PNG_COLOR_TYPE_GRAY ||
        pngxP->info_ptr->color_type == PNG_COLOR_TYPE_GRAY_ALPHA)

        retval = FALSE;
    else if (pngxP->info_ptr->color_type == PNG_COLOR_TYPE_PALETTE) {
        bool foundColor;
        unsigned int i;
            
        for (i = 0, foundColor = FALSE;
             i < pngxP->info_ptr->num_palette && !foundColor;
             ++i) {
            if (iscolor(pngxP->info_ptr->palette[i]))
                foundColor = TRUE;
        }
        retval = foundColor;
    } else
        retval = TRUE;

    return retval;
}



static void
determineOutputType(struct pngx *       const pngxP,
                    enum alpha_handling const alphaHandling,
                    pngcolor            const bgColor,
                    xelval              const maxval,
                    int *               const pnmTypeP) {

    if (alphaHandling != ALPHA_ONLY &&
        (imageHasColor(pngxP) || !isGrayscale(bgColor)))
        *pnmTypeP = PPM_TYPE;
    else {
        if (maxval > 1)
            *pnmTypeP = PGM_TYPE;
        else
            *pnmTypeP = PBM_TYPE;
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

    } else if (pngxP->info_ptr->valid & PNG_INFO_bKGD) {
        /* didn't manage to get libpng to work (bugs?) concerning background
           processing, therefore we do our own.
        */
        switch (pngxP->info_ptr->color_type) {
        case PNG_COLOR_TYPE_GRAY:
        case PNG_COLOR_TYPE_GRAY_ALPHA:
            bgColorP->r = bgColorP->g = bgColorP->b = 
                gamma_correct(pngxP->info_ptr->background.gray, totalgamma);
            break;
        case PNG_COLOR_TYPE_PALETTE: {
            png_color const rawBgcolor = 
                pngxP->info_ptr->palette[pngxP->info_ptr->background.index];
            bgColorP->r = gamma_correct(rawBgcolor.red, totalgamma);
            bgColorP->g = gamma_correct(rawBgcolor.green, totalgamma);
            bgColorP->b = gamma_correct(rawBgcolor.blue, totalgamma);
        }
        break;
        case PNG_COLOR_TYPE_RGB:
        case PNG_COLOR_TYPE_RGB_ALPHA: {
            png_color_16 const rawBgcolor = pngxP->info_ptr->background;
            
            bgColorP->r = gamma_correct(rawBgcolor.red,   totalgamma);
            bgColorP->g = gamma_correct(rawBgcolor.green, totalgamma);
            bgColorP->b = gamma_correct(rawBgcolor.blue,  totalgamma);
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

    if (pngxP->info_ptr->valid & PNG_INFO_pHYs) {
        float const r =
            (float)pngxP->info_ptr->x_pixels_per_unit /
            pngxP->info_ptr->y_pixels_per_unit;

        if (r != 1.0) {
            pm_message ("warning - non-square pixels; "
                        "to fix do a 'pamscale -%cscale %g'",
                        r < 1.0 ? 'x' : 'y',
                        r < 1.0 ? 1.0 / r : r );
            *errorLevelP = PNMTOPNG_WARNING_LEVEL;
        }
    }
}



#define GET_PNG_VAL(p) get_png_val(&(p), pngxP->info_ptr->bit_depth)



static void
makeXelRow(xel *               const xelrow,
           xelval              const maxval,
           int                 const pnmType,
           struct pngx *       const pngxP,
           const png_byte *    const pngRasterRow,
           pngcolor            const bgColor,
           enum alpha_handling const alphaHandling,
           double              const totalgamma) {

    const png_byte * pngPixelP;
    unsigned int col;

    pngPixelP = &pngRasterRow[0];  /* initial value */
    for (col = 0; col < pngxP->info_ptr->width; ++col) {
        switch (pngxP->info_ptr->color_type) {
        case PNG_COLOR_TYPE_GRAY: {
            pngcolor fgColor;
            fgColor.r = fgColor.g = fgColor.b = GET_PNG_VAL(pngPixelP);
            setXel(&xelrow[col], fgColor, bgColor, alphaHandling,
                   isTransparentColor(fgColor, pngxP, totalgamma) ?
                   0 : maxval);
        }
        break;

        case PNG_COLOR_TYPE_GRAY_ALPHA: {
            pngcolor fgColor;
            png_uint_16 alpha;

            fgColor.r = fgColor.g = fgColor.b = GET_PNG_VAL(pngPixelP);
            alpha = GET_PNG_VAL(pngPixelP);
            setXel(&xelrow[col], fgColor, bgColor, alphaHandling, alpha);
        }
        break;

        case PNG_COLOR_TYPE_PALETTE: {
            png_uint_16 const index        = GET_PNG_VAL(pngPixelP);
            png_color   const paletteColor = pngxP->info_ptr->palette[index];

            pngcolor fgColor;

            fgColor.r = paletteColor.red;
            fgColor.g = paletteColor.green;
            fgColor.b = paletteColor.blue;

            setXel(&xelrow[col], fgColor, bgColor, alphaHandling,
                   (pngxP->info_ptr->valid & PNG_INFO_tRNS) &&
                   index < pngxP->info_ptr->num_trans ?
                   pngxP->info_ptr->trans[index] : maxval);
        }
        break;
                
        case PNG_COLOR_TYPE_RGB: {
            pngcolor fgColor;

            fgColor.r = GET_PNG_VAL(pngPixelP);
            fgColor.g = GET_PNG_VAL(pngPixelP);
            fgColor.b = GET_PNG_VAL(pngPixelP);
            setXel(&xelrow[col], fgColor, bgColor, alphaHandling,
                   isTransparentColor(fgColor, pngxP, totalgamma) ?
                   0 : maxval);
        }
        break;

        case PNG_COLOR_TYPE_RGB_ALPHA: {
            pngcolor fgColor;
            png_uint_16 alpha;

            fgColor.r = GET_PNG_VAL(pngPixelP);
            fgColor.g = GET_PNG_VAL(pngPixelP);
            fgColor.b = GET_PNG_VAL(pngPixelP);
            alpha     = GET_PNG_VAL(pngPixelP);
            setXel(&xelrow[col], fgColor, bgColor, alphaHandling, alpha);
        }
        break;

        default:
            pm_error("unknown PNG color type: %d",
                     pngxP->info_ptr->color_type);
        }
    }
}



static void
writePnm(FILE *              const ofP,
         xelval              const maxval,
         int                 const pnmType,
         struct pngx *       const pngxP,
         png_byte **         const pngRaster,
         pngcolor            const bgColor,
         enum alpha_handling const alphaHandling,
         double              const totalgamma) {
/*----------------------------------------------------------------------------
   Write a PNM of either the image or the alpha mask, according to
   'alphaHandling' that is in the PNG image described by *pngxP and
   pngRaster[][].

   'pnmType' and 'maxval' are of the output image.

   Use background color 'bgColor' in the output if the PNG is such that a
   background color is needed.
-----------------------------------------------------------------------------*/
    int const plainFalse = 0;

    xel * xelrow;
    unsigned int row;

    if (verbose)
        pm_message("writing a %s file (maxval=%u)",
                   pnmType == PBM_TYPE ? "PBM" :
                   pnmType == PGM_TYPE ? "PGM" :
                   pnmType == PPM_TYPE ? "PPM" :
                   "UNKNOWN!", 
                   maxval);
    
    xelrow = pnm_allocrow(pngxP->info_ptr->width);

    pnm_writepnminit(stdout,
                     pngxP->info_ptr->width, pngxP->info_ptr->height, maxval,
                     pnmType, plainFalse);

    for (row = 0; row < pngxP->info_ptr->height; ++row) {
        makeXelRow(xelrow, maxval, pnmType, pngxP, pngRaster[row], bgColor,
                   alphaHandling, totalgamma);

        pnm_writepnmrow(ofP, xelrow, pngxP->info_ptr->width, maxval,
                        pnmType, plainFalse);
    }
    pnm_freerow (xelrow);
}



static void 
convertpng(FILE *             const ifP, 
           FILE *             const tfP, 
           struct cmdlineInfo const cmdline,
           int *              const errorLevelP) {

    png_byte ** pngRaster;
    int pnmType;
    pngcolor bgColor;
    float totalgamma;
    jmp_buf jmpbuf;
    struct pngx * pngxP;

    *errorLevelP = 0;

    if (setjmp(jmpbuf))
        pm_error ("setjmp returns error condition");

    pngx_createRead(&pngxP, &jmpbuf);

    readPng(pngxP, ifP, &pngRaster);

    if (verbose)
        dumpPngInfo(pngxP);

    if (cmdline.time)
        showTime(pngxP);
    if (tfP)
        saveText(pngxP, tfP);

    warnNonsquarePixels(pngxP, errorLevelP);

    setupGammaCorrection(pngxP, cmdline.gamma, &totalgamma);

    setupSignificantBits(pngxP, cmdline.alpha, &maxval, errorLevelP);

    getBackgroundColor(pngxP, cmdline.background, totalgamma, maxval,
                       &bgColor);

    determineOutputType(pngxP, cmdline.alpha, bgColor, maxval, &pnmType);

    writePnm(stdout, maxval, pnmType, pngxP, pngRaster, bgColor, 
             cmdline.alpha, totalgamma);

    fflush(stdout);

    freePngRaster(pngRaster, pngxP);

    pngx_destroy(pngxP);
}



int 
main(int argc, const char *argv[]) {

    struct cmdlineInfo cmdline;
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
