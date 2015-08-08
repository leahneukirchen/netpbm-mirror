 /* fitstopnm.c - read a FITS file and produce a PNM.
 **
 ** Copyright (C) 1989 by Jef Poskanzer.
 **
 ** Permission to use, copy, modify, and distribute this software and its
 ** documentation for any purpose and without fee is hereby granted, provided
 ** that the above copyright notice appear in all copies and that both that
 ** copyright notice and this permission notice appear in supporting
 ** documentation.  This software is provided "as is" without express or
 ** implied warranty.
 **
 ** Hacked up version by Daniel Briggs  (dbriggs@nrao.edu)  20-Oct-92
 **
 ** Include floating point formats, more or less.  Will only work on
 ** machines that understand IEEE-754.  Added -scanmax -printmax
 ** -min -max and -noraw.  Ignore axes past 3, instead of error (many packages
 ** use pseudo axes).  Use a finite scale when max=min.  NB: Min and max
 ** are the real world FITS values (scaled), so watch out when bzer & bscale
 ** are not 0 & 1.  Datamin & datamax interpreted correctly in scaled case,
 ** and initialization changed to less likely values.  If datamin & max are
 ** not present in the header, the a first pass is made to determine them
 ** from the array values.
 **
 ** Modified by Alberto Accomazzi (alberto@cfa.harvard.edu), Dec 1, 1992.
 **
 ** Added understanding of 3-plane FITS files, the program is renamed
 ** fitstopnm.  Fixed some non-ansi declarations (DBL_MAX and FLT_MAX
 ** replace MAXDOUBLE and MAXFLOAT), fixed some scaling parameters to
 ** map the full FITS data resolution to the maximum PNM resolution,
 ** disabled min max scanning when reading from stdin.
 */

/*
  The official specification of FITS format (which is for more than
  just visual images) is at
  ftp://legacy.gsfc.nasa.gov/fits_info/fits_office/fits_standard.pdf

  An example FITS file is at

    http://fits.gsfc.nasa.gov/nrao_data/tests/incunabula/mndrll-8.fits

*/

#include <string.h>
#include <float.h>
#include <assert.h>

#include "pm_config.h"
#include "pm_c_util.h"
#include "mallocvar.h"
#include "floatcode.h"
#include "shhopt.h"
#include "pnm.h"



struct CmdlineInfo {
    const char * inputFileName;
    unsigned int image;  /* zero if unspecified */
    float max;
    unsigned int maxSpec;
    float min;
    unsigned int minSpec;
    unsigned int scanmax;
    unsigned int printmax;
    unsigned int noraw;
        /* This is for backward compatibility only.  Use the common option
           -plain now.  (pnm_init() processes -plain).
        */
    unsigned int verbose;
    unsigned int omaxval;
    unsigned int omaxvalSpec;
};



static void 
parseCommandLine(int argc, const char ** argv, 
                 struct CmdlineInfo * const cmdlineP) {
/* --------------------------------------------------------------------------
   Parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.  

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
--------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options. */
    optStruct3 opt;

    unsigned int imageSpec;
    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "image",    OPT_UINT,
            &cmdlineP->image,   &imageSpec,                            0);
    OPTENT3(0, "min",      OPT_FLOAT,
            &cmdlineP->min,     &cmdlineP->minSpec,                    0);
    OPTENT3(0, "max",      OPT_FLOAT,
            &cmdlineP->max,     &cmdlineP->maxSpec,                    0);
    OPTENT3(0, "scanmax",  OPT_FLAG,
            NULL,               &cmdlineP->scanmax,                    0);
    OPTENT3(0, "printmax", OPT_FLAG,
            NULL,               &cmdlineP->printmax,                   0);
    OPTENT3(0, "noraw",    OPT_FLAG,
            NULL,               &cmdlineP->noraw,                      0);
    OPTENT3(0, "verbose",  OPT_FLAG,
            NULL,               &cmdlineP->verbose,                    0);
    OPTENT3(0, "omaxval",  OPT_UINT,
            &cmdlineP->omaxval, &cmdlineP->omaxvalSpec,                0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;   /* We have no parms that are negative numbers */

    /* Set some defaults the lazy way (using multiple setting of variables) */

    pm_optParseOptions3(&argc, (char**)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (imageSpec) {
        if (cmdlineP->image == 0)
            pm_error("You may not specify zero for the image number.  "
                     "Images are numbered starting at 1.");
    } else
        cmdlineP->image = 0;

    if (cmdlineP->maxSpec && cmdlineP->minSpec) {
        if (cmdlineP->max <= cmdlineP->min)
            pm_error("-max must be greater than -min.  You specified "
                     "-max=%f, -min=%f", cmdlineP->max, cmdlineP->min);
    }

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else {
        cmdlineP->inputFileName = argv[1];
        
        if (argc-1 > 1)
            pm_error("Too many arguments (%u).  The only non-option argument "
                     "is the input file name.", argc-1);
    }
    free(option_def);
}



struct FITS_Header {
  int simple;       /* basic format or not */
  int bitpix;
      /* number of bits per pixel, positive for integer, negative 
         for floating point
      */
  int naxis;        /* number of axes */
  int naxis1;       /* number of points on axis 1 */
  int naxis2;       /* number of points on axis 2 */
  int naxis3;       /* number of points on axis 3 */
  double datamin;   /* min # (Physical value!) */
  double datamax;   /* max #     "       "     */
  double bzer;      /* Physical value = Array value*bscale + bzero */
  double bscale;
};


typedef enum {
    VF_CHAR, VF_SHORT, VF_LONG, VF_FLOAT, VF_DOUBLE
} valFmt;

struct fitsRasterInfo {
    valFmt valFmt;
    double bzer;
    double bscale;
};

/* This code deals properly with integers, no matter what the byte order
   or integer size of the host machine.  We handle sign extension manually to
   prevent problems with signed/unsigned characters.  We read floating point
   values properly only when the host architecture conforms to IEEE-754.  If
   you need to tweak this code for other machines, you might want to get a
   copy of the FITS documentation from nssdca.gsfc.nasa.gov
*/

static void
readFitsChar(FILE *   const ifP,
             double * const vP) {

    /* 8 bit FITS integers are unsigned */

    int const ich = getc(ifP);

    if (ich == EOF)
        pm_error("EOF / read error");
    else
        *vP = ich;
}



static void
readFitsShort(FILE *   const ifP,
              double * const vP) {

    int ich;
    int ival;
    unsigned char c[8];

    ich = getc(ifP);

    if (ich == EOF)
        pm_error("EOF / read error");

    c[0] = ich;

    ich = getc(ifP);

    if (ich == EOF)
        pm_error("EOF / read error");

    c[1] = ich;

    if (c[0] & 0x80)
        ival = ~0xFFFF | c[0] << 8 | c[1];
    else
        ival = c[0] << 8 | c[1];

    *vP = ival;
}



static void
readFitsLong(FILE *   const ifP,
             double * const vP) {

    unsigned int i;
    long int lval;
    unsigned char c[4];

    for (i = 0; i < 4; ++i) {
        int const ich = getc(ifP);
        if (ich == EOF)
            pm_error("EOF / read error");
        c[i] = ich;
    }

    if (c[0] & 0x80)
        lval = ~0xFFFFFFFF | c[0] << 24 | c[1] << 16 | c[2] << 8 | c[3];
    else
        lval = c[0] << 24 | c[1] << 16 | c[2] << 8 | c[3] << 0;

    *vP = lval;
}



static void
readFitsFloat(FILE *   const ifP,
              double * const vP) {

    unsigned int i;
    pm_bigendFloat bigend;

    for (i = 0; i < 4; ++i) {
        int const ich = getc(ifP);
        if (ich == EOF)
            pm_error("EOF / read error");
        bigend.bytes[i] = ich;
    }

    *vP = pm_floatFromBigendFloat(bigend);
}



static void
readFitsDouble(FILE *   const ifP,
               double * const vP) {

    unsigned int i;
    pm_bigendDouble bigend;

    for (i = 0; i < 8; ++i) {
        int const ich = getc(ifP);
        if (ich == EOF)
            pm_error("EOF / read error");
        bigend.bytes[i] = ich;
    }

    *vP = pm_doubleFromBigendDouble(bigend);
}



static valFmt
valFmtFromBitpix(int const bitpix) {
/*----------------------------------------------------------------------------
   Return the format of a "value" in the FITS file, given the value
   of the BITPIX header in the FITS file.

   BITPIX has a stupid format wherein it is fundamentally the number
   of bits per value, but its sign indicates whether it is integer
   or floating point.
-----------------------------------------------------------------------------*/
    switch (bitpix) {
    case  +8: return VF_CHAR;
    case +16: return VF_SHORT;
    case +32: return VF_LONG;
    case -32: return VF_FLOAT;
    case -64: return VF_DOUBLE;
    default:
        /* Every possibility is covered above. */
        assert(false);
        return 0;  /* quiet compiler warning */
    }
}



static void
readVal(FILE *   const ifP,
        valFmt   const fmt,
        double * const vP) {

    switch (fmt) {
    case VF_CHAR:
        readFitsChar(ifP, vP);
        break;

    case VF_SHORT:
        readFitsShort(ifP, vP);
        break;
      
    case VF_LONG:
        readFitsLong(ifP, vP);
        break;
      
    case VF_FLOAT:
        readFitsFloat(ifP, vP);
        break;
      
    case VF_DOUBLE:
        readFitsDouble(ifP, vP);
        break;
    }
}



static void
readCard(FILE * const ifP,
         char * const buf) {

    size_t bytesRead;

    bytesRead = fread(buf, 1, 80, ifP);
    if (bytesRead == 0)
        pm_error("error reading header");
}



static void
readFitsHeader(FILE *               const ifP,
               struct FITS_Header * const hP) {

    int seenEnd;
  
    seenEnd = 0;
    /* Set defaults */
    hP->simple  = 0;
    hP->bzer    = 0.0;
    hP->bscale  = 1.0;
    hP->datamin = - DBL_MAX;
    hP->datamax = DBL_MAX;
  
    while (!seenEnd) {
        unsigned int i;
        for (i = 0; i < 36; ++i) {
            char buf[80];
            char c;

            readCard(ifP, buf);
    
            if (sscanf(buf, "SIMPLE = %c", &c) == 1) {
                if (c == 'T' || c == 't')
                    hP->simple = 1;
            } else if (sscanf(buf, "BITPIX = %d", &(hP->bitpix)) == 1);
            else if (sscanf(buf, "NAXIS = %d", &(hP->naxis)) == 1);
            else if (sscanf(buf, "NAXIS1 = %d", &(hP->naxis1)) == 1);
            else if (sscanf(buf, "NAXIS2 = %d", &(hP->naxis2)) == 1);
            else if (sscanf(buf, "NAXIS3 = %d", &(hP->naxis3)) == 1);
            else if (sscanf(buf, "DATAMIN = %lf", &(hP->datamin)) == 1);
            else if (sscanf(buf, "DATAMAX = %lf", &(hP->datamax)) == 1);
            else if (sscanf(buf, "BZERO = %lf", &(hP->bzer)) == 1);
            else if (sscanf(buf, "BSCALE = %lf", &(hP->bscale)) == 1);
            else if (strncmp(buf, "END ", 4 ) == 0) seenEnd = 1;
        }
    }
}



static void
interpretPlanes(struct FITS_Header const fitsHeader,
                unsigned int       const imageRequest,
                bool               const verbose,
                unsigned int *     const imageCountP,
                bool *             const multiplaneP,
                unsigned int *     const desiredImageP) {

    if (fitsHeader.naxis == 2) {
        *imageCountP   = 1;
        *multiplaneP   = FALSE;
        *desiredImageP = 1;
    } else {
        if (imageRequest) {
            if (imageRequest > fitsHeader.naxis3)
                pm_error("Only %u plane%s in this file.  "
                         "You requested image %u", 
                         fitsHeader.naxis3, fitsHeader.naxis3 > 1 ? "s" : "",
                         imageRequest);
            else {
                *imageCountP   = fitsHeader.naxis3;
                *multiplaneP   = FALSE;
                *desiredImageP = imageRequest;
            }
        } else {
            if (fitsHeader.naxis3 == 3) {
                *imageCountP   = 1;
                *multiplaneP   = TRUE;
                *desiredImageP = 1;
            } else if (fitsHeader.naxis3 > 1)
                pm_error("This FITS file contains multiple (%u) images.  "
                         "You must specify which one you want with a "
                         "-image option.", fitsHeader.naxis3);
            else {
                *imageCountP   = fitsHeader.naxis3;
                *multiplaneP   = FALSE;
                *desiredImageP = 1;
            }
        }
    }
    if (verbose) {
        
        pm_message("FITS stream is %smultiplane", *multiplaneP ? "" : "not ");
        pm_message("We will take image %u (1 is first) of %u "
                   "in the FITS stream",
                   *desiredImageP, *imageCountP);
    }
}



static void
scanImageForMinMax(FILE *       const ifP,
                   unsigned int const images,
                   int          const cols,
                   int          const rows,
                   valFmt       const valFmt,
                   double       const bscale,
                   double       const bzer,
                   unsigned int const imagenum,
                   bool         const multiplane,
                   double *     const dataminP,
                   double *     const datamaxP) {

    /* Note that a value in the file might be Not-A-Number.  We ignore
       such entries in computing the minimum and maximum for the image.
    */
    double dmax, dmin;
    unsigned int image;
    pm_filepos rasterPos;
    double fmaxval;
    
    pm_tell2(ifP, &rasterPos, sizeof(rasterPos));

    pm_message("Scanning file for scaling parameters");

    switch (valFmt) {
    case VF_CHAR:   fmaxval = 255.0;        break;
    case VF_SHORT:  fmaxval = 65535.0;      break;
    case VF_LONG:   fmaxval = 4294967295.0; break;
    case VF_FLOAT:  fmaxval = FLT_MAX;      break;
    case VF_DOUBLE: fmaxval = DBL_MAX;      break;
    }

    dmax = -fmaxval;
    dmin = fmaxval;
    for (image = 1; image <= images; ++image) {
        unsigned int row;
        for (row = 0; row < rows; ++row) {
            unsigned int col;
            for (col = 0; col < cols; ++col) {
                double val;
                readVal(ifP, valFmt, &val);
                if (image == imagenum || multiplane ) {
                    /* Note: if 'val' is NaN, result is 2nd operand */
                    dmax = MAX(val, dmax);
                    dmin = MIN(val, dmin);
                }
            }
        }
        if (bscale < 0.0) {
            double const origDmax = dmax;
            dmax = dmin;
            dmin = origDmax;
        }
    }
    *dataminP = dmin * bscale + bzer;
    *datamaxP = dmax * bscale + bzer;

    pm_message("Scan results: min=%f max=%f", *dataminP, *datamaxP);

    pm_seek2(ifP, &rasterPos, sizeof(rasterPos));
}



static void
computeMinMax(FILE *             const ifP,
              unsigned int       const images,
              int                const cols,
              int                const rows,
              struct FITS_Header const h,
              unsigned int       const imagenum,
              bool               const multiplane,
              bool               const forcemin,
              bool               const forcemax,
              double             const frmin,
              double             const frmax,
              double *           const dataminP,
              double *           const datamaxP) {

    double datamin, datamax;

    datamin = -DBL_MAX;  /* initial assumption */
    datamax = DBL_MAX;   /* initial assumption */

    if (forcemin)
        datamin = frmin;
    if (forcemax)
        datamax = frmax;

    if (datamin == -DBL_MAX)
        datamin = h.datamin;
    if (datamax == DBL_MAX)
        datamax = h.datamax;

    if (datamin == -DBL_MAX || datamax == DBL_MAX) {
        double scannedDatamin, scannedDatamax;
        scanImageForMinMax(ifP, images, cols, rows,
                           valFmtFromBitpix(h.bitpix), h.bscale, h.bzer,
                           imagenum, multiplane,
                           &scannedDatamin, &scannedDatamax);

        if (datamin == -DBL_MAX)
            datamin = scannedDatamin;
        if (datamax == DBL_MAX)
            datamax = scannedDatamax;
    }
    *dataminP = datamin;
    *datamaxP = datamax;
}



static xelval
determineMaxval(struct CmdlineInfo const cmdline,
                valFmt             const valFmt,
                double             const datamax,
                double             const datamin) {

    xelval retval;
                
    if (cmdline.omaxvalSpec)
        retval = cmdline.omaxval;
    else {
        if (valFmt == VF_FLOAT || valFmt == VF_DOUBLE) {
            /* samples are floating point, which means the resolution
               could be anything.  So we just pick a convenient maxval
               of 255.  Before Netpbm 10.20 (January 2004), we did
               maxval = max - min for floating point as well as
               integer samples.
            */
            retval = 255;
            if (cmdline.verbose)
                pm_message("FITS image has floating point samples.  "
                           "Using maxval = %u.", (unsigned int)retval);
        } else {
            retval = MAX(1, MIN(PNM_OVERALLMAXVAL, datamax - datamin));
            if (cmdline.verbose)
                pm_message("FITS image has samples in the range %d-%d.  "
                           "Using maxval %u.",
                           (int)(datamin+0.5), (int)(datamax+0.5),
                           (unsigned int)retval);
        }
    }
    return retval;
}



static void
convertPgmRaster(FILE *                const ifP,
                 unsigned int          const cols,
                 unsigned int          const rows,
                 xelval                const maxval,
                 unsigned int          const desiredImage,
                 unsigned int          const imageCount,
                 struct fitsRasterInfo const rasterInfo,
                 double                const scale,
                 double                const datamin,
                 xel **                const xels) {
        
    /* Note: the FITS specification does not give the association between
       file position and image position (i.e. is the first pixel in the
       file the top left, bottom left, etc.).  We use the common sense,
       popular order of row major, top to bottom, left to right.  This
       has been the case and accepted since 1989, but in 2008, we discovered
       that Gimp and ImageMagick do bottom to top.
    */
    unsigned int image;

    pm_message("writing PGM file");

    for (image = 1; image <= desiredImage; ++image) {
        unsigned int row;
        if (image != desiredImage)
            pm_message("skipping image plane %u of %u", image, imageCount);
        else if (imageCount > 1)
            pm_message("reading image plane %u of %u", image, imageCount);
        for (row = 0; row < rows; ++row) {
            unsigned int col;
            for (col = 0; col < cols; ++col) {
                double val;
                readVal(ifP, rasterInfo.valFmt, &val);
                {
                    double const t = scale *
                        (val * rasterInfo.bscale + rasterInfo.bzer - datamin);
                    xelval const tx = MAX(0, MIN(t, maxval));
                    if (image == desiredImage)
                        PNM_ASSIGN1(xels[row][col], tx);
                }
            }
        }
    } 
}



static void
convertPpmRaster(FILE *                const ifP,
                 unsigned int          const cols,
                 unsigned int          const rows,
                 xelval                const maxval,
                 struct fitsRasterInfo const rasterInfo,
                 double                const scale,
                 double                const datamin,
                 xel **                const xels) {
/*----------------------------------------------------------------------------
   Read the FITS raster from file *ifP into xels[][].  Image dimensions
   are 'cols' by 'rows'.  The FITS raster is 3 planes composing one
   image: a red plane followed by a green plane followed by a blue plane.
-----------------------------------------------------------------------------*/
    unsigned int plane;

    pm_message("Writing PPM file "
               "(Probably not what you want - consider an -image option)");

    for (plane = 0; plane < 3; ++plane) {
        unsigned int row;
        pm_message("reading image plane %u (%s)",
                   plane, plane == 0 ? "red" : plane == 1 ? "green" : "blue");
        for (row = 0; row < rows; ++row) {
            unsigned int col;
            for (col = 0; col < cols; ++col) {
                double val;
                readVal(ifP, rasterInfo.valFmt, &val);
                {
                    double const t = scale *
                        (val * rasterInfo.bscale + rasterInfo.bzer - datamin);
                    xelval const sample = MAX(0, MIN(t, maxval));

                    switch (plane) {
                    case 0: PPM_PUTR(xels[row][col], sample); break;
                    case 1: PPM_PUTG(xels[row][col], sample); break;
                    case 2: PPM_PUTB(xels[row][col], sample); break;
                    }
                }
            }
        }
    }
}



static void
convertRaster(FILE *                const ifP,
              unsigned int          const cols,
              unsigned int          const rows,
              xelval                const maxval,
              bool                  const forceplain,
              bool                  const multiplane,
              unsigned int          const desiredImage,
              unsigned int          const imageCount,
              struct fitsRasterInfo const rasterInfo,
              double                const scale,
              double                const datamin) {

    xel ** xels;
    int format;

    xels = pnm_allocarray(cols, rows);

    if (multiplane) {
        format = PPM_FORMAT;
        convertPpmRaster(ifP, cols, rows, maxval, rasterInfo, scale, datamin,
                         xels);
    } else {
        format = PGM_FORMAT;
        convertPgmRaster(ifP, cols, rows, maxval,
                         desiredImage, imageCount, rasterInfo, scale, datamin,
                         xels);
    }
    pnm_writepnm(stdout, xels, cols, rows, maxval, format, forceplain);
    pnm_freearray(xels, rows);
}



int
main(int argc, const char * argv[]) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    unsigned int cols, rows;
    xelval maxval;
    double scale;
    double datamin, datamax;
    struct FITS_Header fitsHeader;
    struct fitsRasterInfo rasterInfo;

    unsigned int imageCount;
    unsigned int desiredImage;
        /* Plane number (starting at one) of plane that contains the image
           we want.
        */
    bool multiplane;
        /* This is a one-image multiplane stream; 'desiredImage'
           is undefined
        */
  
    pm_proginit(&argc, argv);
  
    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    readFitsHeader(ifP, &fitsHeader);
  
    if (!fitsHeader.simple)
        pm_error("FITS file is not in simple format, can't read");

    if (fitsHeader.naxis != 2 && fitsHeader.naxis != 3)
        pm_message("Warning: FITS file has %u axes", fitsHeader.naxis);

    cols = fitsHeader.naxis1;
    rows = fitsHeader.naxis2;

    rasterInfo.bscale = fitsHeader.bscale;
    rasterInfo.bzer   = fitsHeader.bzer;
    rasterInfo.valFmt = valFmtFromBitpix(fitsHeader.bitpix);

    interpretPlanes(fitsHeader, cmdline.image, cmdline.verbose,
                    &imageCount, &multiplane, &desiredImage);

    computeMinMax(ifP, imageCount, cols, rows, fitsHeader,
                  desiredImage, multiplane,
                  cmdline.minSpec, cmdline.maxSpec,
                  cmdline.min, cmdline.max,
                  &datamin, &datamax);

    maxval = determineMaxval(cmdline, rasterInfo.valFmt, datamax, datamin);

    if (datamax - datamin == 0)
        scale = 1.0;
    else
        scale = maxval / (datamax - datamin);

    if (cmdline.printmax)
        printf("%f %f\n", datamin, datamax);
    else
        convertRaster(ifP, cols, rows, maxval, cmdline.noraw,
                      multiplane, desiredImage, imageCount,
                      rasterInfo, scale, datamin);

    pm_close(ifP);
    pm_close(stdout);

    return 0;
}



