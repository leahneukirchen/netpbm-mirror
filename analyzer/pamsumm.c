/*=============================================================================
                               pamsumm
===============================================================================
  Summarize all the samples of a PAM image with various functions.

  By Bryan Henderson, San Jose CA 2004.02.07.

  Contributed to the public domain
=============================================================================*/
#include "pm_c_util.h"
#include "pam.h"
#include "shhopt.h"
#include "mallocvar.h"

enum Function {FN_ADD, FN_MEAN, FN_MIN, FN_MAX};

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char *  inputFileName;  /* Name of input file */
    enum Function function;
    unsigned int  normalize;
    unsigned int  brief;
    unsigned int  verbose;
};



static void
parseCommandLine(int argc, const char ** const argv,
                 struct CmdlineInfo * const cmdlineP) {

    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int sumSpec, meanSpec, minSpec, maxSpec;

    MALLOCARRAY(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "sum",       OPT_FLAG,  NULL, &sumSpec,             0);
    OPTENT3(0,   "mean",      OPT_FLAG,  NULL, &meanSpec,            0);
    OPTENT3(0,   "min",       OPT_FLAG,  NULL, &minSpec,             0);
    OPTENT3(0,   "max",       OPT_FLAG,  NULL, &maxSpec,             0);
    OPTENT3(0,   "normalize", OPT_FLAG,  NULL, &cmdlineP->normalize, 0);
    OPTENT3(0,   "brief",     OPT_FLAG,  NULL, &cmdlineP->brief,     0);
    OPTENT3(0,   "verbose",   OPT_FLAG,  NULL, &cmdlineP->verbose,   0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (sumSpec + minSpec + maxSpec > 1)
        pm_error("You may specify at most one of -sum, -min, and -max");

    if (sumSpec) {
        cmdlineP->function = FN_ADD;
    } else if (meanSpec) {
        cmdlineP->function = FN_MEAN;
    } else if (minSpec) {
        cmdlineP->function = FN_MIN;
    } else if (maxSpec) {
        cmdlineP->function = FN_MAX;
    } else
        pm_error("You must specify one of -sum, -min, -max, or -mean");

    if (argc-1 > 1)
        pm_error("Too many arguments (%d).  File name is the only argument.",
                 argc-1);

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else
        cmdlineP->inputFileName = argv[1];

    free(option_def);
}



struct Accum {
    union {
        double sum;
        unsigned int min;
        unsigned int max;
    } u;
};



static void
initAccumulator(struct Accum * const accumulatorP,
                enum Function  const function) {

    switch(function) {
    case FN_ADD:  accumulatorP->u.sum = 0.0;      break;
    case FN_MEAN: accumulatorP->u.sum = 0.0;      break;
    case FN_MIN:  accumulatorP->u.min = UINT_MAX; break;
    case FN_MAX:  accumulatorP->u.max = 0;        break;
    }
}



static void
aggregate(struct pam *   const inpamP,
          tuple *        const tupleRow,
          enum Function  const function,
          struct Accum * const accumulatorP) {

    unsigned int col;

    for (col = 0; col < inpamP->width; ++col) {
        unsigned int plane;
        for (plane = 0; plane < inpamP->depth; ++plane) {
            switch(function) {
            case FN_ADD:
            case FN_MEAN:
                accumulatorP->u.sum += tupleRow[col][plane];
            break;
            case FN_MIN:
                if (tupleRow[col][plane] < accumulatorP->u.min)
                    accumulatorP->u.min = tupleRow[col][plane];
                break;
            case FN_MAX:
                if (tupleRow[col][plane] > accumulatorP->u.min)
                    accumulatorP->u.min = tupleRow[col][plane];
                break;
            }
        }
    }
}



static void
printSummary(struct Accum  const accumulator,
             unsigned int  const scale,
             unsigned int  const count,
             enum Function const function,
             bool          const mustNormalize,
             bool          const brief) {

    switch (function) {
    case FN_ADD: {
        const char * const intro = brief ? "" : "the sum of all samples is ";

        if (mustNormalize)
            printf("%s%f\n", intro, accumulator.u.sum/scale);
        else
            printf("%s%u\n", intro, (unsigned int)accumulator.u.sum);
    }
    break;
    case FN_MEAN: {
        const char * const intro = brief ? "" : "the mean of all samples is ";

        if (mustNormalize)
            printf("%s%f\n", intro, accumulator.u.sum/count/scale);
        else
            printf("%s%f\n", intro, accumulator.u.sum/count);
    }
    break;
    case FN_MIN: {
        const char * const intro =
            brief ? "" : "the minimum of all samples is ";

        if (mustNormalize)
            printf("%s%f\n", intro, (double)accumulator.u.min/scale);
        else
            printf("%s%u\n", intro, accumulator.u.min);
    }
    break;
    case FN_MAX: {
        const char * const intro =
            brief ? "" : "the maximum of all samples is ";

        if (mustNormalize)
            printf("%s%f\n", intro, (double)accumulator.u.max/scale);
        else
            printf("%s%u\n", intro, accumulator.u.max);
    }
    break;
    }
}



int
main(int argc, const char *argv[]) {

    FILE * ifP;
    tuple * inputRow;   /* Row from input image */
    unsigned int row;
    struct CmdlineInfo cmdline;
    struct pam inpam;   /* Input PAM image */
    struct Accum accumulator;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    inputRow = pnm_allocpamrow(&inpam);

    initAccumulator(&accumulator, cmdline.function);

    for (row = 0; row < inpam.height; ++row) {
        pnm_readpamrow(&inpam, inputRow);

        aggregate(&inpam, inputRow, cmdline.function, &accumulator);
    }
    printSummary(accumulator, (unsigned)inpam.maxval,
                 inpam.height * inpam.width * inpam.depth,
                 cmdline.function, cmdline.normalize, cmdline.brief);

    pnm_freepamrow(inputRow);
    pm_close(inpam.file);

    return 0;
}
