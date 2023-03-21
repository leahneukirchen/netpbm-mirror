/*****************************************************************************
                                jpegtopnm
******************************************************************************
  This program is part of the Netpbm package.

  This program converts from the JFIF format, which is based on JPEG, to
  the fundamental ppm or pgm format (depending on whether the JFIF
  image is gray scale or color).

  This program is by Bryan Henderson on 2000.03.20, but is derived
  with permission from the program djpeg, which is in the Independent
  Jpeg Group's JPEG library package.  Under the terms of that permission,
  redistribution of this software is restricted as described in the
  file README.JPEG.

  Copyright (C) 1991-1998, Thomas G. Lane.

  TODO:

    For CMYK and YCCK JPEG input, optionally produce a 4-deep PAM
    output containing CMYK values.  Define a tupletype for this.
    Extend pamtoppm to convert this to ppm using the standard
    transformation.

    See if additional decompressor options effects significant speedup.
    grayscale output of color image, downscaling, color quantization, and
    dithering are possibilities.  Djpeg's man page says they make it faster.

  IMPLEMENTATION NOTE - PRECISION

    There are two versions of the JPEG library.  One handles only JPEG
    files with 8 bit samples; the other handles only 12 bit files.
    This program may be compiled and linked against either, and run
    dynamically linked to either at runtime independently.  It uses
    the precision information from the file header.  Note that when
    the input has 12 bit precision, this program generates PPM files
    with two-byte samples, but when the input has 8 bit precision, it
    generates PPM files with one-byte samples.  One should use
    Pnmdepth to reduce precision to 8 bits if one-byte-sample output
    is essential.

  IMPLEMENTATION NOTE - EXIF

    See http://exif.org.  See the programs Exifdump
    (http://topo.math.u-psud.fr/~bousch/exifdump.py) and Jhead
    (http://www.sentex.net/~mwandel/jhead).


*****************************************************************************/

#define _DEFAULT_SOURCE 1  /* New name for SVID & BSD source defines */
#define _BSD_SOURCE 1      /* Make sure strdup() is in string.h */
#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */

#include <stdbool.h>
#include <ctype.h>		/* to declare isprint() */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
/* Note: jpeglib.h prerequires stdlib.h and ctype.h.  It should include them
   itself, but doesn't.
*/
#include <jpeglib.h>

#include "pm_c_util.h"
#include "pnm.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "nstring.h"
#include "exif.h"
#include "jpegdatasource.h"

#define EXIT_WARNING 2  /* Goes with EXIT_FAILURE, EXIT_SUCCESS in stdlib.h */

enum Inklevel {NORMAL, ADOBE, GUESS};
   /* This describes image samples that represent ink levels.  NORMAL
      means 0 is no ink; ADOBE means 0 is maximum ink.  GUESS means we
      don't know what 0 means, so we have to guess from information in
      the image.
      */

enum Colorspace {
    /* These are the color spaces in which we can get pixels from the
       JPEG decompressor.  We include only those that are possible
       given our particular inputs to the decompressor.  The
       decompressor is theoretically capable of other, e.g. YCCK.
       Unlike the JPEG library, this type distinguishes between the
       Adobe and non-Adobe style of CMYK samples.
    */
    GRAYSCALE_COLORSPACE,
    RGB_COLORSPACE,
    CMYK_NORMAL_COLORSPACE,
    CMYK_ADOBE_COLORSPACE
    };

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    char * inputFileName;
    char * exifFileName;
        /* Name of file in which to save EXIF information.  NULL means don't
           save.  "-" means standard output
        */
    unsigned int  verbose;
    unsigned int  nosmooth;
    J_DCT_METHOD  dctMethod;
    long int      maxMemoryToUse;
    unsigned int  traceLevel;
    enum Inklevel inklevel;
    unsigned int  comments;
    unsigned int  dumpexif;
    unsigned int  traceexif;
    unsigned int  multiple;
    unsigned int  repair;
};


static bool displayComments;
    /* User wants comments from the JPEG to be displayed */

static void
interpretMaxmemory(bool         const maxmemorySpec,
                   const char * const maxmemory,
                   long int *   const maxMemoryToUseP) {
/*----------------------------------------------------------------------------
   Interpret the "maxmemory" command line option.
-----------------------------------------------------------------------------*/
    long int lval;
    char ch;

    if (!maxmemorySpec) {
        *maxMemoryToUseP = -1;  /* unspecified */
    } else if (sscanf(maxmemory, "%ld%c", &lval, &ch) < 1) {
        pm_error("Invalid value for --maxmemory option: '%s'.", maxmemory);
    } else {
        if (ch == 'm' || ch == 'M') lval *= 1000L;
        *maxMemoryToUseP = lval * 1000L;
    }
}


static void
interpretAdobe(int             const adobe,
               int             const notadobe,
               enum Inklevel * const inklevelP) {
/*----------------------------------------------------------------------------
   Interpret the adobe/notadobe command line options
-----------------------------------------------------------------------------*/
    if (adobe && notadobe)
        pm_error("You cannot specify both -adobe and -notadobe options.");
    else {
        if (adobe)
            *inklevelP = ADOBE;
        else if (notadobe)
            *inklevelP = NORMAL;
        else
            *inklevelP = GUESS;
    }
}



static void
parseCommandLine(int                  const argc,
                 char **              const argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that many of the strings that this function returns in the
   *cmdlineP structure are actually in the supplied argv array.  And
   sometimes, one of these strings is actually just a suffix of an entry
   in argv!

   On the other hand, unlike other option processing functions, we do
   not change argv at all.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int i;  /* local loop variable */

    const char * maxmemory;
    const char * dctval;
    unsigned int adobe, notadobe;

    unsigned int tracelevelSpec, exifSpec, dctvalSpec, maxmemorySpec;
    unsigned int option_def_index;

    int argc_parse;       /* argc, except we modify it as we parse */
    char ** argv_parse;

    MALLOCARRAY_NOFAIL(option_def, 100);
    MALLOCARRAY_NOFAIL(argv_parse, argc);

    /* argv, except we modify it as we parse */

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "verbose",     OPT_FLAG,   NULL, &cmdlineP->verbose,       0);
    OPTENT3(0, "dct",         OPT_STRING, &dctval,
            &dctvalSpec, 0);
    OPTENT3(0, "maxmemory",   OPT_STRING, &maxmemory,
            &maxmemorySpec, 0);
    OPTENT3(0, "nosmooth",    OPT_FLAG,   NULL, &cmdlineP->nosmooth,      0);
    OPTENT3(0, "tracelevel",  OPT_UINT,   &cmdlineP->traceLevel,
            &tracelevelSpec, 0);
    OPTENT3(0, "adobe",       OPT_FLAG,   NULL, &adobe,                   0);
    OPTENT3(0, "notadobe",    OPT_FLAG,   NULL, &notadobe,                0);
    OPTENT3(0, "comments",    OPT_FLAG,   NULL, &cmdlineP->comments,      0);
    OPTENT3(0, "exif",        OPT_STRING, &cmdlineP->exifFileName,
            &exifSpec, 0);
    OPTENT3(0, "dumpexif",    OPT_FLAG,   NULL, &cmdlineP->dumpexif,      0);
    OPTENT3(0, "multiple",    OPT_FLAG,   NULL, &cmdlineP->multiple,      0);
    OPTENT3(0, "repair",      OPT_FLAG,   NULL, &cmdlineP->repair,        0);
    OPTENT3(0, "traceexif",   OPT_FLAG,   NULL, &cmdlineP->traceexif,     0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We may have parms that are negative numbers */

    /* Make private copy of arguments for pm_optParseOptions to corrupt */
    argc_parse = argc;
    for (i=0; i < argc; ++i)
        argv_parse[i] = argv[i];

    pm_optParseOptions3(&argc_parse, argv_parse, opt, sizeof(opt), 0);
        /* Uses and sets argc_parse, argv_parse,
           and some of *cmdlineP and others. */

    if (!tracelevelSpec)
        cmdlineP->traceLevel = 0;

    if (!exifSpec)
        cmdlineP->exifFileName = NULL;

    if (argc_parse - 1 == 0)
        cmdlineP->inputFileName = strdup("-");  /* he wants stdin */
    else if (argc_parse - 1 == 1)
        cmdlineP->inputFileName = strdup(argv_parse[1]);
    else
        pm_error("Too many arguments.  The only argument accepted "
                 "is the input file specification");

    if (!dctvalSpec)
        cmdlineP->dctMethod = JDCT_DEFAULT;
    else {
        if (streq(dctval, "int"))
            cmdlineP->dctMethod = JDCT_ISLOW;
        else if (streq(dctval, "fast"))
            cmdlineP->dctMethod = JDCT_IFAST;
        else if (streq(dctval, "float"))
            cmdlineP->dctMethod = JDCT_FLOAT;
        else pm_error("Invalid value for the --dct option: '%s'.", dctval);
    }

    interpretMaxmemory(maxmemorySpec, maxmemory,
                       &cmdlineP->maxMemoryToUse);

    interpretAdobe(adobe, notadobe, &cmdlineP->inklevel);

    free(argv_parse);
}


/*
 * Marker processor for COM and interesting APPn markers.
 * This replaces the library's built-in processor, which just skips the marker.
 * We want to print out the marker as text, to the extent possible.
 * Note this code relies on a non-suspending data source.
 */

#if 0
static unsigned int
jpegGetc (j_decompress_ptr const cinfoP) {

/* Read next byte */
    struct jpeg_source_mgr * datasrcP = cinfoP->src;

    if (datasrcP->bytes_in_buffer == 0) {
        if (! (*datasrcP->fill_input_buffer) (cinfoP))
            pm_error("Can't suspend here.");
    }
    datasrcP->bytes_in_buffer--;
    return GETJOCTET(*datasrcP->next_input_byte++);
}


static bool
printTextMarker(j_decompress_ptr const cinfoP) {
/*----------------------------------------------------------------------------
   This is a routine that you can register with the Jpeg decompressor
   with e.g.

     jpeg_set_marker_processor(cinfoP, JPEG_APP0 + appType,
                               printTextMarker);

  The decompressor then calls it when it encounters a miscellaneous marker
  of the specified type (e.g. APP1).  At that time, the jpeg input stream
  is positioned to the marker contents -- first 2 bytes of length information,
  MSB first, where the length includes those two bytes, then the data.

  We just get and print the contents of the marker.

  This routine is no longer used; it is kept as an example in case we want
  to use it in the future.  Instead, we use jpeg_save_markers() and have
  the Jpeg library store all the markers in memory for our later access.
-----------------------------------------------------------------------------*/
    const bool traceit = (cinfoP->err->trace_level >= 1);
    const bool display_value =
        traceit || (cinfoP->unread_marker == JPEG_COM && displayComments);

    INT32 length;
    unsigned int ch;
    unsigned int lastch;

    lastch = 0;  /* initial value */

    length = jpeg_getc(cinfoP) << 8;
    length += jpeg_getc(cinfoP);
    length -= 2;			/* discount the length word itself */

    if (traceit) {
        if (cinfoP->unread_marker == JPEG_COM)
            fprintf(stderr, "Comment, length %ld:\n", (long) length);
        else			/* assume it is an APPn otherwise */
            fprintf(stderr, "APP%d, length %ld:\n",
                    cinfoP->unread_marker - JPEG_APP0, (long) length);
    }

    if (cinfoP->unread_marker == JPEG_COM && displayComments)
        fprintf(stderr, "COMMENT: ");

    while (--length >= 0) {
        ch = jpeg_getc(cinfoP);
        if (display_value) {
            /* Emit the character in a readable form.
             * Nonprintables are converted to \nnn form,
             * while \ is converted to \\.
             * Newlines in CR, CR/LF, or LF form will be printed as one
             * newline.
             */
            if (ch == '\r') {
              fprintf(stderr, "\n");
            } else if (ch == '\n') {
                if (lastch != '\r')
                    fprintf(stderr, "\n");
            } else if (ch == '\\') {
                fprintf(stderr, "\\\\");
            } else if (isprint(ch)) {
                putc(ch, stderr);
            } else {
                fprintf(stderr, "\\%03o", ch);
            }
          lastch = ch;
        }
    }

    if (display_value)
        fprintf(stderr, "\n");

    return true;
}
#endif



static void
printMarker(struct jpeg_marker_struct const marker) {

    if (marker.original_length != marker.data_length) {
        /* This should be impossible, because we asked for up to 65535
           bytes, and the jpeg spec doesn't allow anything bigger than that.
        */
        pm_message("INTERNAL ERROR: %d of %d bytes of marker were "
                   "saved.", marker.data_length, marker.original_length);
    }

    {
        int i;
        JOCTET lastch;

        lastch = 0;  /* initial value */
        for (i = 0; i < marker.data_length; i++) {
            /* Emit the character in a readable form.
             * Nonprintables are converted to \nnn form,
             * while \ is converted to \\.
             * Newlines in CR, CR/LF, or LF form will be printed as one
             * newline.
             */
            if (marker.data[i] == '\r') {
                fprintf(stderr, "\n");
            } else if (marker.data[i] == '\n') {
                if (lastch != '\r')
                    fprintf(stderr, "\n");
            } else if (marker.data[i] == '\\') {
                fprintf(stderr, "\\\\");
            } else if (isprint(marker.data[i])) {
                putc(marker.data[i], stderr);
            } else {
                fprintf(stderr, "\\%03o", marker.data[i]);
            }
            lastch = marker.data[i];
        }
        fprintf(stderr, "\n");
    }
}


typedef struct rgb {unsigned int r; unsigned int g; unsigned int b;} rgb_type;


static rgb_type *
read_rgb(JSAMPLE *       const ptr,
         enum Colorspace const colorspace,
         unsigned int    const maxval) {
/*----------------------------------------------------------------------------
  Return the RGB triple corresponding to the color of the JPEG pixel at
  'ptr', which is in color space 'color_space'.

  Assume 'maxval' is the maximum sample value in the input pixel, and also
  use it for the maximum sample value in the return value.
-----------------------------------------------------------------------------*/
    static rgb_type rgb;  /* Our return value */

    switch (colorspace) {
    case RGB_COLORSPACE: {
        rgb.r = GETJSAMPLE(*(ptr+0));
        rgb.g = GETJSAMPLE(*(ptr+1));
        rgb.b = GETJSAMPLE(*(ptr+2));
    } break;
    case CMYK_NORMAL_COLORSPACE: {
        int const c = GETJSAMPLE(*(ptr+0));
        int const m = GETJSAMPLE(*(ptr+1));
        int const y = GETJSAMPLE(*(ptr+2));
        int const k = GETJSAMPLE(*(ptr+3));

        /* I swapped m and y below, because they looked wrong.
           -Bryan 2000.08.20
           */
        rgb.r = ((maxval-k)*(maxval-c))/maxval;
        rgb.g = ((maxval-k)*(maxval-m))/maxval;
        rgb.b = ((maxval-k)*(maxval-y))/maxval;
    } break;
    case CMYK_ADOBE_COLORSPACE: {
        int const c = GETJSAMPLE(*(ptr+0));
        int const m = GETJSAMPLE(*(ptr+1));
        int const y = GETJSAMPLE(*(ptr+2));
        int const k = GETJSAMPLE(*(ptr+3));

        rgb.r = (k*c)/maxval;
        rgb.g = (k*m)/maxval;
        rgb.b = (k*y)/maxval;
    } break;
    default:
        pm_error("Internal error: unknown color space %d passed to "
                 "read_rgb().", (int) colorspace);
    }
    return &rgb;
}



/* pnmbuffer is declared global because it would be improper to pass a
   pointer to it as input to copy_pixel_row(), since it isn't
   logically a parameter of the operation, but rather is private to
   copy_pixel_row().  But it would be impractical to allocate and free
   the storage with every call to copy_pixel_row().
*/
static xel * pnmbuffer;      /* Output buffer.  Input to pnm_writepnmrow() */

static void
copyPixelRow(JSAMPROW        const jpegbuffer,
             unsigned int    const width,
             unsigned int    const samplesPerPixel,
             enum Colorspace const colorSpace,
             FILE *          const ofP,
             int             const format,
             xelval          const maxval) {

    JSAMPLE * ptr;
    unsigned int outputCursor;     /* Cursor into output buffer 'pnmbuffer' */

    ptr = &jpegbuffer[0];  /* Start at beginning of input row */

    for (outputCursor = 0; outputCursor < width; ++outputCursor) {
        xel currentPixel;
        if (samplesPerPixel >= 3) {
            const rgb_type * const rgbP = read_rgb(ptr, colorSpace, maxval);
            PPM_ASSIGN(currentPixel, rgbP->r, rgbP->g, rgbP->b);
        } else {
            PNM_ASSIGN1(currentPixel, GETJSAMPLE(*ptr));
        }
        ptr += samplesPerPixel;  /* move to next pixel of input */
        pnmbuffer[outputCursor] = currentPixel;
    }
    pnm_writepnmrow(ofP, pnmbuffer, width, maxval, format, false);
}



static void
setColorSpaces(J_COLOR_SPACE   const jpegColorSpace,
               int *           const outputTypeP,
               J_COLOR_SPACE * const outColorSpaceP) {
/*----------------------------------------------------------------------------
   Decide what type of output (PPM or PGM) we shall generate and what
   color space we must request from the JPEG decompressor, based on the
   color space of the input JPEG image, 'jpegColorSpace'.

   Write to stderr a message telling which type we picked.

   Exit the process with EXIT_FAILURE completion code and a message to
   stderr if the input color space is beyond our capability.
-----------------------------------------------------------------------------*/
    /* Note that the JPEG decompressor is not capable of translating
       CMYK or YCCK to RGB, but can translate YCCK to CMYK.
    */

    switch (jpegColorSpace) {
    case JCS_UNKNOWN:
        pm_error("Input JPEG image has 'unknown' color space "
                 "(JCS_UNKNOWN).  "
                 "We cannot interpret this image.");
        break;
    case JCS_GRAYSCALE:
        *outputTypeP    = PGM_TYPE;
        *outColorSpaceP = JCS_GRAYSCALE;
        break;
    case JCS_RGB:
        *outputTypeP    = PPM_TYPE;
        *outColorSpaceP = JCS_RGB;
        break;
    case JCS_YCbCr:
        *outputTypeP    = PPM_TYPE;
        *outColorSpaceP = JCS_RGB;
        /* Design note:  We found this YCbCr->RGB conversion increases
           user mode CPU time by 2.5%.  2002.10.12
        */
        break;
    case JCS_CMYK:
        *outputTypeP    = PPM_TYPE;
        *outColorSpaceP = JCS_CMYK;
        break;
    case JCS_YCCK:
        *outputTypeP    = PPM_TYPE;
        *outColorSpaceP = JCS_CMYK;
        break;
    default:
        pm_error("INTERNAL ERROR: unknown color space code %d passed "
                 "to setColorSpaces().", jpegColorSpace);
    }
    pm_message("WRITING %s FILE",
               *outputTypeP == PPM_TYPE ? "PPM" : "PGM");
}



static const char *
colorspaceName(J_COLOR_SPACE const jpegColorSpace) {

    const char * retval;

    switch(jpegColorSpace) {
    case JCS_UNKNOWN:   retval = "JCS_UNKNOWN";   break;
    case JCS_GRAYSCALE: retval = "JCS_GRAYSCALE"; break;
    case JCS_RGB:       retval = "JCS_RGB";       break;
    case JCS_YCbCr:     retval = "JCS_YCbCr";     break;
    case JCS_CMYK:      retval = "JCS_CMYK";      break;
    case JCS_YCCK:      retval = "JCS_YCCK";      break;
    default:            retval = "invalid";       break;
    };
    return retval;
}



static void
printVerboseInfoAboutHeader(struct jpeg_decompress_struct const cinfo){

    struct jpeg_marker_struct * markerP;

    pm_message("input color space is %d (%s)",
               cinfo.jpeg_color_space,
               colorspaceName(cinfo.jpeg_color_space));

    /* Note that raw information about marker, including marker type code,
       was already printed by the jpeg library, because of the jpeg library
       trace level >= 1.  Our job is to interpret it a little bit.
    */
    if (cinfo.marker_list)
        pm_message("Miscellaneous markers (excluding APP0, APP12) "
                   "in header:");
    else
        pm_message("No miscellaneous markers (excluding APP0, APP12) "
                   "in header");
    for (markerP = cinfo.marker_list; markerP; markerP = markerP->next) {
        if (markerP->marker == JPEG_COM)
            pm_message("Comment marker (COM):");
        else if (markerP->marker >= JPEG_APP0 &&
                 markerP->marker <= JPEG_APP0+15)
            pm_message("Miscellaneous marker type APP%d:",
                       markerP->marker - JPEG_APP0);
        else
            pm_message("Miscellaneous marker of unknown type (0x%X):",
                       markerP->marker);

        printMarker(*markerP);
    }
}



static void
beginJpegInput(struct jpeg_decompress_struct * const cinfoP,
               bool                            const verbose,
               J_DCT_METHOD                    const dctMethod,
               int                             const maxMemoryToUse,
               bool                            const nosmooth) {
/*----------------------------------------------------------------------------
   Read the JPEG header, create decompressor object (and
   allocate memory for it), set up decompressor.
-----------------------------------------------------------------------------*/
    /* Read file header, set default decompression parameters */
    jpeg_read_header(cinfoP, true);

    cinfoP->dct_method = dctMethod;
    if (maxMemoryToUse != -1)
        cinfoP->mem->max_memory_to_use = maxMemoryToUse;
    if (nosmooth)
        cinfoP->do_fancy_upsampling = false;

}



static void
printComments(struct jpeg_decompress_struct const cinfo) {

    struct jpeg_marker_struct * markerP;

    for (markerP = cinfo.marker_list;
         markerP; markerP = markerP->next) {
        if (markerP->marker == JPEG_COM) {
            pm_message("COMMENT:");
            printMarker(*markerP);
        }
    }
}



static void
printExifInfo(struct jpeg_marker_struct const marker,
              bool                      const wantTagTrace) {
/*----------------------------------------------------------------------------
   Dump as informational messages the contents of the Jpeg miscellaneous
   marker 'marker', assuming it is an Exif header.
-----------------------------------------------------------------------------*/
    exif_ImageInfo imageInfo;
    const char * error;

    assert(marker.data_length >= 6);

    exif_parse(marker.data+6, marker.data_length-6,
               &imageInfo, wantTagTrace, &error);

    if (error) {
        pm_message("EXIF header is invalid.  %s", error);
        pm_strfree(error);
    } else
        exif_showImageInfo(&imageInfo);
}



static bool
isExif(struct jpeg_marker_struct const marker) {
/*----------------------------------------------------------------------------
   Return true iff the JPEG miscellaneous marker 'marker' is an Exif
   header.
-----------------------------------------------------------------------------*/
    bool retval;

    if (marker.marker == JPEG_APP0+1) {
        if (marker.data_length >=6 && memcmp(marker.data, "Exif", 4) == 0)
            retval = true;
        else retval = false;
    }
    else retval = false;

    return retval;
}



static void
dumpExif(struct jpeg_decompress_struct const cinfo,
         bool                          const wantTrace) {
/*----------------------------------------------------------------------------
   Dump as informational messages the contents of all EXIF headers in
   the image, interpreted.  An EXIF header is an APP1 marker.
-----------------------------------------------------------------------------*/
    struct jpeg_marker_struct * markerP;
    bool foundOne;

    for (markerP = cinfo.marker_list, foundOne = false;
         markerP;
         markerP = markerP->next) {
        if (isExif(*markerP)) {
            pm_message("EXIF INFO:");
            printExifInfo(*markerP, wantTrace);
            foundOne = true;
        }
    }
    if (!foundOne)
        pm_message("No EXIF info in image.");
}



static void
saveExif(struct jpeg_decompress_struct const cinfo,
         const char *                  const exifFileName) {
/*----------------------------------------------------------------------------
  Write the contents of the first Exif header in the image into the file with
  name 'exifFileName'.  Start with the two byte length field.  If
  'exifFileName' is "-", write to standard output.

  If there is no Exif header in the image, write just zero, as a two byte pure
  binary integer.
-----------------------------------------------------------------------------*/
    FILE * exifFileP;
    struct jpeg_marker_struct * markerP;

    exifFileP = pm_openw(exifFileName);

    for (markerP = cinfo.marker_list;
         markerP && !isExif(*markerP);
         markerP = markerP->next);

    if (markerP) {
        pm_writebigshort(exifFileP, markerP->data_length+2);
        if (ferror(exifFileP))
            pm_error("Write of Exif header to file '%s' failed on first byte.",
                     exifFileName);
        else {
            int rc;

            rc = fwrite(markerP->data, 1, markerP->data_length, exifFileP);
            if (rc != markerP->data_length)
                pm_error("Write of Exif header to '%s' failed.  Wrote "
                         "length successfully, but then failed after "
                         "%d characters of data.", exifFileName, rc);
        }
    } else {
        /* There is no Exif header in the image */
        pm_writebigshort(exifFileP, 0);
        if (ferror(exifFileP))
            pm_error("Write of Exif header file '%s' failed.", exifFileName);
    }
    pm_close(exifFileP);
}



static void
tellDetails(struct jpeg_decompress_struct const cinfo,
            xelval                        const maxval,
            int                           const outputType) {

    printVerboseInfoAboutHeader(cinfo);

    pm_message("Input image data precision = %d bits",
               cinfo.data_precision);
    pm_message("Output file will have format %c%c "
               "with max sample value of %d.",
               (char) (outputType/256), (char) (outputType % 256),
               maxval);
}



static enum Colorspace
computeColorSpace(struct jpeg_decompress_struct * const cinfoP,
                  enum Inklevel                   const inklevel) {

    enum Colorspace colorSpace;

    if (cinfoP->out_color_space == JCS_GRAYSCALE)
        colorSpace = GRAYSCALE_COLORSPACE;
    else if (cinfoP->out_color_space == JCS_RGB)
        colorSpace = RGB_COLORSPACE;
    else if (cinfoP->out_color_space == JCS_CMYK) {
        switch (inklevel) {
        case ADOBE:
            colorSpace = CMYK_ADOBE_COLORSPACE; break;
        case NORMAL:
            colorSpace = CMYK_NORMAL_COLORSPACE; break;
        case GUESS:
            colorSpace = CMYK_ADOBE_COLORSPACE; break;
        }
    } else
        pm_error("Internal error: unacceptable output color space from "
                 "JPEG decompressor.");

    return colorSpace;
}



static void
convertRaster(struct jpeg_decompress_struct * const cinfoP,
              enum Colorspace                 const colorspace,
              FILE *                          const ofP,
              xelval                          const format,
              unsigned int                    const maxval) {

    JSAMPROW jpegbuffer;  /* Input buffer.  Filled by jpeg_scanlines() */

    jpegbuffer = ((*cinfoP->mem->alloc_sarray)
                  ((j_common_ptr) cinfoP, JPOOL_IMAGE,
                   cinfoP->output_width * cinfoP->output_components,
                   (JDIMENSION) 1)
        )[0];

    while (cinfoP->output_scanline < cinfoP->output_height) {
        jpeg_read_scanlines(cinfoP, &jpegbuffer, 1);
        if (ofP)
            copyPixelRow(jpegbuffer, cinfoP->output_width,
                         cinfoP->out_color_components,
                         colorspace, ofP, format, maxval);
    }
}



static void
convertImage(FILE *                          const ofP,
             struct CmdlineInfo              const cmdline,
             struct jpeg_decompress_struct * const cinfoP) {

    int format;
        /* The type of output file, PGM or PPM.  Value is either PPM_TYPE
           or PGM_TYPE, which conveniently also pass as format values
           PPM_FORMAT and PGM_FORMAT.
        */
    xelval maxval;
        /* The maximum value of a sample (color component), both in the input
           and the output.
        */
    enum Colorspace colorspace;
        /* The color space of the pixels coming out of the JPEG decompressor */

    beginJpegInput(cinfoP, cmdline.verbose,
                   cmdline.dctMethod,
                   cmdline.maxMemoryToUse, cmdline.nosmooth);

    setColorSpaces(cinfoP->jpeg_color_space, &format,
                   &cinfoP->out_color_space);

    maxval = pm_bitstomaxval(cinfoP->data_precision);

    if (cmdline.verbose)
        tellDetails(*cinfoP, maxval, format);

    /* Calculate output image dimensions so we can allocate space */
    jpeg_calc_output_dimensions(cinfoP);

    /* Start decompressor */
    jpeg_start_decompress(cinfoP);

    if (ofP)
        /* Write pnm output header */
        pnm_writepnminit(ofP, cinfoP->output_width, cinfoP->output_height,
                         maxval, format, false);

    pnmbuffer = pnm_allocrow(cinfoP->output_width);

    colorspace = computeColorSpace(cinfoP, cmdline.inklevel);

    convertRaster(cinfoP, colorspace, ofP, format, maxval);

    if (cmdline.comments)
        printComments(*cinfoP);
    if (cmdline.dumpexif)
        dumpExif(*cinfoP, cmdline.traceexif);
    if (cmdline.exifFileName)
        saveExif(*cinfoP, cmdline.exifFileName);

    pnm_freerow(pnmbuffer);

    /* Finish decompression and release decompressor memory. */
    jpeg_finish_decompress(cinfoP);
}




static void
saveMarkers(struct jpeg_decompress_struct * const cinfoP) {

    unsigned int appType;
    /* Get all the miscellaneous markers (COM and APPn) saved for our
       later access.
    */
    jpeg_save_markers(cinfoP, JPEG_COM, 65535);
    for (appType = 0; appType <= 15; ++appType) {
        if (appType == 0 || appType == 14) {
            /* The jpeg library uses APP0 and APP14 internally (see
               libjpeg.doc), so we don't mess with those.
            */
        } else
            jpeg_save_markers(cinfoP, JPEG_APP0 + appType, 65535);
    }
}



static void
convertImages(FILE *                          const ofP,
              struct CmdlineInfo              const cmdline,
              struct jpeg_decompress_struct * const cinfoP,
              struct sourceManager *          const sourceManagerP) {

    if (cmdline.multiple) {
        unsigned int imageSequence;
        for (imageSequence = 0; dsDataLeft(sourceManagerP); ++imageSequence) {
            if (cmdline.verbose)
                pm_message("Reading Image %u", imageSequence);
            convertImage(ofP, cmdline, cinfoP);
        }
    } else {
        if (dsDataLeft(sourceManagerP)) {
            convertImage(ofP, cmdline, cinfoP);
        } else
            pm_error("Input stream is empty");
    }
    if (dsPrematureEof(sourceManagerP)) {
        if (cmdline.repair)
            pm_message("Premature EOF on input; repaired by padding end "
                       "of image.");
        else
            pm_error("Premature EOF on input.  Use -repair to salvage.");
    }
}



int
main(int argc, const char **argv) {

    FILE * ofP;
    struct CmdlineInfo cmdline;
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    struct sourceManager * sourceManagerP;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, (char **)argv, &cmdline);

    if (cmdline.exifFileName && streq(cmdline.exifFileName, "-"))
        /* He's got exif going to stdout, so there can be no image output */
        ofP = NULL;
    else
        ofP = stdout;

    displayComments = cmdline.comments;

    /* Initialize the JPEG decompression object with default error handling. */
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    if (cmdline.traceLevel == 0 && cmdline.verbose)
        cinfo.err->trace_level = 1;
    else
        cinfo.err->trace_level = cmdline.traceLevel;

    saveMarkers(&cinfo);

    sourceManagerP = dsCreateSource(cmdline.inputFileName);

    cinfo.src = dsJpegSourceMgr(sourceManagerP);

    convertImages(ofP, cmdline, &cinfo, sourceManagerP);

    jpeg_destroy_decompress(&cinfo);

    if (ofP) {
        int rc;
        rc = fclose(ofP);
        if (rc == EOF)
            pm_error("Error writing output file.  Errno = %s (%d).",
                     strerror(errno), errno);
    }

    dsDestroySource(sourceManagerP);

    free(cmdline.inputFileName);

    exit(jerr.num_warnings > 0 ? EXIT_WARNING : EXIT_SUCCESS);
}



