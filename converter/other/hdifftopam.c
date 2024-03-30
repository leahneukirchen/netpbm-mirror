/******************************************************************************
                                hdifftopam
*******************************************************************************
  This program recovers a PAM image from a horizontal difference images
  such as created by Pamtohdiff.

  By Bryan Henderson, San Jose, CA 2002.04.15.
******************************************************************************/
#include <string.h>
#include <stdio.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pam.h"

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileNm;  /* Names of input files */
    unsigned int pnm;
    unsigned int verbose;
};



static void
parseCommandLine(int argc, const char ** const argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;   /* Used by OPTENT3 */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "pnm",       OPT_FLAG,    NULL, &cmdlineP->pnm,      0);
    OPTENT3(0, "verbose",   OPT_FLAG,    NULL, &cmdlineP->verbose,  0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 < 1)
        cmdlineP->inputFileNm = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileNm = argv[1];
    else
        pm_error("Too many arguments.");
}



static void
makePnm(struct pam * const pamP) {

    switch (pamP->depth) {
    case 1:
        pamP->format = PGM_FORMAT;
        break;
    case 3:
        pamP->format = PPM_FORMAT;
        break;
    default:
        pm_error("Input depth (%d) does not correspond to a PNM format.",
                 pamP->depth);
    }
}



static void
    describeOutput(struct pam const pam) {

    pm_message("Output is %d x %d x %d, maxval %u",
               pam.width, pam.height, pam.depth, (unsigned int) pam.maxval);
}



int
main(int argc, const char ** argv) {

    FILE * ifP;
    struct CmdlineInfo cmdline;
    struct pam diffpam, outpam;
    unsigned int row;
    tuple * diffrow;
    tuple * outrow;
    tuple * prevrow;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileNm);

    pnm_readpaminit(ifP, &diffpam, PAM_STRUCT_SIZE(tuple_type));

    if (diffpam.format != PAM_FORMAT)
        pm_error("Input must be a PAM file, not PNM");
    else if (!streq(diffpam.tuple_type, "hdiff"))
        pm_error("Input tuple type is '%s'.  Must be 'hdiff'",
                 diffpam.tuple_type);

    outpam = diffpam;
    outpam.file = stdout;
    strcpy(outpam.tuple_type, "unhdiff");

    if (cmdline.verbose)
        describeOutput(outpam);
    if (cmdline.pnm)
        makePnm(&outpam);

    pnm_writepaminit(&outpam);

    diffrow = pnm_allocpamrow(&diffpam);
    outrow =  pnm_allocpamrow(&outpam);
    prevrow = pnm_allocpamrow(&diffpam);

    pnm_setpamrow(&diffpam, prevrow, 0);

    {
        unsigned int const bias = diffpam.maxval/2;

        for (row = 0; row < diffpam.height; ++row) {
            unsigned int col;
            pnm_readpamrow(&diffpam, diffrow);
            for (col = 0; col < diffpam.width; ++col) {
                unsigned int plane;
                for (plane = 0; plane < diffpam.depth; ++plane) {
                    sample const prevSample = prevrow[col][plane];
                    sample const diffSample = diffrow[col][plane];

                    outrow[col][plane] =
                        (-bias + prevSample + diffSample) % (outpam.maxval+1);
                    prevrow[col][plane] = outrow[col][plane];
                }
            }
            pnm_writepamrow(&outpam, outrow);
        }
    }
    pnm_freepamrow(prevrow);
    pnm_freepamrow(outrow);
    pnm_freepamrow(diffrow);

    exit(0);
}



