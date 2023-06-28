/*****************************************************************************
                              pamtojpeg2k
******************************************************************************

  Convert a PNM image to JPEG-2000 code stream image

  By Bryan Henderson, San Jose CA  2002.10.26

*****************************************************************************/

#define _DEFAULT_SOURCE 1 /* New name for SVID & BSD source defines */
#define _BSD_SOURCE 1    /* Make sure strdup() is in string.h */
#define _XOPEN_SOURCE 500 /* Make sure strdup() is in string.h */
    /* In 2014.09, this was _XOPEN_SOURCE 600, with a comment saying it was
       necessary to make <inttypes.h> define int_fast32_t, etc. on AIX.
       <jasper/jasper.h> does use int_fast32_t and does include <inttypes.h>,
       but plenty of source files of libjasper do too, and they did not have
       _XOPEN_SOURCE 600, so it would seem to be superfluous here too.
    */

#include <string.h>

#include <jasper/jasper.h>

#include "pm_c_util.h"
#include "pam.h"
#include "shhopt.h"
#include "nstring.h"
#include "mallocvar.h"

#include "libjasper_compat.h"


enum compmode {COMPMODE_INTEGER, COMPMODE_REAL};

enum progression {PROG_LRCP, PROG_RLCP, PROG_RPCL, PROG_PCRL, PROG_CPRL};

struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    char * inputFilename;
    unsigned int imgareatlx;
    unsigned int imgareatly;
    unsigned int tilegrdtlx;
    unsigned int tilegrdtly;
    unsigned int tilewidth;
    unsigned int tileheight;
    unsigned int prcwidth;
    unsigned int prcheight;
    unsigned int cblkwidth;
    unsigned int cblkheight;
    enum compmode compmode;
    unsigned int compressionSpec;
    float        compression;
    char *       ilyrrates;
    enum progression progression;
    unsigned int numrlvls;
    unsigned int numgbits;
    unsigned int nomct;
    unsigned int sop;
    unsigned int eph;
    unsigned int lazy;
    unsigned int termall;
    unsigned int segsym;
    unsigned int vcausal;
    unsigned int pterm;
    unsigned int resetprob;
    unsigned int debuglevel;  /* Jasper library debug level */
    unsigned int verbose;
};


static void
parseCommandLine(int argc, char ** argv,
                 struct cmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that many of the strings that this function returns in the
   *cmdline_p structure are actually in the supplied argv array.  And
   sometimes, one of these strings is actually just a suffix of an entry
   in argv!
-----------------------------------------------------------------------------*/
    optEntry *option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int imgareatlxSpec, imgareatlySpec;
    unsigned int tilegrdtlxSpec, tilegrdtlySpec;
    unsigned int tilewidthSpec, tileheightSpec;
    unsigned int prcwidthSpec, prcheightSpec;
    unsigned int cblkwidthSpec, cblkheightSpec;
    unsigned int modeSpec, ilyrratesSpec;
    unsigned int progressionSpec, numrlvlsSpec, numgbitsSpec;
    unsigned int debuglevelSpec;

    char * progressionOpt;
    char * modeOpt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "imgareatlx",   OPT_UINT,   &cmdlineP->imgareatlx,
            &imgareatlxSpec,       0);
    OPTENT3(0, "imgareatly",   OPT_UINT,   &cmdlineP->imgareatly,
            &imgareatlySpec,       0);
    OPTENT3(0, "tilegrdtlx",   OPT_UINT,   &cmdlineP->tilegrdtlx,
            &tilegrdtlxSpec,       0);
    OPTENT3(0, "tilegrdtly",   OPT_UINT,   &cmdlineP->tilegrdtly,
            &tilegrdtlySpec,       0);
    OPTENT3(0, "tilewidth",    OPT_UINT,   &cmdlineP->tilewidth,
            &tilewidthSpec,        0);
    OPTENT3(0, "tileheight",   OPT_UINT,   &cmdlineP->tileheight,
            &tileheightSpec,       0);
    OPTENT3(0, "prcwidth",     OPT_UINT,   &cmdlineP->prcwidth,
            &prcwidthSpec,       0);
    OPTENT3(0, "prcheight",    OPT_UINT,   &cmdlineP->prcheight,
            &prcheightSpec,      0);
    OPTENT3(0, "cblkwidth",    OPT_UINT,   &cmdlineP->cblkwidth,
            &cblkwidthSpec,      0);
    OPTENT3(0, "cblkheight",   OPT_UINT,   &cmdlineP->cblkheight,
            &cblkheightSpec,     0);
    OPTENT3(0, "mode",         OPT_STRING, &modeOpt,
            &modeSpec,           0);
    OPTENT3(0, "compression",  OPT_FLOAT,  &cmdlineP->compression,
            &cmdlineP->compressionSpec,    0);
    OPTENT3(0, "ilyrrates",    OPT_STRING, &cmdlineP->ilyrrates,
            &ilyrratesSpec,      0);
    OPTENT3(0, "progression",  OPT_STRING, &progressionOpt,
            &progressionSpec,    0);
    OPTENT3(0, "numrlvls",     OPT_UINT,   &cmdlineP->numrlvls,
            &numrlvlsSpec,       0);
    OPTENT3(0, "numgbits",     OPT_UINT,   &cmdlineP->numgbits,
            &numgbitsSpec,       0);
    OPTENT3(0, "nomct",        OPT_FLAG,   NULL,
            &cmdlineP->nomct,    0);
    OPTENT3(0, "sop",          OPT_FLAG,   NULL,
            &cmdlineP->sop,      0);
    OPTENT3(0, "eph",          OPT_FLAG,   NULL,
            &cmdlineP->eph,      0);
    OPTENT3(0, "lazy",         OPT_FLAG,   NULL,
            &cmdlineP->lazy,     0);
    OPTENT3(0, "termall",      OPT_FLAG,   NULL,
            &cmdlineP->termall,  0);
    OPTENT3(0, "segsym",       OPT_FLAG,   NULL,
            &cmdlineP->segsym,    0);
    OPTENT3(0, "vcausal",      OPT_FLAG,   NULL,
            &cmdlineP->vcausal,   0);
    OPTENT3(0, "pterm",        OPT_FLAG,   NULL,
            &cmdlineP->pterm,     0);
    OPTENT3(0, "resetprob",    OPT_FLAG,   NULL,
            &cmdlineP->resetprob, 0);
    OPTENT3(0, "verbose",      OPT_FLAG,   NULL,
            &cmdlineP->verbose,   0);
    OPTENT3(0, "debuglevel",   OPT_UINT,   &cmdlineP->debuglevel,
            &debuglevelSpec,      0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, argv, opt, sizeof(opt), 0);

    if (!imgareatlxSpec)
        cmdlineP->imgareatlx = 0;
    if (!imgareatlySpec)
        cmdlineP->imgareatly = 0;
    if (!tilegrdtlxSpec)
        cmdlineP->tilegrdtlx = 0;
    if (!tilegrdtlySpec)
        cmdlineP->tilegrdtly = 0;
    if (!tilewidthSpec)
        cmdlineP->tilewidth = 0;
    if (!tileheightSpec)
        cmdlineP->tileheight = 0;
    if (!prcwidthSpec)
        cmdlineP->prcwidth = 32768;
    if (!prcheightSpec)
        cmdlineP->prcheight = 32768;
    if (!cblkwidthSpec)
        cmdlineP->cblkwidth = 64;
    if (!cblkheightSpec)
        cmdlineP->cblkheight = 64;
    if (modeSpec) {
        if (strcmp(modeOpt, "integer") == 0 || strcmp(modeOpt, "int") == 0)
            cmdlineP->compmode = COMPMODE_INTEGER;
        else if (strcmp(modeOpt, "real") == 0)
            cmdlineP->compmode = COMPMODE_REAL;
        else
            pm_error("Invalid value for 'mode' option: '%s'.  "
                     "valid values are 'INTEGER' and 'REAL'", modeOpt);
    } else
        cmdlineP->compmode = COMPMODE_INTEGER;
    if (!ilyrratesSpec)
        cmdlineP->ilyrrates = (char*) "";
    if (progressionSpec) {
        if (streq(progressionOpt, "lrcp"))
            cmdlineP->progression = PROG_LRCP;
        else if (streq(progressionOpt, "rlcp"))
            cmdlineP->progression = PROG_RLCP;
        else if (streq(progressionOpt, "rpcl"))
            cmdlineP->progression = PROG_RPCL;
        else if (streq(progressionOpt, "pcrl"))
            cmdlineP->progression = PROG_PCRL;
        else if (streq(progressionOpt, "cprl"))
            cmdlineP->progression = PROG_CPRL;
        else
            pm_error("Invalid value for -progression: '%s'.  "
                     "Valid values are lrcp, rlcp, rpcl, pcrl, and cprl.",
                     progressionOpt);
    } else
        cmdlineP->progression = PROG_LRCP;
    if (!numrlvlsSpec)
        cmdlineP->numrlvls = 6;
    if (!numgbitsSpec)
        cmdlineP->numgbits = 2;
    if (!debuglevelSpec)
        cmdlineP->debuglevel = 0;

    if (argc - 1 == 0)
        cmdlineP->inputFilename = strdup("-");  /* he wants stdin */
    else if (argc - 1 == 1)
        cmdlineP->inputFilename = strdup(argv[1]);
    else
        pm_error("Too many arguments.  The only argument accepted\n"
                 "is the input file specification");

}



static void
createJasperRaster(struct pam *  const inpamP,
                   jas_image_t * const jasperP) {
/*----------------------------------------------------------------------------
   Create the raster in the *jasperP object, reading the raster from the
   input file described by *inpamP, which is positioned to the raster.
-----------------------------------------------------------------------------*/
    jas_matrix_t ** matrix;  /* malloc'ed */
        /* matrix[X] is the data for Plane X of the current row */
    unsigned int plane;
    unsigned int row;
    tuple * tuplerow;
    bool oddMaxval;
    sample jasperMaxval;

    MALLOCARRAY_NOFAIL(matrix, inpamP->depth);

    for (plane = 0; plane < inpamP->depth; ++plane) {
        matrix[plane] = jas_matrix_create(1, inpamP->width);

        if (matrix[plane] == NULL)
            pm_error("Unable to create matrix for plane %u.  "
                     "jas_matrix_create() failed.", plane);
    }
    tuplerow = pnm_allocpamrow(inpamP);

    jasperMaxval = pm_bitstomaxval(pm_maxvaltobits(inpamP->maxval));
    oddMaxval = jasperMaxval != inpamP->maxval;

    for (row = 0; row < inpamP->height; ++row) {
        unsigned int col;

        pnm_readpamrow(inpamP, tuplerow);

        for (col = 0; col < inpamP->width; ++col) {
            unsigned int plane;
            for (plane = 0; plane < inpamP->depth; ++plane) {
                unsigned int jasperSample;

                if (oddMaxval)
                    jasperSample = tuplerow[col][plane] *
                        jasperMaxval / inpamP->maxval;
                else
                    jasperSample = tuplerow[col][plane];

                jas_matrix_set(matrix[plane], 0, col, jasperSample);
            }
        }
        {
            unsigned int plane;

            for (plane = 0; plane < inpamP->depth; ++plane) {
                int rc;
                rc = jas_image_writecmpt(jasperP, plane, 0, row,
                                         inpamP->width, 1,
                                         matrix[plane]);
                if (rc != 0)
                    pm_error("jas_image_writecmpt() of plane %u failed.",
                             plane);
            }
        }
    }

    pnm_freepamrow(tuplerow);
    for (plane = 0; plane < inpamP->depth; ++plane)
        jas_matrix_destroy(matrix[plane]);

    free(matrix);
}



static void
createJasperImage(struct pam *   const inpamP,
                  jas_image_t ** const jasperPP) {

	jas_image_cmptparm_t * cmptparms;
    unsigned int plane;

    MALLOCARRAY_NOFAIL(cmptparms, inpamP->depth);

    for (plane = 0; plane < inpamP->depth; ++plane) {
        cmptparms[plane].tlx = 0;
        cmptparms[plane].tly = 0;
        cmptparms[plane].hstep = 1;
        cmptparms[plane].vstep = 1;
        cmptparms[plane].width = inpamP->width;
        cmptparms[plane].height = inpamP->height;
        cmptparms[plane].prec = pm_maxvaltobits(inpamP->maxval);
        cmptparms[plane].sgnd = 0;
    }
    *jasperPP =
        jas_image_create(inpamP->depth, cmptparms, JAS_CLRSPC_UNKNOWN);
    if (*jasperPP == NULL)
        pm_error("Unable to create jasper image structure.  "
                 "jas_image_create() failed.");

    free(cmptparms);
}



static void
convertToJasperImage(struct pam *   const inpamP,
                     jas_image_t ** const jasperPP) {

    jas_image_t * jasperP;

    createJasperImage(inpamP, &jasperP);

    if (strneq(inpamP->tuple_type, "RGB", 3)) {
        if (inpamP->depth < 3)
            pm_error("Input tuple type is RGB*, but depth is only %d.  "
                     "It should be at least 3.", inpamP->depth);
        else {
            jas_image_setclrspc(jasperP, JAS_CLRSPC_GENRGB);
            jas_image_setcmpttype(jasperP, 0,
                                  JAS_IMAGE_CT_COLOR(JAS_IMAGE_CT_RGB_R));
            jas_image_setcmpttype(jasperP, 1,
                                  JAS_IMAGE_CT_COLOR(JAS_IMAGE_CT_RGB_G));
            jas_image_setcmpttype(jasperP, 2,
                                  JAS_IMAGE_CT_COLOR(JAS_IMAGE_CT_RGB_B));
        }
    } else {
        if (strneq(inpamP->tuple_type, "GRAYSCALE", 9) ||
            strneq(inpamP->tuple_type, "BLACKANDWHITE", 13)) {
            jas_image_setclrspc(jasperP, JAS_CLRSPC_GENGRAY);
            jas_image_setcmpttype(jasperP, 0,
                                  JAS_IMAGE_CT_COLOR(JAS_IMAGE_CT_GRAY_Y));
        }
    }

    createJasperRaster(inpamP, jasperP);

    *jasperPP = jasperP;
}



static void
writeJpc(jas_image_t *      const jasperP,
         struct cmdlineInfo const cmdline,
         FILE *             const ofP) {

    jas_stream_t * outStreamP;
    const char * options;
    const char * ilyrratesOpt;
    const char * prgValue;
    char rateOpt[20+1];

    /* Note: ilyrrates is a hack because we're too lazy to properly parse
       command line options to get the information and then compose
       a proper input to Jasper.  So the user can screw things up by
       specifying garbage for the -ilyrrates option
    */
    if (strlen(cmdline.ilyrrates) > 0)
        pm_asprintf(&ilyrratesOpt, "ilyrrates=%s", cmdline.ilyrrates);
    else
        ilyrratesOpt = strdup("");

    switch(cmdline.progression) {
    case PROG_LRCP: prgValue = "lrcp"; break;
    case PROG_RLCP: prgValue = "rlcp"; break;
    case PROG_RPCL: prgValue = "rcpc"; break;
    case PROG_PCRL: prgValue = "pcrl"; break;
    case PROG_CPRL: prgValue = "cprl"; break;
    }

    /* Note that asprintfN() doesn't understand %f, but sprintf() does */

    if (cmdline.compressionSpec)
        sprintf(rateOpt, "rate=%1.9f", 1.0/cmdline.compression);
    else {
        /* No 'rate' option.  This means there is no constraint on the image
           size, so the encoder will compress losslessly.  Note that the
           image may get larger, because of metadata.
        */
        rateOpt[0] = '\0';
    }
    pm_asprintf(&options,
                "imgareatlx=%u "
                "imgareatly=%u "
                "tilegrdtlx=%u "
                "tilegrdtly=%u "
                "tilewidth=%u "
                "tileheight=%u "
                "prcwidth=%u "
                "prcheight=%u "
                "cblkwidth=%u "
                "cblkheight=%u "
                "mode=%s "
                "%s "    /* rate */
                "%s "    /* ilyrrates */
                "prg=%s "
                "numrlvls=%u "
                "numgbits=%u "
                "%s %s %s %s %s %s %s %s %s",

                cmdline.imgareatlx,
                cmdline.imgareatly,
                cmdline.tilegrdtlx,
                cmdline.tilegrdtly,
                cmdline.tilewidth,
                cmdline.tileheight,
                cmdline.prcwidth,
                cmdline.prcheight,
                cmdline.cblkwidth,
                cmdline.cblkheight,
                cmdline.compmode == COMPMODE_INTEGER ? "int" : "real",
                rateOpt,
                ilyrratesOpt,
                prgValue,
                cmdline.numrlvls,
                cmdline.numgbits,
                cmdline.nomct     ? "nomct"     : "",
                cmdline.sop       ? "sop"       : "",
                cmdline.eph       ? "eph"       : "",
                cmdline.lazy      ? "lazy"      : "",
                cmdline.termall   ? "termall"   : "",
                cmdline.segsym    ? "segsym"    : "",
                cmdline.vcausal   ? "vcausal"   : "",
                cmdline.pterm     ? "pterm"     : "",
                cmdline.resetprob ? "resetprob" : ""
        );

    pm_strfree(ilyrratesOpt);

    /* Open the output image file (Standard Output) */
    outStreamP = jas_stream_fdopen(fileno(ofP), "w+b");
    if (outStreamP == NULL)
        pm_error("Unable to open output stream.  jas_stream_fdopen() "
                 "failed");

    {
        int rc;

        if (cmdline.verbose)
            pm_message("Using Jasper to encode to 'jpc' format with options "
                       "'%s'", options);

        rc = jas_image_encode(jasperP, outStreamP,
                              jas_image_strtofmt((char*)"jpc"),
                              (char *)options);
        if (rc != 0)
            pm_error("jas_image_encode() failed to encode the JPEG 2000 "
                     "image.  Rc=%d", rc);
    }
	jas_stream_flush(outStreamP);

    {
        int rc;

        rc = jas_stream_close(outStreamP);

        if (rc != 0)
            pm_error("Failed to close output stream, "
                     "jas_stream_close() rc = %d", rc);
    }

	jas_image_clearfmts();

    pm_strfree(options);
}



int
main(int argc, char **argv)
{
    struct cmdlineInfo cmdline;
    FILE * ifP;
    struct pam inpam;
    jas_image_t * jasperP;

    pnm_init(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    {
        int rc;

        rc = jas_init();
        if ( rc != 0 )
            pm_error("Failed to initialize Jasper library.  "
                     "jas_init() returns rc %d", rc );
    }

    jas_setdbglevel(cmdline.debuglevel);

    ifP = pm_openr(cmdline.inputFilename);

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    convertToJasperImage(&inpam, &jasperP);

    writeJpc(jasperP, cmdline, stdout);

	jas_image_destroy(jasperP);

    pm_close(ifP);

    pm_close(stdout);

    return 0;
}
