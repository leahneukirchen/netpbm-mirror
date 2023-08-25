/*****************************************************************************
                                pnmtojpeg
******************************************************************************
  This program is part of the Netpbm package.

  This program converts from the PNM formats to the JFIF format
  which is based on JPEG.

  This program is by Bryan Henderson on 2000.03.06, but is derived
  with permission from the program cjpeg, which is in the Independent
  Jpeg Group's JPEG library package.  Under the terms of that permission,
  redistribution of this software is restricted as described in the
  file README.JPEG.

  Copyright (C) 1991-1998, Thomas G. Lane.

*****************************************************************************/

#define _DEFAULT_SOURCE 1  /* New name for SVID & BSD source defines */
#define _BSD_SOURCE 1      /* Make sure strdup() is in string.h */
#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */

#include <ctype.h>		/* to declare isdigit(), etc. */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
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

#define EXIT_WARNING 2   /* Goes with EXIT_SUCCESS, EXIT_FAILURE in stdlib.h */

enum RestartUnit {RESTART_MCU, RESTART_ROW, RESTART_NONE};
enum DensityUnit {DEN_UNSPECIFIED, DEN_DOTS_PER_INCH, DEN_DOTS_PER_CM};

struct Density {
    enum DensityUnit unit;
        /* The units of density for 'horiz', 'vert' */
    unsigned short horiz;  /* Not 0 */
        /* Horizontal density, in units specified by 'unit' */
    unsigned short vert;   /* Not 0 */
        /* Same as 'horiz', but vertical */
};

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
     */
    char *           inputFileNm;
    unsigned int     verbose;
    unsigned int     quality;
    unsigned int     baseline;
    unsigned int     progressive;
    unsigned int     arithmetic;
    J_DCT_METHOD     dctMethod;
    unsigned int     grayscale;
    unsigned int     rgb;
    long int         maxMemoryToUse;
    unsigned int     tracelevel;
    char *           qslots;
    char *           qtablefile;
    char *           sample;
    char *           scans;
    int              smooth;
    unsigned int     optimize;
    unsigned int     restartValue;
    enum RestartUnit restartUnit;
    char *           restart;
    char *           comment;            /* NULL if none */
    const char *     exif;               /* NULL if none */
    unsigned int     densitySpec;
        /* boolean: JFIF should specify a density.  If false, 'density'
           is undefined.
        */
    struct Density density;
};

static void
interpretMaxmemory (const char * const maxmemory,
                    long int *   const maxMemoryToUseP) {
    long int lval;
    char ch;

    if (maxmemory == NULL) {
        *maxMemoryToUseP = -1;  /* unspecified */
    } else if (sscanf(maxmemory, "%ld%c", &lval, &ch) < 1) {
        pm_error("Invalid value for --maxmemory option: '%s'.", maxmemory);
        exit(EXIT_FAILURE);
    } else {
        if (ch == 'm' || ch == 'M') lval *= 1000L;
        *maxMemoryToUseP = lval * 1000L;
    }
}



static void
interpretRestart(const char *       const restartOpt,
                 unsigned int *     const restartValueP,
                 enum RestartUnit * const restartUnitP) {
/*----------------------------------------------------------------------------
   Interpret the restart command line option.  Return values suitable
   for plugging into a jpeg_compress_struct to control compression.
-----------------------------------------------------------------------------*/
    if (restartOpt == NULL) {
        /* No --restart option.  Set default */
        *restartUnitP = RESTART_NONE;
    } else {
        /* Restart interval in MCU rows (or in MCUs with 'b'). */
        long lval;
        char ch;
        unsigned int matches;

        matches= sscanf(restartOpt, "%ld%c", &lval, &ch);
        if (matches == 0)
            pm_error("Invalid value for the --restart option : '%s'.",
                     restartOpt);
        else {
            if (lval < 0 || lval > 65535L) {
                pm_error("--restart value %ld is out of range.", lval);
                exit(EXIT_FAILURE);
            } else {
                if (matches == 1) {
                    *restartValueP = lval;
                    *restartUnitP  = RESTART_ROW;
                } else {
                    if (ch == 'b' || ch == 'B') {
                        *restartValueP = lval;
                        *restartUnitP  = RESTART_MCU;
                    } else
                        pm_error("Invalid --restart value '%s'.", restartOpt);
                }
            }
        }
    }
}




static void
interpretDensity(const char *     const densityString,
                 struct Density * const densityP) {
/*----------------------------------------------------------------------------
   Interpret the value of the "-density" option.
-----------------------------------------------------------------------------*/
    if (strlen(densityString) < 1)
        pm_error("-density value cannot be null.");
    else {
        char * unitName;  /* malloc'ed */
        int matched;
        int horiz, vert;

        unitName = malloc(strlen(densityString)+1);

        matched = sscanf(densityString, "%dx%d%s", &horiz, &vert, unitName);

        if (matched < 2)
            pm_error("Invalid format for density option value '%s'.  It "
                     "should follow the example '3x2' or '3x2dpi' or "
                     "'3x2dpcm'.", densityString);
        else {
            if (horiz <= 0 || horiz >= 1<<16)
                pm_error("Horizontal density %d is outside the range 1-65535",
                         horiz);
            else if (vert <= 0 || vert >= 1<<16)
                pm_error("Vertical density %d is outside the range 1-65535",
                         vert);
            else {
                densityP->horiz = horiz;
                densityP->vert  = vert;

                if (matched < 3)
                    densityP->unit = DEN_UNSPECIFIED;
                else {
                    if (streq(unitName, "dpi") || streq(unitName, "DPI"))
                        densityP->unit = DEN_DOTS_PER_INCH;
                    else if (streq(unitName, "dpcm") ||
                             streq(unitName, "DPCM"))
                        densityP->unit = DEN_DOTS_PER_CM;
                    else
                        pm_error("Unrecognized unit '%s' in the density value "
                                 "'%s'.  I recognize only 'dpi' and 'dpcm'",
                                 unitName, densityString);
                }
            }
        }
        free(unitName);
    }
}



static void
parseCommandLine(const int argc, const char ** argv,
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
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    int i;  /* local loop variable */

    const char * dctval;
    const char * maxmemory;
    const char * restart;
    const char * density;

    unsigned int qualitySpec, smoothSpec;

    unsigned int option_def_index;

    int argcParse;       /* argc, except we modify it as we parse */
    const char ** argvParse;
        /* argv, except we modify it as we parse */

    MALLOCARRAY_NOFAIL(option_def, 100);

    MALLOCARRAY(argvParse, argc + 1);  /* +1 for the terminating null ptr */

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "verbose",     OPT_FLAG,   NULL, &cmdlineP->verbose,        0);
    OPTENT3(0, "quality",     OPT_UINT,   &cmdlineP->quality,
            &qualitySpec,        0);
    OPTENT3(0, "baseline",    OPT_FLAG,   NULL, &cmdlineP->baseline,       0);
    OPTENT3(0, "progressive", OPT_FLAG,   NULL, &cmdlineP->progressive,    0);
    OPTENT3(0, "arithmetic",  OPT_FLAG,   NULL, &cmdlineP->arithmetic,     0);
    OPTENT3(0, "dct",         OPT_STRING, &dctval, NULL,                   0);
    OPTENT3(0, "grayscale",   OPT_FLAG,   NULL, &cmdlineP->grayscale,      0);
    OPTENT3(0, "greyscale",   OPT_FLAG,   NULL, &cmdlineP->grayscale,      0);
    OPTENT3(0, "rgb",         OPT_FLAG,   NULL, &cmdlineP->rgb,            0);
    OPTENT3(0, "maxmemory",   OPT_STRING, &maxmemory, NULL,                0);
    OPTENT3(0, "tracelevel",  OPT_UINT,   &cmdlineP->tracelevel, NULL,    0);
    OPTENT3(0, "qslots",      OPT_STRING, &cmdlineP->qslots,      NULL,    0);
    OPTENT3(0, "qtables",     OPT_STRING, &cmdlineP->qtablefile,  NULL,    0);
    OPTENT3(0, "sample",      OPT_STRING, &cmdlineP->sample,      NULL,    0);
    OPTENT3(0, "scans",       OPT_STRING, &cmdlineP->scans,       NULL,    0);
    OPTENT3(0, "smooth",      OPT_UINT,   &cmdlineP->smooth,
            &smoothSpec,  0);
    OPTENT3(0, "optimize",    OPT_FLAG,   NULL, &cmdlineP->optimize,       0);
    OPTENT3(0, "optimise",    OPT_FLAG,   NULL, &cmdlineP->optimize,       0);
    OPTENT3(0, "restart",     OPT_STRING, &restart, NULL,                   0);
    OPTENT3(0, "comment",     OPT_STRING, &cmdlineP->comment, NULL,        0);
    OPTENT3(0, "exif",        OPT_STRING, &cmdlineP->exif, NULL,  0);
    OPTENT3(0, "density",     OPT_STRING, &density,
            &cmdlineP->densitySpec, 0);


    /* Set the defaults */
    dctval = NULL;
    maxmemory = NULL;
    cmdlineP->tracelevel = 0;
    cmdlineP->qslots = NULL;
    cmdlineP->qtablefile = NULL;
    cmdlineP->sample = NULL;
    cmdlineP->scans = NULL;
    restart = NULL;
    cmdlineP->comment = NULL;
    cmdlineP->exif = NULL;

    /* Make private copy of arguments for pm_optParseOptions to corrupt */
    argcParse = argc;
    for (i = 0; i < argc+1; ++i)
        argvParse[i] = argv[i];

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argcParse, (char **)argvParse, opt, sizeof(opt), 0);

    if (!qualitySpec)
        cmdlineP->quality = -1;  /* unspecified */

    if (!smoothSpec)
        cmdlineP->smooth = -1;

    if (cmdlineP->rgb && cmdlineP->grayscale)
        pm_error("You can't specify both -rgb and -grayscale");

    if (argcParse - 1 == 0)
        cmdlineP->inputFileNm = strdup("-");  /* he wants stdin */
    else if (argcParse - 1 == 1)
        cmdlineP->inputFileNm = strdup(argvParse[1]);
    else
        pm_error("Too many arguments.  The only argument accepted "
                 "is the input file specification.");
    if (dctval == NULL)
        cmdlineP->dctMethod = JDCT_DEFAULT;
    else {
        if (streq(dctval, "int"))
            cmdlineP->dctMethod = JDCT_ISLOW;
        else if (streq(dctval, "fast"))
            cmdlineP->dctMethod = JDCT_IFAST;
        else if (streq(dctval, "float"))
            cmdlineP->dctMethod = JDCT_FLOAT;
        else
            pm_error("Invalid value for the --dct option: '%s'.", dctval);
    }

    interpretMaxmemory(maxmemory, &cmdlineP->maxMemoryToUse);
    interpretRestart(restart, &cmdlineP->restartValue,
                      &cmdlineP->restartUnit);
    if (cmdlineP->densitySpec)
        interpretDensity(density, &cmdlineP->density);

    if (cmdlineP->smooth > 100)
        pm_error("Smoothing factor %d is greater than 100 (%%).",
                 cmdlineP->smooth);

    if (streq(cmdlineP->inputFileNm, "=") &&
        cmdlineP->exif && streq(cmdlineP->exif, "-"))

        pm_error("Cannot have both input image and exif header be from "
                 "Standard Input.");


    free(argvParse);
}



static void
reportCompressor(const struct jpeg_compress_struct cinfo) {

    if (cinfo.scan_info == NULL)
        pm_message("No scan script is being used");
    else {
        unsigned int i;
        pm_message("A scan script with %d entries is being used:",
                   cinfo.num_scans);
        for (i = 0; i < cinfo.num_scans; ++i) {
            unsigned int j;
            pm_message("    Scan %2d: Ss=%2d Se=%2d Ah=%2d Al=%2d  "
                       "%d components",
                       i,
                       cinfo.scan_info[i].Ss,
                       cinfo.scan_info[i].Se,
                       cinfo.scan_info[i].Ah,
                       cinfo.scan_info[i].Al,
                       cinfo.scan_info[i].comps_in_scan
                       );
            for (j = 0; j < cinfo.scan_info[i].comps_in_scan; ++j)
                pm_message("        Color component %d index: %d", j,
                           cinfo.scan_info[i].component_index[j]);
        }
    }
}



static void
setupJpegSourceParameters(struct jpeg_compress_struct * const cinfoP,
                          unsigned int                  const width,
                          unsigned int                  const height,
                          int                           const format) {
/*----------------------------------------------------------------------------
   Set up in the compressor descriptor *cinfoP the description of the
   source image as required by the compressor.
-----------------------------------------------------------------------------*/
    switch PNM_FORMAT_TYPE(format) {
    case PBM_TYPE:
    case PGM_TYPE:
        cinfoP->in_color_space = JCS_GRAYSCALE;
        cinfoP->input_components = 1;
        break;
    case PPM_TYPE:
        cinfoP->in_color_space = JCS_RGB;
        cinfoP->input_components = 3;
        break;
    default:
        pm_error("INTERNAL ERROR; invalid format in "
                 "setup_jpeg_source_parameters()");
    }
}



static void
setupJpegDensity(struct jpeg_compress_struct * const cinfoP,
                 struct Density                const density) {
/*----------------------------------------------------------------------------
   Set up in the compressor descriptor *cinfoP the density information
   'density'.
-----------------------------------------------------------------------------*/
    switch(density.unit) {
    case DEN_UNSPECIFIED:   cinfoP->density_unit = 0; break;
    case DEN_DOTS_PER_INCH: cinfoP->density_unit = 1; break;
    case DEN_DOTS_PER_CM:   cinfoP->density_unit = 2; break;
    }

    cinfoP->X_density = density.horiz;
    cinfoP->Y_density = density.vert;
}



/*----------------------------------------------------------------------------
   The functions below here are essentially the file rdswitch.c from
   the JPEG library.  They perform the functions specified by the following
   pnmtojpeg options:

   -qtables file          Read quantization tables from text file
   -scans file            Read scan script from text file
   -qslots N[,N,...]      Set component quantization table selectors
   -sample HxV[,HxV,...]  Set component sampling factors
-----------------------------------------------------------------------------*/

static int
textGetc (FILE * fileP) {
/*----------------------------------------------------------------------------
  Read next char, skipping over any comments (# to end of line).

  Return a comment/newline sequence as a newline.
-----------------------------------------------------------------------------*/
    int ch;

    ch = getc(fileP);
    if (ch == '#') {
        do {
            ch = getc(fileP);
        } while (ch != '\n' && ch != EOF);
    }
    return ch;
}



static boolean
readTextInteger(FILE * const fileP,
                long * const resultP,
                int *  const termcharP) {
/*----------------------------------------------------------------------------
   Read the next unsigned decimal integer from file 'fileP', skipping
   white space as necessary.  Return it as *resultP.

   Also read one character after the integer and return it as *termcharP.

   If there is no character after the integer, return *termcharP == EOF.

   Iff the next thing in the file is not a valid unsigned decimal integer,
   return FALSE.
-----------------------------------------------------------------------------*/
    int ch;
    boolean retval;

    /* Skip any leading whitespace, detect EOF */
    do {
        ch = textGetc(fileP);
    } while (isspace(ch));

    if (!isdigit(ch))
        retval = FALSE;
    else {
        long val;
        val = ch - '0';  /* initial value */
        while ((ch = textGetc(fileP)) != EOF) {
            if (! isdigit(ch))
                break;
            val *= 10;
            val += ch - '0';
        }
        *resultP = val;
        retval = TRUE;
    }
    *termcharP = ch;
    return retval;
}



static bool
readScanInteger (FILE * const fileP,
                 long * const resultP,
                 int *  const termcharP) {
/*----------------------------------------------------------------------------
  Variant of readTextInteger that always looks for a non-space termchar;
  this simplifies parsing of punctuation in scan scripts.
-----------------------------------------------------------------------------*/
    int ch;

    if (! readTextInteger(fileP, resultP, termcharP))
        return false;

    ch = *termcharP;  /* initial value */

    while (ch != EOF && isspace(ch))
        ch = textGetc(fileP);

    if (isdigit(ch)) {		/* oops, put it back */
        if (ungetc(ch, fileP) == EOF)
            return false;
        ch = ' ';
    } else {
        /* Any separators other than ';' and ':' are ignored;
         * this allows user to insert commas, etc, if desired.
         */
        if (ch != EOF && ch != ';' && ch != ':')
            ch = ' ';
    }
    *termcharP = ch;
    return true;
}



static bool
readScanScript(j_compress_ptr const cinfo,
               const char *   const fileNm) {
/*----------------------------------------------------------------------------
  Read a scan script from the specified text file.
  Each entry in the file defines one scan to be emitted.
  Entries are separated by semicolons ';'.
  An entry contains one to four component indexes,
  optionally followed by a colon ':' and four progressive-JPEG parameters.
  The component indexes denote which component(s) are to be transmitted
  in the current scan.  The first component has index 0.
  Sequential JPEG is used if the progressive-JPEG parameters are omitted.
  The file is free format text: any whitespace may appear between numbers
  and the ':' and ';' punctuation marks.  Also, other punctuation (such
  as commas or dashes) can be placed between numbers if desired.
  Comments preceded by '#' may be included in the file.
  Note: we do very little validity checking here;
  jcmaster.c will validate the script parameters.
-----------------------------------------------------------------------------*/
    FILE * fp;
    unsigned int nscans;
    unsigned int ncomps;
    int termchar;
    long val;
#define MAX_SCANS  100      /* quite arbitrary limit */
    jpeg_scan_info scans[MAX_SCANS];

    fp = fopen(fileNm, "r");
    if (fp == NULL) {
        pm_message("Can't open scan definition file '%s'", fileNm);
        return false;
    }
    nscans = 0;

    while (readScanInteger(fp, &val, &termchar)) {
        ++nscans;  /* We got another scan */
        if (nscans > MAX_SCANS) {
            pm_message("Too many scans defined in file '%s'", fileNm);
            fclose(fp);
            return false;
        }
        scans[nscans-1].component_index[0] = (int) val;
        ncomps = 1;
        while (termchar == ' ') {
            if (ncomps >= MAX_COMPS_IN_SCAN) {
                pm_message("Too many components in one scan in file '%s'",
                           fileNm);
                fclose(fp);
                return FALSE;
            }
            if (! readScanInteger(fp, &val, &termchar))
                goto bogus;
            scans[nscans-1].component_index[ncomps] = (int) val;
            ++ncomps;
        }
        scans[nscans-1].comps_in_scan = ncomps;
        if (termchar == ':') {
            if (! readScanInteger(fp, &val, &termchar) || termchar != ' ')
                goto bogus;
            scans[nscans-1].Ss = (int) val;
            if (! readScanInteger(fp, &val, &termchar) || termchar != ' ')
                goto bogus;
            scans[nscans-1].Se = (int) val;
            if (! readScanInteger(fp, &val, &termchar) || termchar != ' ')
                goto bogus;
            scans[nscans-1].Ah = (int) val;
            if (! readScanInteger(fp, &val, &termchar))
                goto bogus;
            scans[nscans-1].Al = (int) val;
        } else {
            /* set non-progressive parameters */
            scans[nscans-1].Ss = 0;
            scans[nscans-1].Se = DCTSIZE2-1;
            scans[nscans-1].Ah = 0;
            scans[nscans-1].Al = 0;
        }
        if (termchar != ';' && termchar != EOF) {
        bogus:
            pm_message("Invalid scan entry format in file '%s'", fileNm);
            fclose(fp);
            return false;
        }
    }

    if (termchar != EOF) {
        pm_message("Non-numeric data in file '%s'", fileNm);
        fclose(fp);
        return false;
    }

    if (nscans > 0) {
        /* Stash completed scan list in cinfo structure.  NOTE: in
           this program, JPOOL_IMAGE is the right lifetime for this
           data, but if you want to compress multiple images you'd
           want JPOOL_PERMANENT.
        */
        unsigned int const scanInfoSz = nscans * sizeof(jpeg_scan_info);
        jpeg_scan_info * const scanInfo =
            (jpeg_scan_info *)
            (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_IMAGE,
                                        scanInfoSz);
        memcpy(scanInfo, scans, scanInfoSz);
        cinfo->scan_info = scanInfo;
        cinfo->num_scans = nscans;
    }

    fclose(fp);
    return true;
}



static bool
readQuantTables(j_compress_ptr const cinfo,
                const char *   const fileNm,
                int            const scaleFactor,
                bool           const forceBaseline) {
/*----------------------------------------------------------------------------
  Read a set of quantization tables from the specified file.
  The file is plain ASCII text: decimal numbers with whitespace between.
  Comments preceded by '#' may be included in the file.
  There may be one to NUM_QUANT_TBLS tables in the file, each of 64 values.
  The tables are implicitly numbered 0,1,etc.
  NOTE: does not affect the qslots mapping, which will default to selecting
  table 0 for luminance (or primary) components, 1 for chrominance components.
  You must use -qslots if you want a different component->table mapping.
-----------------------------------------------------------------------------*/
    FILE * fp;
    bool retval;

    fp = fopen(fileNm, "rb");
    if (fp == NULL) {
        pm_message("Can't open table file '%s'", fileNm);
        retval = false;
    } else {
        boolean eof, error;
        unsigned int tblno;

        for (tblno = 0, eof = false, error = false; !eof && !error; ++tblno) {
            long val;
            int termchar;
            boolean gotOne;

            gotOne = readTextInteger(fp, &val, &termchar);
            if (gotOne) {
                /* read 1st element of table */
                if (tblno >= NUM_QUANT_TBLS) {
                    pm_message("Too many tables in file '%s'", fileNm);
                    error = true;
                } else {
                    unsigned int table[DCTSIZE2];
                    unsigned int i;

                    table[0] = (unsigned int) val;
                    for (i = 1; i < DCTSIZE2 && !error; ++i) {
                        if (! readTextInteger(fp, &val, &termchar)) {
                            pm_message("Invalid table data in file '%s'",
                                       fileNm);
                            error = true;
                        } else
                            table[i] = (unsigned int) val;
                    }
                    if (!error)
                        jpeg_add_quant_table(
                            cinfo, tblno, table, scaleFactor, forceBaseline);
                }
            } else {
                if (termchar == EOF)
                    eof = TRUE;
                else {
                    pm_message("Non-numeric data in file '%s'", fileNm);
                    error = TRUE;
                }
            }
        }

        fclose(fp);
        retval = !error;
    }

    return retval;
}



static bool
setQuantSlots(j_compress_ptr const cinfo,
              const char *   const arg) {
/*----------------------------------------------------------------------------
  Process a quantization-table-selectors parameter string, of the form
  N[,N,...]

  If there are more components than parameters, the last value is replicated.
-----------------------------------------------------------------------------*/
    int val;
    int ci;
    char ch;
    unsigned int i;

    val = 0; /* initial value - default table */

    for (ci = 0, i = 0; ci < MAX_COMPONENTS; ++ci) {
        if (arg[i]) {
            ch = ',';			/* if not set by sscanf, will be ',' */
            if (sscanf(&arg[i], "%d%c", &val, &ch) < 1)
                return false;
            if (ch != ',')		/* syntax check */
                return false;
            if (val < 0 || val >= NUM_QUANT_TBLS) {
                pm_message("Invalid quantization table number: %d.  "
                           "JPEG quantization tables are numbered 0..%d",
                           val, NUM_QUANT_TBLS - 1);
                return false;
            }
            cinfo->comp_info[ci].quant_tbl_no = val;
            while (arg[i] && arg[i] != ',') {
                ++i;
                /* advance to next segment of arg string */
            }
        } else {
            /* reached end of parameter, set remaining components to last tbl*/
            cinfo->comp_info[ci].quant_tbl_no = val;
        }
    }
    return true;
}


static bool
setSampleFactors (j_compress_ptr const cinfo,
                  const char *   const arg) {
/*----------------------------------------------------------------------------
  Process a sample-factors parameter string, of the form
  HxV[,HxV,...]

  If there are more components than parameters, "1x1" is assumed for the rest.
-----------------------------------------------------------------------------*/
    int val1, val2;
    char ch1, ch2;
    unsigned int i;
    unsigned int ci;

    for (ci = 0, i = 0; ci < MAX_COMPONENTS; ++ci) {
        if (arg[i]) {
            ch2 = ',';		/* if not set by sscanf, will be ',' */
            if (sscanf(&arg[i], "%d%c%d%c", &val1, &ch1, &val2, &ch2) < 3)
                return false;
            if ((ch1 != 'x' && ch1 != 'X') || ch2 != ',') /* syntax check */
                return false;
            if (val1 <= 0 || val1 > 4) {
                pm_message("Invalid sampling factor: %d.  "
                           "JPEG sampling factors must be 1..4", val1);
                return false;
            }
            if (val2 <= 0 || val2 > 4) {
                pm_message("Invalid sampling factor: %d.  "
                           "JPEG sampling factors must be 1..4", val2);
                return false;
            }
            cinfo->comp_info[ci].h_samp_factor = val1;
            cinfo->comp_info[ci].v_samp_factor = val2;

            while (arg[i] && arg[i] != ',') {
                /* advance to next segment of arg string */
                ++i;
            }
        } else {
            /* reached end of parameter, set remaining components
               to 1x1 sampling */
            cinfo->comp_info[ci].h_samp_factor = 1;
            cinfo->comp_info[ci].v_samp_factor = 1;
        }
    }
    return true;
}



static void
setupJpeg(struct jpeg_compress_struct * const cinfoP,
          struct jpeg_error_mgr       * const jerrP,
          struct CmdlineInfo            const cmdline,
          unsigned int                  const width,
          unsigned int                  const height,
          pixval                        const maxval,
          int                           const inputFmt,
          FILE *                        const ofP) {

    int quality;
    int qScaleFactor;

    /* Initialize the JPEG compression object with default error handling. */
    cinfoP->err = jpeg_std_error(jerrP);
    jpeg_create_compress(cinfoP);

    setupJpegSourceParameters(cinfoP, width, height, inputFmt);

    jpeg_set_defaults(cinfoP);

    cinfoP->data_precision = BITS_IN_JSAMPLE;
        /* we always rescale data to this */
    cinfoP->image_width = width;
    cinfoP->image_height = height;

    cinfoP->arith_code = cmdline.arithmetic;
    cinfoP->dct_method = cmdline.dctMethod;
    if (cmdline.tracelevel == 0 && cmdline.verbose)
        cinfoP->err->trace_level = 1;
    else cinfoP->err->trace_level = cmdline.tracelevel;
    if (cmdline.grayscale)
        jpeg_set_colorspace(cinfoP, JCS_GRAYSCALE);
    else if (cmdline.rgb)
        /* This is not legal if the input is not JCS_RGB too, i.e. it's PPM */
        jpeg_set_colorspace(cinfoP, JCS_RGB);
    else
        /* This default will be based on the in_color_space set above */
        jpeg_default_colorspace(cinfoP);
    if (cmdline.maxMemoryToUse != -1)
        cinfoP->mem->max_memory_to_use = cmdline.maxMemoryToUse;
    cinfoP->optimize_coding = cmdline.optimize;
    if (cmdline.quality == -1) {
        quality = 75;
        qScaleFactor = 100;
    } else {
        quality = cmdline.quality;
        qScaleFactor = jpeg_quality_scaling(cmdline.quality);
    }
    if (cmdline.smooth != -1)
        cinfoP->smoothing_factor = cmdline.smooth;

    /* Set quantization tables for selected quality. */
    /* Some or all may be overridden if user specified --qtables. */
    jpeg_set_quality(cinfoP, quality, cmdline.baseline);

    if (cmdline.qtablefile != NULL) {
        if (! readQuantTables(cinfoP, cmdline.qtablefile,
                              qScaleFactor, cmdline.baseline))
            pm_error("Can't use quantization table file '%s'.",
                     cmdline.qtablefile);
    }

    if (cmdline.qslots != NULL) {
        if (! setQuantSlots(cinfoP, cmdline.qslots))
            pm_error("Bad quantization-table-selectors parameter string '%s'.",
                     cmdline.qslots);
    }

    if (cmdline.sample != NULL) {
        if (! setSampleFactors(cinfoP, cmdline.sample))
            pm_error("Bad sample-factors parameters string '%s'.",
                     cmdline.sample);
    }

    if (cmdline.progressive)
        jpeg_simple_progression(cinfoP);

    if (cmdline.densitySpec)
        setupJpegDensity(cinfoP, cmdline.density);

    if (cmdline.scans != NULL) {
        if (! readScanScript(cinfoP, cmdline.scans)) {
            pm_message("Error in scan script '%s'.", cmdline.scans);
        }
    }

    /* Specify data destination for compression */
    jpeg_stdio_dest(cinfoP, ofP);

    if (cmdline.verbose)
        reportCompressor(*cinfoP);

    /* Start compressor */
    jpeg_start_compress(cinfoP, TRUE);

}



static void
writeExifHeader(struct jpeg_compress_struct * const cinfoP,
                const char *                  const exifFileNm) {
/*----------------------------------------------------------------------------
   Generate an APP1 marker in the JFIF output that is an Exif header.

   The contents of the Exif header are in the file with filespec
   'exifFileNm' (file spec and contents are not validated).

   exifFileNm = "-" means Standard Input.

   If the file contains just two bytes of zero, don't write any marker
   but don't recognize any error either.
-----------------------------------------------------------------------------*/
    FILE * exifFp;
    unsigned short length;

    exifFp = pm_openr(exifFileNm);

    pm_readbigshort(exifFp, (short*)&length);

    if (length == 0) {
        /* Special value meaning "no header" */
    } else if (length < 3)
        pm_error("Invalid length %u at start of exif file", length);
    else {
        unsigned char * exifData;
        size_t const dataLength = length - 2;
            /* Subtract 2 byte length field*/
        size_t rc;

        assert(dataLength > 0);

        MALLOCARRAY(exifData, dataLength);
        if (!exifData)
            pm_error("Unable to allocate %u bytes for exif header buffer",
                     (unsigned)dataLength);

        rc = fread(exifData, 1, dataLength, exifFp);

        if (rc != dataLength)
            pm_error("Premature end of file on exif header file.  Should be "
                     "%u bytes of data, read only %u",
                     (unsigned)dataLength, (unsigned)rc);

        jpeg_write_marker(cinfoP, JPEG_APP0+1,
                          (const JOCTET *) exifData, dataLength);

        free(exifData);
    }

    pm_close(exifFp);
}



static void
computeRescalingArray(JSAMPLE **                  const rescaleP,
                      pixval                      const maxval,
                      struct jpeg_compress_struct const cinfo) {
/*----------------------------------------------------------------------------
   Compute the rescaling array for a maximum pixval of 'maxval'.
   Allocate the memory for it too.
-----------------------------------------------------------------------------*/
    long const halfMaxval = maxval / 2;

    JSAMPLE * rescale;
    long val;

    rescale = (JSAMPLE *)
        (cinfo.mem->alloc_small) ((j_common_ptr) &cinfo, JPOOL_IMAGE,
                                  (size_t) (((long) maxval + 1L) *
                                            sizeof(JSAMPLE)));
    for (val = 0; val <= maxval; val++) {
        /* The multiplication here must be done in 32 bits to avoid overflow */
        rescale[val] = (JSAMPLE) ((val*MAXJSAMPLE + halfMaxval)/maxval);
    }

    *rescaleP = rescale;
}



static void
translateRow(pixel        const pnm_buffer[],
             unsigned int const width,
             unsigned int const inputComponentCt,
             JSAMPLE      const translate[],
             JSAMPLE *    const jpegBuffer) {
/*----------------------------------------------------------------------------
   Convert the input row, in pnm format, to an output row in JPEG compressor
   input format.

   This is a byte for byte copy, translated through the array 'translate'.
-----------------------------------------------------------------------------*/
  unsigned int col;
  /* I'm not sure why the JPEG library data structures don't have some kind
     of pixel data structure (such that a row buffer is an array of pixels,
     rather than an array of samples).  But because of this, we have to
     index jpeg_buffer the old fashioned way.
     */

  switch (inputComponentCt) {
  case 1:
      for (col = 0; col < width; ++col)
          jpegBuffer[col] = translate[(int)PNM_GET1(pnm_buffer[col])];
      break;
  case 3:
      for (col = 0; col < width; ++col) {
          jpegBuffer[col * 3 + 0] =
              translate[(int)PPM_GETR(pnm_buffer[col])];
          jpegBuffer[col * 3 + 1] =
              translate[(int)PPM_GETG(pnm_buffer[col])];
          jpegBuffer[col * 3 + 2] =
              translate[(int)PPM_GETB(pnm_buffer[col])];
      }
      break;
  default:
      pm_error("INTERNAL ERROR: invalid number of input components in "
               "translate_row()");
  }

}



static void
convertScanLines(struct jpeg_compress_struct * const cinfoP,
                 FILE *                        const inputFileNm,
                 pixval                        const maxval,
                 int                           const inputFmt,
                 const JSAMPLE *               const xlateTable){
/*----------------------------------------------------------------------------
  Read scan lines from the input file, which is already opened in the
  netpbm library sense and ready for reading, and write them to the
  output JPEG object.  Translate the pnm sample values to JPEG sample
  values through the table xlateTable[].
-----------------------------------------------------------------------------*/
    xel * pnmBuffer;
        /* contains the row of the input image currently being processed,
           in pnm_readpnmrow format
        */
    JSAMPARRAY buffer;
        /* Row 0 of this array contains the row of the output image currently
           being processed, in JPEG compressor input format.  The array has
           only that one row.
        */

    /* Allocate the libpnm output and compressor input buffers */
    buffer = (*cinfoP->mem->alloc_sarray)
        ((j_common_ptr) cinfoP, JPOOL_IMAGE,
         (unsigned int) cinfoP->image_width * cinfoP->input_components,
         (unsigned int) 1);

    pnmBuffer = pnm_allocrow(cinfoP->image_width);

    while (cinfoP->next_scanline < cinfoP->image_height) {
        if (cinfoP->err->trace_level > 1)
            pm_message("Converting Row %d...", cinfoP->next_scanline);
        pnm_readpnmrow(inputFileNm, pnmBuffer, cinfoP->image_width,
                       maxval, inputFmt);
        translateRow(pnmBuffer,
                     cinfoP->image_width, cinfoP->input_components,
                     xlateTable,  buffer[0]);
        jpeg_write_scanlines(cinfoP, buffer, 1);
        if (cinfoP->err->trace_level > 1)
            pm_message("Done.");
    }

    pnm_freerow(pnmBuffer);
    /* Don't worry about the compressor input buffer; it gets freed
       automatically
    */
}



int
main(int           argc,
     const char ** argv) {

    struct CmdlineInfo cmdline;
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE * ifP;
    FILE * ofP;
    int height;
        /* height of the input image in rows, as specified by its header */
    int width;
        /* width of the input image in columns, as specified by its header */
    pixval maxval;
        /* maximum value of an input pixel component, as specified by header */
    int inputFmt;
        /* The input format, as determined by its header.  */
    JSAMPLE *rescale;         /* => maxval-remapping array, or NULL */
        /* This is an array that maps each possible pixval in the input to
           a new value such that while the range of the input values is
           0 .. maxval, the range of the output values is 0 .. MAXJSAMPLE.
        */

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileNm);

    ofP = stdout;

    /* Open the pnm input */
    pnm_readpnminit(ifP, &width, &height, &maxval, &inputFmt);
    if (cmdline.verbose) {
        pm_message("Input file has format %c%c.\n"
                   "It has %d rows of %d columns of pixels "
                   "with max sample value of %d.",
                   (char) (inputFmt/256), (char) (inputFmt % 256),
                   height, width, maxval);
    }

    setupJpeg(&cinfo, &jerr, cmdline, width, height, maxval, inputFmt, ofP);

    computeRescalingArray(&rescale, maxval, cinfo);

    if (cmdline.comment)
        jpeg_write_marker(&cinfo, JPEG_COM, (const JOCTET *) cmdline.comment,
                          strlen(cmdline.comment));

    if (cmdline.exif)
        writeExifHeader(&cinfo, cmdline.exif);

    /* Translate and copy over the actual scanlines */
    convertScanLines(&cinfo, ifP, maxval, inputFmt, rescale);

    /* Finish compression and release memory */
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    /* Close files, if we opened them */
    if (ifP != stdin)
        pm_close(ifP);

    free(cmdline.inputFileNm);
    /* Program may have exited with non-zero completion code via
       various function calls above.
    */
    return jerr.num_warnings > 0 ? EXIT_WARNING : EXIT_SUCCESS;
}



