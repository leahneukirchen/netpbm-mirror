/*=========================================================================
                             ppmcolormask
===========================================================================

  This program produces a PBM mask of areas containing a certain color.

  By Bryan Henderson, Olympia WA; April 2000.

  Contributed to the public domain by its author.
=========================================================================*/

#define _DEFAULT_SOURCE /* New name for SVID & BSD source defines */
#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */
#define _BSD_SOURCE  /* Make sure strdup() is in <string.h> */
#include <assert.h>
#include <string.h>

#include "pm_c_util.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "nstring.h"
#include "ppm.h"
#include "pam.h"

typedef enum {
    MATCH_EXACT,
    MATCH_BK
} MatchType;

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFilename;
    unsigned int colorCt;
    struct {
        MatchType matchType;
        union {
            tuplen   color;   /* matchType == MATCH_EXACT */
            bk_color bkColor; /* matchType == MATCH_BK */
        } u;
    } maskColor[16];
    unsigned int verbose;
};



static void
freeCmdline(struct CmdlineInfo * const cmdlineP) {

    unsigned int i;

    for (i = 0; i < cmdlineP->colorCt; ++ i) {
        if (cmdlineP->maskColor[i].matchType == MATCH_EXACT)
            free(cmdlineP->maskColor[i].u.color);
    }
}



static void
parseColorOpt(const char *         const colorOpt,
              struct CmdlineInfo * const cmdlineP) {

    unsigned int colorCt;
    char * colorOptWork;
    char * cursor;
    bool eol;
    
    colorOptWork = strdup(colorOpt);
    cursor = &colorOptWork[0];
    
    eol = FALSE;    /* initial value */
    colorCt = 0;    /* initial value */
    while (!eol && colorCt < ARRAY_SIZE(cmdlineP->maskColor)) {
        const char * token;
        token = pm_strsep(&cursor, ",");
        if (token) {
            if (strneq(token, "bk:", 3)) {
                cmdlineP->maskColor[colorCt].matchType = MATCH_BK;
                cmdlineP->maskColor[colorCt].u.bkColor =
                    ppm_bk_color_from_name(&token[3]);
            } else {
                cmdlineP->maskColor[colorCt].matchType = MATCH_EXACT;
                cmdlineP->maskColor[colorCt].u.color =
                    pnm_parsecolorn(token);
            }
            ++colorCt;
        } else
            eol = TRUE;
    }
    free(colorOptWork);

    cmdlineP->colorCt = colorCt;
}



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo *cmdlineP) {
/*----------------------------------------------------------------------------
   Note that many of the strings that this function returns in the
   *cmdlineP structure are actually in the supplied argv array.  And
   sometimes, one of these strings is actually just a suffix of an entry
   in argv!
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options. */
    optStruct3 opt;

    unsigned int option_def_index;
    const char * colorOpt;
    unsigned int colorSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "color",      OPT_STRING, &colorOpt, &colorSpec,           0);
    OPTENT3(0, "verbose",    OPT_FLAG,   NULL, &cmdlineP->verbose,        0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We may have parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and all of *cmdlineP. */

    if (colorSpec)
        parseColorOpt(colorOpt, cmdlineP);

    if (colorSpec) {
        if (argc-1 < 1)
            cmdlineP->inputFilename = "-";  /* he wants stdin */
        else if (argc-1 == 1)
            cmdlineP->inputFilename = argv[1];
        else
            pm_error("Too many arguments.  When you specify -color, "
                     "the only argument accepted is the optional input "
                     "file name.");
    } else {
        if (argc-1 < 1)
            pm_error("You must specify the -color option.");
        else {
            cmdlineP->colorCt = 1;
            cmdlineP->maskColor[0].matchType = MATCH_EXACT;
            cmdlineP->maskColor[0].u.color = pnm_parsecolorn(argv[1]);

            if (argc - 1 < 2)
                cmdlineP->inputFilename = "-";  /* he wants stdin */
            else if (argc-1 == 2)
                cmdlineP->inputFilename = argv[2];
            else 
                pm_error("Too many arguments.  The only arguments accepted "
                         "are the mask color and optional input file name");
        }
    }
}



static void
setupOutput(FILE *       const fileP,
            unsigned int const width,
            unsigned int const height,
            struct pam * const outPamP) {

    outPamP->size             = sizeof(*outPamP);
    outPamP->len              = PAM_STRUCT_SIZE(tuple_type);
    outPamP->file             = fileP;
    outPamP->format           = RPBM_FORMAT;
    outPamP->plainformat      = 0;
    outPamP->height           = height;
    outPamP->width            = width;
    outPamP->depth            = 1;
    outPamP->maxval           = 1;
    outPamP->bytes_per_sample = 1;
    strcpy(outPamP->tuple_type, PAM_PBM_TUPLETYPE);
}



static bool
isBkColor(tuple        const comparator,
          struct pam * const pamP,
          bk_color     const comparand) {

    pixel comparatorPixel;
    bk_color comparatorBk;

    /* TODO: keep a cache of the bk color for each color in
       a colorhash_table.
    */
    
    assert(pamP->depth >= 3);

    PPM_ASSIGN(comparatorPixel,
               comparator[PAM_RED_PLANE],
               comparator[PAM_GRN_PLANE],
               comparator[PAM_BLU_PLANE]);

    comparatorBk = ppm_bk_color_from_color(comparatorPixel, pamP->maxval);

    return comparatorBk == comparand;
}



static bool
colorIsInSet(tuple              const color,
             struct pam *       const pamP,
             struct CmdlineInfo const cmdline) {

    bool isInSet;
    unsigned int i;
    tuple maskColorUnnorm;

    maskColorUnnorm = pnm_allocpamtuple(pamP);

    for (i = 0, isInSet = FALSE; i < cmdline.colorCt && !isInSet; ++i) {

        assert(i < ARRAY_SIZE(cmdline.maskColor));

        switch(cmdline.maskColor[i].matchType) {
        case MATCH_EXACT:
            pnm_unnormalizetuple(pamP,
                                 cmdline.maskColor[i].u.color,
                                 maskColorUnnorm);
            if (pnm_tupleequal(pamP, color, maskColorUnnorm))
                isInSet = TRUE;
            break;
        case MATCH_BK:
            if (isBkColor(color, pamP, cmdline.maskColor[i].u.bkColor))
                isInSet = TRUE;
            break;
        }
    }

    free(maskColorUnnorm);

    return isInSet;
}



int
main(int argc, const char *argv[]) {

    struct CmdlineInfo cmdline;

    FILE * ifP;
    struct pam inPam;
    struct pam outPam;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFilename);

    pnm_readpaminit(ifP, &inPam, PAM_STRUCT_SIZE(allocation_depth));

    pnm_setminallocationdepth(&inPam, 3);

    setupOutput(stdout, inPam.width, inPam.height, &outPam);

    pnm_writepaminit(&outPam);
    {
        tuple * const inputRow = pnm_allocpamrow(&inPam);
        tuple * const maskRow  = pnm_allocpamrow(&outPam);

        unsigned int numPixelsMasked;

        unsigned int row;

        for (row = 0, numPixelsMasked = 0; row < inPam.height; ++row) {
            unsigned int col;
            pnm_readpamrow(&inPam, inputRow);
            pnm_makerowrgb(&inPam, inputRow);
            for (col = 0; col < inPam.width; ++col) {
                if (colorIsInSet(inputRow[col], &inPam, cmdline)) {
                    maskRow[col][0] = PAM_BLACK;
                    ++numPixelsMasked;
                } else 
                    maskRow[col][0] = PAM_BW_WHITE;
            }
            pnm_writepamrow(&outPam, maskRow);
        }

        if (cmdline.verbose)
            pm_message("%u pixels found matching %u requested colors",
                       numPixelsMasked, cmdline.colorCt);

        pnm_freepamrow(maskRow);
        pnm_freepamrow(inputRow);
    }
    freeCmdline(&cmdline);
    pm_close(ifP);

    return 0;
}



