/*----------------------------------------------------------------------------
                               pamstack
------------------------------------------------------------------------------
  Part of the Netpbm package.

  Combine the channels (stack the planes) of multiple PAM images to create
  a single PAM image.


  By Bryan Henderson, San Jose CA 2000.08.05

  Contributed to the public domain by its author 2002.05.05.
-----------------------------------------------------------------------------*/

#include <string.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pam.h"

#define MAX_INPUTS 16
    /* The most input PAMs we allow user to specify */

enum MaxvalScaling {
    /* How to scale maxvals if the inputs don't all have the same maxval */
    MAXVALSCALE_NONE,  /* Don't scale -- fail program */
    MAXVALSCALE_FIRST, /* Scale everything to maxval of first input */
    MAXVALSCALE_LCM    /* Scale everything to least common multiple */
};

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    unsigned int nInput;
        /* The number of input PAMs.  At least 1, at most 16. */
    const char * inputFileName[MAX_INPUTS];
        /* The PAM files to combine, in order. */
    const char * tupletype;
    enum MaxvalScaling maxvalScaling;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec strings we return are stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;
    extern struct pam pam;  /* Just so we can look at field sizes */

    unsigned int option_def_index;
    unsigned int tupletypeSpec, firstmaxvalSpec, lcmmaxvalSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "tupletype",   OPT_STRING, &cmdlineP->tupletype,
            &tupletypeSpec, 0);
    OPTENT3(0, "firstmaxval", OPT_FLAG, NULL, &firstmaxvalSpec, 0);
    OPTENT3(0, "lcmmaxval",   OPT_FLAG, NULL, &lcmmaxvalSpec,   0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We may have parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!tupletypeSpec)
        cmdlineP->tupletype = "";
    else
        if (strlen(cmdlineP->tupletype)+1 > sizeof(pam.tuple_type))
            pm_error("Tuple type name specified is too long.  Maximum of "
                     "%u characters allowed.",
                     (unsigned)sizeof(pam.tuple_type));

    if (firstmaxvalSpec) {
        if (lcmmaxvalSpec)
            pm_error("Cannot specify both -lcmmaxval and -firstmaxval");
        else
            cmdlineP->maxvalScaling = MAXVALSCALE_FIRST;
    } else if (lcmmaxvalSpec) {
        if (firstmaxvalSpec)
            pm_error("Cannot specify both -lcmmaxval and -firstmaxval");
        else
            cmdlineP->maxvalScaling = MAXVALSCALE_LCM;
    } else
        cmdlineP->maxvalScaling = MAXVALSCALE_NONE;

    cmdlineP->nInput = 0;  /* initial value */
    {
        unsigned int argn;
        bool stdinUsed;
        for (argn = 1, stdinUsed = false; argn < argc; ++argn) {
            if (cmdlineP->nInput >= MAX_INPUTS)
                pm_error("You may not specify more than %u input images.",
                         MAX_INPUTS);
            cmdlineP->inputFileName[cmdlineP->nInput++] = argv[argn];
            if (streq(argv[argn], "-")) {
                if (stdinUsed)
                    pm_error("You cannot specify Standard Input ('-') "
                             "for more than one input file");
                stdinUsed = true;
            }
        }
    }
    if (cmdlineP->nInput < 1)
        cmdlineP->inputFileName[cmdlineP->nInput++] = "-";
}



static void
openAllStreams(unsigned int  const nInput,
               const char ** const inputFileName,
               FILE **       const ifP) {

    unsigned int inputSeq;

    for (inputSeq = 0; inputSeq < nInput; ++inputSeq)
        ifP[inputSeq] = pm_openr(inputFileName[inputSeq]);
}



static void
outputRaster(const struct pam * const inpam,  /* array */
             unsigned int       const nInput,
             struct pam         const outpam) {
/*----------------------------------------------------------------------------
   Write the raster of the output image according to 'outpam'.  Compose it
   from the 'nInput' input images described by 'inpam'.

   'outpam' may indicate a different maxval from some or all of the input
   images.
-----------------------------------------------------------------------------*/
    tuple * inrow;
    tuple * outrow;

    outrow = pnm_allocpamrow(&outpam);
    inrow  = pnm_allocpamrow(&outpam);

    {
        unsigned int row;

        for (row = 0; row < outpam.height; ++row) {
            unsigned int inputSeq;
            unsigned int outPlane;

            for (inputSeq = 0, outPlane = 0; inputSeq < nInput; ++inputSeq) {
                struct pam thisInpam = inpam[inputSeq];
                unsigned int col;

                pnm_readpamrow(&thisInpam, inrow);

                pnm_scaletuplerow(&thisInpam, inrow, inrow, outpam.maxval);

                for (col = 0; col < outpam.width; ++col) {
                    unsigned int inPlane;
                    for (inPlane = 0; inPlane < thisInpam.depth; ++inPlane) {
                        outrow[col][outPlane+inPlane] = inrow[col][inPlane];
                    }
                }
                outPlane += thisInpam.depth;
            }
            pnm_writepamrow(&outpam, outrow);
        }
    }
    pnm_freepamrow(outrow);
    pnm_freepamrow(inrow);
}



static void
processOneImageInAllStreams(unsigned int       const nInput,
                            FILE *             const ifP[],
                            FILE *             const ofP,
                            const char *       const tupletype,
                            enum MaxvalScaling const maxvalScaling) {
/*----------------------------------------------------------------------------
   Take one image from each of the 'nInput' open input streams ifP[]
   and stack them into one output image on *ofP.

   Take the images from the current positions of those streams and leave
   the streams positioned after them.

   Make the output image have tuple type 'tupletype'.

   Scale input samples for output according to 'maxvalScaling'.
-----------------------------------------------------------------------------*/
    struct pam inpam[MAX_INPUTS];   /* Input PAM images */
    struct pam outpam;  /* Output PAM image */

    unsigned int inputSeq;
        /* The horizontal sequence -- i.e. the sequence of the
           input stream, not the sequence of an image within a
           stream.
        */

    unsigned int outputDepth;
    outputDepth = 0;  /* initial value */
    sample maxvalLcm;
        /* Least common multiple of all maxvals or PNM_OVERALLMAXVAL if the
           LCM is greater than that.
        */
    bool allImagesSameMaxval;
        /* The images all have the same maxval */

    for (inputSeq = 0, allImagesSameMaxval = true, maxvalLcm = 1;
         inputSeq < nInput;
         ++inputSeq) {

        pnm_readpaminit(ifP[inputSeq], &inpam[inputSeq],
                        PAM_STRUCT_SIZE(tuple_type));

        /* All images, including this one, must have same dimensions as
           the first image.
        */
        if (inpam[inputSeq].width != inpam[0].width)
            pm_error("Image no. %u does not have the same width as "
                     "Image 0.", inputSeq);
        if (inpam[inputSeq].height != inpam[0].height)
            pm_error("Image no. %u does not have the same height as "
                     "Image 0.", inputSeq);

        if (inpam[inputSeq].maxval != inpam[0].maxval)
            allImagesSameMaxval = false;

        maxvalLcm = pm_lcm(maxvalLcm, inpam[inputSeq].maxval, 1,
                           PAM_OVERALL_MAXVAL);

        outputDepth += inpam[inputSeq].depth;
    }

    outpam        = inpam[0];     /* Initial value */

    switch (maxvalScaling) {
    case MAXVALSCALE_NONE:
        if (!allImagesSameMaxval)
            pm_message("Inputs do not all have same maxval.  "
                       "Consider -firstmaxval or -lcmmaxval");
        outpam.maxval = inpam[0].maxval;
        break;
    case MAXVALSCALE_FIRST:
        outpam.maxval = inpam[0].maxval;
        if (!allImagesSameMaxval)
            pm_message("Input maxvals vary; making output maxval %lu "
                       "per -firstmaxval", outpam.maxval);
        break;
    case MAXVALSCALE_LCM:
        outpam.maxval = maxvalLcm;
        if (!allImagesSameMaxval)
            pm_message("Input maxvals vary; making output maxval %lu "
                       "per -lcmmaxval", outpam.maxval);
        break;
    }
    outpam.depth  = outputDepth;
    outpam.file   = ofP;
    outpam.format = PAM_FORMAT;
    strcpy(outpam.tuple_type, tupletype);

    pm_message("Writing %u channel PAM image", outpam.depth);

    pnm_writepaminit(&outpam);

    outputRaster(inpam, nInput, outpam);
}



static void
nextImageAllStreams(unsigned int const nInput,
                    FILE *       const ifP[],
                    bool *       const eofP) {
/*----------------------------------------------------------------------------
   Advance all the streams ifP[] to the next image.

   Return *eofP == TRUE iff at least one stream has no next image.
-----------------------------------------------------------------------------*/
    unsigned int inputSeq;

    for (inputSeq = 0; inputSeq < nInput; ++inputSeq) {
        int eof;
        pnm_nextimage(ifP[inputSeq], &eof);
        if (eof)
            *eofP = true;
    }
}



int
main(int argc, const char *argv[]) {

    struct CmdlineInfo cmdline;
    FILE * ifP[MAX_INPUTS];
    bool eof;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    openAllStreams(cmdline.nInput, cmdline.inputFileName, ifP);

    eof = FALSE;
    while (!eof) {
        processOneImageInAllStreams(cmdline.nInput, ifP, stdout,
                                    cmdline.tupletype, cmdline.maxvalScaling);

        nextImageAllStreams(cmdline.nInput, ifP, &eof);
    }

    return 0;
}



