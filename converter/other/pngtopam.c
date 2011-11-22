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
#include <png.h>    /* includes zlib.h and setjmp.h */
#define VERSION "2.37.4 (5 December 1999) +netpbm"

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pam.h"

/* A hack until we can remove direct access to png_info from the program */
#if PNG_LIBPNG_VER >= 10400
#define trans_values trans_color
#define TRANS_ALPHA trans_alpha
#else
#define TRANS_ALPHA trans
#endif

typedef struct _jmpbuf_wrapper {
  jmp_buf jmpbuf;
} jmpbuf_wrapper;

enum alpha_handling {ALPHA_NONE, ALPHA_ONLY, ALPHA_MIX, ALPHA_IN};

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
static jmpbuf_wrapper pngtopnm_jmpbuf_struct;


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

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
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
         enum alpha_handling const alphaHandling,
         png_uint_16         const alpha) {

    if (alphaHandling == ALPHA_ONLY)
        tuple[0] = alpha;
    else if (alphaHandling == ALPHA_NONE ||
             (alphaHandling == ALPHA_MIX && alpha == maxval)) {
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
                alphaMix(foreground.r, background.r, alpha, maxval);
        else {
            tuple[PAM_RED_PLANE] =
                alphaMix(foreground.r, background.r, alpha, maxval);
            tuple[PAM_GRN_PLANE] =
                alphaMix(foreground.g, background.g, alpha, maxval);
            tuple[PAM_BLU_PLANE] =
                alphaMix(foreground.b, background.b, alpha, maxval);
        }
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



static int iscolor (png_color c)
{
  return c.red != c.green || c.green != c.blue;
}

static void save_text (png_info *info_ptr, FILE *tfp)
{
  int i, j, k;

  for (i = 0 ; i < info_ptr->num_text ; i++) {
    j = 0;
    while (info_ptr->text[i].key[j] != '\0' && info_ptr->text[i].key[j] != ' ')
      j++;    
    if (info_ptr->text[i].key[j] != ' ') {
      fprintf (tfp, "%s", info_ptr->text[i].key);
      for (j = strlen (info_ptr->text[i].key) ; j < 15 ; j++)
        putc (' ', tfp);
    } else {
      fprintf (tfp, "\"%s\"", info_ptr->text[i].key);
      for (j = strlen (info_ptr->text[i].key) ; j < 13 ; j++)
        putc (' ', tfp);
    }
    putc (' ', tfp); /* at least one space between key and text */
    
    for (j = 0 ; j < info_ptr->text[i].text_length ; j++) {
      putc (info_ptr->text[i].text[j], tfp);
      if (info_ptr->text[i].text[j] == '\n')
        for (k = 0 ; k < 16 ; k++)
          putc ((int)' ', tfp);
    }
    putc ((int)'\n', tfp);
  }
}

static void show_time (png_info *info_ptr)
{
    static const char * const month[] = {
        "", "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };

  if (info_ptr->valid & PNG_INFO_tIME) {
    if (info_ptr->mod_time.month < 1 ||
      info_ptr->mod_time.month >= ARRAY_SIZE(month)) {
      pm_message("tIME chunk in PNG input is invalid; "
                 "modification time of image is unknown.  "
                 "The month value, which should be in the range "
                 "1-12, is %u", info_ptr->mod_time.month);
    } else
    pm_message ("modification time: %02d %s %d %02d:%02d:%02d",
                info_ptr->mod_time.day, month[info_ptr->mod_time.month],
                info_ptr->mod_time.year, info_ptr->mod_time.hour,
                info_ptr->mod_time.minute, info_ptr->mod_time.second);
  }
}

static void pngtopnm_error_handler (png_structp png_ptr, png_const_charp msg)
{
  jmpbuf_wrapper  *jmpbuf_ptr;

  /* this function, aside from the extra step of retrieving the "error
   * pointer" (below) and the fact that it exists within the application
   * rather than within libpng, is essentially identical to libpng's
   * default error handler.  The second point is critical:  since both
   * setjmp() and longjmp() are called from the same code, they are
   * guaranteed to have compatible notions of how big a jmp_buf is,
   * regardless of whether _BSD_SOURCE or anything else has (or has not)
   * been defined. */

  pm_message("fatal libpng error: %s", msg);

  jmpbuf_ptr = png_get_error_ptr(png_ptr);
  if (jmpbuf_ptr == NULL) {
      /* we are completely hosed now */
      pm_error("EXTREMELY fatal error: jmpbuf unrecoverable; terminating.");
  }

  longjmp(jmpbuf_ptr->jmpbuf, 1);
}



static void
dump_png_info(png_info *info_ptr) {

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

    if (info_ptr->valid & PNG_INFO_hIST)
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



static unsigned int
computePngLineSize(png_info * const pngInfoP) {

    unsigned int const bytesPerSample = pngInfoP->bit_depth == 16 ? 2 : 1;

    unsigned int samplesPerPixel;

    switch (pngInfoP->color_type) {
    case PNG_COLOR_TYPE_GRAY_ALPHA: samplesPerPixel = 2; break;
    case PNG_COLOR_TYPE_RGB:        samplesPerPixel = 3; break;
    case PNG_COLOR_TYPE_RGB_ALPHA:  samplesPerPixel = 4; break;
    default:                        samplesPerPixel = 1;
    }

    if (UINT_MAX / bytesPerSample / samplesPerPixel < pngInfoP->width)
        pm_error("Width %u of PNG is uncomputably large",
                 (unsigned int)pngInfoP->width);
       
    return pngInfoP->width * bytesPerSample * samplesPerPixel;
}



static void
allocPngRaster(png_info *   const pngInfoP,
               png_byte *** const pngImageP) {

    unsigned int const lineSize = computePngLineSize(pngInfoP);

    png_byte ** pngImage;
    unsigned int row;

    MALLOCARRAY(pngImage, pngInfoP->height);

    if (pngImage == NULL)
        pm_error("couldn't allocate space for %u PNG raster rows",
                 (unsigned int)pngInfoP->height);

    for (row = 0; row < pngInfoP->height; ++row) {
        MALLOCARRAY(pngImage[row], lineSize);
        if (pngImage[row] == NULL)
            pm_error("couldn't allocate space for %uth row of PNG raster",
                     row);
    }
    *pngImageP = pngImage;
}



static void
freePngRaster(png_byte ** const pngRaster,
              png_info *  const pngInfoP) {

    unsigned int row;

    for (row = 0; row < pngInfoP->height; ++row)
        free(pngRaster[row]);

    free(pngRaster);
}



static bool
isTransparentColor(pngcolor   const color,
                   png_info * const pngInfoP,
                   double     const totalgamma) {
/*----------------------------------------------------------------------------
   Return TRUE iff pixels of color 'color' are supposed to be transparent
   everywhere they occur.  Assume it's an RGB image.

   'color' has been gamma-corrected.
-----------------------------------------------------------------------------*/
    bool retval;

    if (pngInfoP->valid & PNG_INFO_tRNS) {
        const png_color_16 * const transColorP = &pngInfoP->trans_values;

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
    
        switch (pngInfoP->color_type) {
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



#define SIG_CHECK_SIZE 4

static void
read_sig_buf(FILE * const ifP) {

    unsigned char sig_buf[SIG_CHECK_SIZE];
    size_t bytesRead;

    bytesRead = fread(sig_buf, 1, SIG_CHECK_SIZE, ifP);
    if (bytesRead != SIG_CHECK_SIZE)
        pm_error ("input file is empty or too short");

    if (png_sig_cmp(sig_buf, (png_size_t) 0, (png_size_t) SIG_CHECK_SIZE)
        != 0)
        pm_error ("input file is not a PNG file");
}



static void
setupGammaCorrection(png_struct * const png_ptr,
                     png_info *   const info_ptr,
                     float        const displaygamma,
                     float *      const totalgammaP) {

    if (displaygamma == -1.0)
        *totalgammaP = -1.0;
    else {
        float imageGamma;
        if (info_ptr->valid & PNG_INFO_gAMA)
            imageGamma = info_ptr->gamma;
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
            png_set_gamma(png_ptr, displaygamma, imageGamma);
            *totalgammaP = imageGamma * displaygamma;
            /* in case of gamma-corrections, sBIT's as in the
               PNG-file are not valid anymore 
            */
            info_ptr->valid &= ~PNG_INFO_sBIT;
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
                if (info_ptr->TRANS_ALPHA[i] != 0 &&
                    info_ptr->TRANS_ALPHA[i] != maxval) {
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
getComponentSbitFg(png_info * const pngInfoP,
                   png_byte * const fgSbitP,
                   bool *     const notUniformP) {

    if (pngInfoP->color_type == PNG_COLOR_TYPE_RGB ||
        pngInfoP->color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
        pngInfoP->color_type == PNG_COLOR_TYPE_PALETTE) {
        if (pngInfoP->sig_bit.red == pngInfoP->sig_bit.blue &&
            pngInfoP->sig_bit.red == pngInfoP->sig_bit.green) {
            *notUniformP = false;
            *fgSbitP     = pngInfoP->sig_bit.red;
        } else
            *notUniformP = true;
    } else {
        /* It has only a gray channel so it's obviously uniform */
        *notUniformP = false;
        *fgSbitP     = pngInfoP->sig_bit.gray;
    }
}



static void
getComponentSbit(png_info *          const pngInfoP,
                 enum alpha_handling const alphaHandling,
                 png_byte *          const componentSbitP,
                 bool *              const notUniformP) {

    switch (alphaHandling) {

    case ALPHA_ONLY:
        /* We care only about the alpha channel, so the uniform Sbit is
           the alpha Sbit
        */
        *notUniformP = false;
        *componentSbitP = pngInfoP->sig_bit.alpha;
        break;
    case ALPHA_NONE:
    case ALPHA_MIX:
        /* We aren't going to produce an alpha channel, so we care only
           about the uniformity of the foreground channels.
        */
        getComponentSbitFg(pngInfoP, componentSbitP, notUniformP);
        break;
    case ALPHA_IN: {
        /* We care about both the foreground and the alpha */
        bool fgNotUniform;
        png_byte fgSbit;
        
        getComponentSbitFg(pngInfoP, &fgSbit, &fgNotUniform);

        if (fgNotUniform)
            *notUniformP = true;
        else {
            if (fgSbit == pngInfoP->sig_bit.alpha) {
                *notUniformP    = false;
                *componentSbitP = fgSbit;
            } else
                *notUniformP = true;
        }
    } break;
    }
}

                 

static void
shiftPalette(png_info *   const pngInfoP,
             unsigned int const shift) {
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
        unsigned int i;
        
        for (i = 0; i < pngInfoP->num_palette; ++i) {
            pngInfoP->palette[i].red   >>= (8 - shift);
            pngInfoP->palette[i].green >>= (8 - shift);
            pngInfoP->palette[i].blue  >>= (8 - shift);
        }
    }
}



static void
computeMaxvalFromSbit(png_struct *        const pngP,
                      png_info *          const pngInfoP,
                      enum alpha_handling const alphaHandling,
                      png_uint_16 *       const maxvalP,
                      bool *              const succeededP,
                      int *               const errorlevelP) {

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

    getComponentSbit(pngInfoP, alphaHandling, &componentSigBit, &notUniform);

    if (notUniform) {
        pm_message("This program cannot handle "
                   "different bit depths for color channels");
        pm_message("writing file with %u bit resolution", pngInfoP->bit_depth);
        *succeededP = false;
        *errorlevelP = PNMTOPNG_WARNING_LEVEL;
    } else if (componentSigBit > 15) {
        pm_message("Invalid PNG: says %u significant bits for a component; "
                   "max possible is 16.  Ignoring sBIT chunk.",
                   componentSigBit);
        *succeededP = false;
        *errorlevelP = PNMTOPNG_WARNING_LEVEL;
    } else {
        if (alphaHandling == ALPHA_MIX &&
            (pngInfoP->color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
             pngInfoP->color_type == PNG_COLOR_TYPE_GRAY_ALPHA ||
             paletteHasPartialTransparency(pngInfoP)))
            *succeededP = false;
        else {
            if (componentSigBit < pngInfoP->bit_depth) {
                pm_message("Image has fewer significant bits, "
                           "writing file with %u bits", componentSigBit);
                *maxvalP = (1l << componentSigBit) - 1;
                *succeededP = true;

                if (pngInfoP->color_type == PNG_COLOR_TYPE_PALETTE)
                    shiftPalette(pngInfoP, componentSigBit);
                else
                    png_set_shift(pngP, &pngInfoP->sig_bit);
            } else
                *succeededP = false;
        }
    }
}



static void
setupSignificantBits(png_struct *        const pngP,
                     png_info *          const pngInfoP,
                     enum alpha_handling const alphaHandling,
                     png_uint_16 *       const maxvalP,
                     int *               const errorlevelP) {
/*----------------------------------------------------------------------------
  Figure out what maxval would best express the information in the PNG
  described by *pngP and *pngInfoP, with 'alpha' telling which
  information in the PNG we care about (image or alpha mask).

  Return the result as *maxvalP.

  Also set up *pngP for the corresponding significant bits.
-----------------------------------------------------------------------------*/
    bool gotItFromSbit;
    
    if (pngInfoP->valid & PNG_INFO_sBIT)
        computeMaxvalFromSbit(pngP, pngInfoP, alphaHandling,
                              maxvalP, &gotItFromSbit, errorlevelP);
    else
        gotItFromSbit = false;

    if (!gotItFromSbit) {
        if (pngInfoP->color_type == PNG_COLOR_TYPE_PALETTE) {
            if (alphaHandling == ALPHA_ONLY) {
                if (pngInfoP->color_type == PNG_COLOR_TYPE_GRAY ||
                    pngInfoP->color_type == PNG_COLOR_TYPE_RGB)
                    /* The alpha mask will be all opaque, so maxval 1
                       is plenty
                    */
                    *maxvalP = 1;
                else if (paletteHasPartialTransparency(pngInfoP))
                    /* Use same maxval as PNG transparency palette for
                       simplicity
                    */
                    *maxvalP = 255;
                else
                    /* A common case, so we conserve bits */
                    *maxvalP = 1;
            } else
                /* Use same maxval as PNG palette for simplicity */
                *maxvalP = 255;
        } else {
            *maxvalP = (1l << pngInfoP->bit_depth) - 1;
        }
    }
}



static bool
imageHasColor(png_info * const info_ptr) {

    bool retval;

    if (info_ptr->color_type == PNG_COLOR_TYPE_GRAY ||
        info_ptr->color_type == PNG_COLOR_TYPE_GRAY_ALPHA)

        retval = FALSE;
    else if (info_ptr->color_type == PNG_COLOR_TYPE_PALETTE) {
        bool foundColor;
        unsigned int i;
            
        for (i = 0, foundColor = FALSE;
             i < info_ptr->num_palette && !foundColor;
             ++i) {
            if (iscolor(info_ptr->palette[i]))
                foundColor = TRUE;
        }
        retval = foundColor;
    } else
        retval = TRUE;

    return retval;
}



static void
determineOutputType(png_info *          const pngInfoP,
                    enum alpha_handling const alphaHandling,
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
            imageHasColor(pngInfoP) || !isGrayscale(bgColor);

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
getBackgroundColor(png_info *   const info_ptr,
                   const char * const requestedColor,
                   float        const totalgamma,
                   xelval       const maxval,
                   pngcolor *   const bgColorP) {
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

    } else if (info_ptr->valid & PNG_INFO_bKGD) {
        /* didn't manage to get libpng to work (bugs?) concerning background
           processing, therefore we do our own.
        */
        switch (info_ptr->color_type) {
        case PNG_COLOR_TYPE_GRAY:
        case PNG_COLOR_TYPE_GRAY_ALPHA:
            bgColorP->r = bgColorP->g = bgColorP->b = 
                gamma_correct(info_ptr->background.gray, totalgamma);
            break;
        case PNG_COLOR_TYPE_PALETTE: {
            png_color const rawBgcolor = 
                info_ptr->palette[info_ptr->background.index];
            bgColorP->r = gamma_correct(rawBgcolor.red, totalgamma);
            bgColorP->g = gamma_correct(rawBgcolor.green, totalgamma);
            bgColorP->b = gamma_correct(rawBgcolor.blue, totalgamma);
        }
        break;
        case PNG_COLOR_TYPE_RGB:
        case PNG_COLOR_TYPE_RGB_ALPHA: {
            png_color_16 const rawBgcolor = info_ptr->background;
            
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



#define GET_PNG_VAL(p) get_png_val(&(p), pngInfoP->bit_depth)



static void
makeTupleRow(const struct pam *  const pamP,
             const tuple *       const tuplerow,
             png_info *          const pngInfoP,
             const png_byte *    const pngRasterRow,
             pngcolor            const bgColor,
             enum alpha_handling const alphaHandling,
             double              const totalgamma) {

    const png_byte * pngPixelP;
    unsigned int col;

    pngPixelP = &pngRasterRow[0];  /* initial value */
    for (col = 0; col < pngInfoP->width; ++col) {
        switch (pngInfoP->color_type) {
        case PNG_COLOR_TYPE_GRAY: {
            pngcolor fgColor;
            fgColor.r = fgColor.g = fgColor.b = GET_PNG_VAL(pngPixelP);
            setTuple(pamP, tuplerow[col], fgColor, bgColor, alphaHandling,
                   isTransparentColor(fgColor, pngInfoP, totalgamma) ?
                   0 : maxval);
        }
        break;

        case PNG_COLOR_TYPE_GRAY_ALPHA: {
            pngcolor fgColor;
            png_uint_16 alpha;

            fgColor.r = fgColor.g = fgColor.b = GET_PNG_VAL(pngPixelP);
            alpha = GET_PNG_VAL(pngPixelP);
            setTuple(pamP, tuplerow[col], fgColor, bgColor,
                     alphaHandling, alpha);
        }
        break;

        case PNG_COLOR_TYPE_PALETTE: {
            png_uint_16 const index        = GET_PNG_VAL(pngPixelP);
            png_color   const paletteColor = pngInfoP->palette[index];

            pngcolor fgColor;

            fgColor.r = paletteColor.red;
            fgColor.g = paletteColor.green;
            fgColor.b = paletteColor.blue;

            setTuple(pamP, tuplerow[col], fgColor, bgColor, alphaHandling,
                     (pngInfoP->valid & PNG_INFO_tRNS) &&
                     index < pngInfoP->num_trans ?
                     pngInfoP->TRANS_ALPHA[index] : maxval);
        }
        break;
                
        case PNG_COLOR_TYPE_RGB: {
            pngcolor fgColor;

            fgColor.r = GET_PNG_VAL(pngPixelP);
            fgColor.g = GET_PNG_VAL(pngPixelP);
            fgColor.b = GET_PNG_VAL(pngPixelP);
            setTuple(pamP, tuplerow[col], fgColor, bgColor, alphaHandling,
                     isTransparentColor(fgColor, pngInfoP, totalgamma) ?
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
            setTuple(pamP, tuplerow[col], fgColor, bgColor,
                     alphaHandling, alpha);
        }
        break;

        default:
            pm_error("unknown PNG color type: %d", pngInfoP->color_type);
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
        pm_message("Writing a PAM file with tuple type %s, maxval %u",
                   pamP->tuple_type, maxval);
        break;
    default:
        assert(false); /* Every possible value handled above */
    }
}
    


static void
writeNetpbm(struct pam *        const pamP,
            png_info *          const pngInfoP,
            png_byte **         const pngRaster,
            pngcolor            const bgColor,
            enum alpha_handling const alphaHandling,
            double              const totalgamma) {
/*----------------------------------------------------------------------------
   Write a Netpbm image of either the image or the alpha mask, according to
   'alphaHandling' that is in the PNG image described by 'pngInfoP' and
   pngRaster.

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

    for (row = 0; row < pngInfoP->height; ++row) {
        makeTupleRow(pamP, tuplerow, pngInfoP, pngRaster[row], bgColor,
                     alphaHandling, totalgamma);

        pnm_writepamrow(pamP, tuplerow);
    }
    pnm_freepamrow(tuplerow);
}



static void 
convertpng(FILE *             const ifp, 
           FILE *             const tfp, 
           struct cmdlineInfo const cmdline,
           int *              const errorlevelP) {

    png_struct * png_ptr;
    png_info * info_ptr;
    png_byte ** png_image;
    pngcolor bgColor;
    float totalgamma;
    struct pam pam;

    *errorlevelP = 0;

    read_sig_buf(ifp);

    png_ptr = png_create_read_struct(
        PNG_LIBPNG_VER_STRING,
        &pngtopnm_jmpbuf_struct, pngtopnm_error_handler, NULL);
    if (png_ptr == NULL)
        pm_error("cannot allocate main libpng structure (png_ptr)");

    info_ptr = png_create_info_struct (png_ptr);
    if (info_ptr == NULL)
        pm_error("cannot allocate LIBPNG structures");

    if (setjmp(pngtopnm_jmpbuf_struct.jmpbuf))
        pm_error ("setjmp returns error condition");

    png_init_io (png_ptr, ifp);
    png_set_sig_bytes (png_ptr, SIG_CHECK_SIZE);
    png_read_info (png_ptr, info_ptr);

    allocPngRaster(info_ptr, &png_image);

    if (info_ptr->bit_depth < 8)
        png_set_packing (png_ptr);

    setupGammaCorrection(png_ptr, info_ptr, cmdline.gamma, &totalgamma);

    setupSignificantBits(png_ptr, info_ptr, cmdline.alpha,
                         &maxval, errorlevelP);

    getBackgroundColor(info_ptr, cmdline.background, totalgamma, maxval,
                       &bgColor);

    png_read_image(png_ptr, png_image);
    png_read_end(png_ptr, info_ptr);

    if (verbose)
        /* Note that some of info_ptr is not defined until png_read_end() 
           completes.  That's because it comes from chunks that are at the
           end of the stream.
        */
        dump_png_info(info_ptr);

    if (cmdline.time)
        show_time(info_ptr);
    if (tfp)
        save_text(info_ptr, tfp);

    if (info_ptr->valid & PNG_INFO_pHYs) {
        float const r =
            (float)info_ptr->x_pixels_per_unit / info_ptr->y_pixels_per_unit;
        if (r != 1.0) {
            pm_message ("warning - non-square pixels; "
                        "to fix do a 'pamscale -%cscale %g'",
                        r < 1.0 ? 'x' : 'y',
                        r < 1.0 ? 1.0 / r : r );
            *errorlevelP = PNMTOPNG_WARNING_LEVEL;
        }
    }

    pam.size        = sizeof(pam);
    pam.len         = PAM_STRUCT_SIZE(tuple_type);
    pam.file        = stdout;
    pam.plainformat = 0;
    pam.height      = info_ptr->height;
    pam.width       = info_ptr->width;
    pam.maxval      = maxval;

    determineOutputType(info_ptr, cmdline.alpha, bgColor, maxval,
                        &pam.format, &pam.depth, pam.tuple_type);

    writeNetpbm(&pam, info_ptr, png_image, bgColor, cmdline.alpha, totalgamma);

    fflush(stdout);

    freePngRaster(png_image, info_ptr);

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
}



int 
main(int argc, const char *argv[]) {

    struct cmdlineInfo cmdline;
    FILE * ifP;
    FILE * tfP;
    int errorlevel;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    verbose = cmdline.verbose;

    ifP = pm_openr(cmdline.inputFilespec);

    if (cmdline.text)
        tfP = pm_openw(cmdline.text);
    else
        tfP = NULL;

    convertpng(ifP, tfP, cmdline, &errorlevel);

    if (tfP)
        pm_close(tfP);

    pm_close(ifP);
    pm_close(stdout);

    return errorlevel;
}
