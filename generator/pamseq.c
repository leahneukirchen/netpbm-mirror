#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "pm_c_util.h"
#include "pam.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "nstring.h"



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    unsigned int depth;
    sample maxval;
    const char * tupletype;
    sample * min;   /* array of size 'depth' */
    sample * max;   /* array of size 'depth' */
    sample * step;  /* array of size 'depth' */
};



static void
destroyCmdline(struct CmdlineInfo * const cmdlineP)  {

    if (cmdlineP->min)
        free(cmdlineP->min);
    if (cmdlineP->max)
        free(cmdlineP->max);
    if (cmdlineP->step)
        free(cmdlineP->step);
}



static unsigned int
entryCt(char ** const stringList) {

    unsigned int i;

    for (i = 0; stringList[i]; ++i) {}

    return i;
}



static void
parseOptList(bool         const isSpec,
             char **      const stringList,
             unsigned int const depth,
             sample       const maxval,
             const char * const optNm,
             sample **    const sampleListP) {

    if (!isSpec)
        *sampleListP = NULL;
    else {
        unsigned int i;
        sample * sampleList;
        const char * memberError;

        if (entryCt(stringList) != depth) {
            pm_error("Wrong number of values for -%s: %u.  Need %u",
                     optNm, entryCt(stringList), depth);
        }

        MALLOCARRAY(sampleList, depth);

        for (i = 0, memberError = NULL; i < depth && !memberError; ++i) {
            char * endPtr;
            long const n = strtol(stringList[i], &endPtr, 10);

            if (strlen(stringList[i]) == 0)
                pm_asprintf(&memberError, "is null string");
            else if (*endPtr != '\0')
                pm_asprintf(&memberError,
                            "contains non-numeric character '%c'",
                            *endPtr);
            else if (n < 0)
                pm_asprintf(&memberError, "is negative");
            else if (n > maxval)
                pm_asprintf(&memberError, "is greater than maxval %lu",
                            maxval);
            else
                sampleList[i] = n;
        }
        if (memberError) {
            free(sampleList);
            pm_errormsg("Value in -%s %s", optNm, memberError);
            pm_longjmp();
        }
        *sampleListP = sampleList;
    }
}



static void
validateMinIsAtMostMax(sample *     const min,
                       sample *     const max,
                       unsigned int const depth) {

    unsigned int plane;

    for (plane = 0; plane < depth; ++plane) {
        if (min[plane] > max[plane])
            pm_error("-min for plane %u (%lu) is greater than -max (%lu)",
                     plane, min[plane], max[plane]);
    }
}



static void
validateStepIsPositive(sample *     const step,
                       unsigned int const depth) {

    unsigned int plane;

    for (plane = 0; plane < depth; ++plane) {
        if (step[plane] <= 0)
            pm_error("-step for plane %u (%lu) is not positive",
                     plane, step[plane]);
    }
}



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
  Convert program invocation arguments (argc,argv) into a format the
  program can use easily, struct cmdlineInfo.  Validate arguments along
  the way and exit program with message if invalid.

  Note that some string information we return as *cmdlineP is in the storage
  argv[] points to.
-----------------------------------------------------------------------------*/
    optEntry *option_def;
    optStruct3 opt;
        /* Instructions to pm_optParseOptions3 on how to parse our options. */

    unsigned int maxSpec;
    char ** max;
    unsigned int minSpec;
    char ** min;
    unsigned int stepSpec;
    char ** step;
    unsigned int tupletypeSpec;
    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "tupletype",  OPT_STRING, &cmdlineP->tupletype,
            &tupletypeSpec,     0);
    OPTENT3(0,   "min",         OPT_STRINGLIST, &min,
            &minSpec,           0);
    OPTENT3(0,   "max",         OPT_STRINGLIST, &max,
            &maxSpec,           0);
    OPTENT3(0,   "step",        OPT_STRINGLIST, &step,
            &stepSpec,          0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!tupletypeSpec)
        cmdlineP->tupletype = "";
    else {
        struct pam pam;
        if (strlen(cmdlineP->tupletype)+1 > sizeof(pam.tuple_type))
            pm_error("The tuple type you specified is too long.  "
                     "Maximum %u characters.",
                     (unsigned)sizeof(pam.tuple_type)-1);
    }

    if (argc-1 < 2)
        pm_error("Need two arguments: depth and maxval.");
    else if (argc-1 > 2)
        pm_error("Only two argumeents allowed: depth and maxval.  "
                 "You specified %d", argc-1);
    else {
        const char * error;
        unsigned int depth, maxval;

        pm_string_to_uint(argv[1], &depth, &error);
        if (error) {
            pm_error("'%s' is invalid as an image depth.  %s", argv[1], error);
            pm_strfree(error);
        }
        else if (depth <= 0)
            pm_error("depth argument must be a positive number.  You "
                     "specified '%s'", argv[1]);
        else
            cmdlineP->depth = depth;

        maxval = pm_parse_maxval(argv[2]);

        if (maxval > PAM_OVERALL_MAXVAL)
            pm_error("The maxval you specified (%u) is too big.  "
                     "Maximum is %lu", maxval, PAM_OVERALL_MAXVAL);
        else
            cmdlineP->maxval = maxval;


        if (pm_maxvaltobits(cmdlineP->maxval) +
            pm_maxvaltobits(cmdlineP->depth-1) > sizeof(unsigned int)*8)
            pm_error("The maxval (%u) and depth (%u) you specified result "
                     "in a larger number of tuples than this program can "
                     "handle (roughly %u)",
                     (unsigned int) cmdlineP->maxval, cmdlineP->depth,
                     (unsigned int) -1);
    }
    parseOptList(minSpec, min,  cmdlineP->depth, cmdlineP->maxval, "min",
                 &cmdlineP->min);
    parseOptList(maxSpec, max,  cmdlineP->depth, cmdlineP->maxval, "max",
                 &cmdlineP->max);
    parseOptList(stepSpec, step, cmdlineP->depth, cmdlineP->maxval, "step",
                 &cmdlineP->step);

    if (cmdlineP->min && cmdlineP->max)
        validateMinIsAtMostMax(cmdlineP->min, cmdlineP->max, cmdlineP->depth);

    if (cmdlineP->step)
        validateStepIsPositive(cmdlineP->step, cmdlineP->depth);

    if (minSpec)
        free(min);
    if (maxSpec)
        free(max);
    if (stepSpec)
        free(step);
}



static void
computeMinMaxStep(unsigned int   const depth,
                  sample         const maxval,
                  const sample * const min,
                  const sample * const max,
                  const sample * const step,
                  sample **      const minP,
                  sample **      const maxP,
                  sample **      const stepP) {

    unsigned int plane;

    MALLOCARRAY(*minP,  depth);
    MALLOCARRAY(*maxP,  depth);
    MALLOCARRAY(*stepP, depth);

    for (plane = 0; plane < depth; ++plane) {
        (*minP)[plane]  = min  ? min[plane]  : 0;
        (*maxP)[plane]  = max  ? max[plane]  : maxval;
        (*stepP)[plane] = step ? step[plane] : 1;
    }
}



static int
imageWidth(unsigned int   const depth,
           const sample * const min,
           const sample * const max,
           const sample * const step) {
/*----------------------------------------------------------------------------
   The width of the output image (i.e. the number of pixels in the image),
   given that the minimum and maximum sample values in Plane P are min[P] and
   max[P] and the samples step by step[P].

   E.g. in the example case of min 0, max 4, and step 1 everywhere, with
   depth 2,  We return 5*5 = 25.
-----------------------------------------------------------------------------*/
    unsigned int product;
    unsigned int plane;

    for (plane = 0, product=1; plane < depth; ++plane) {
        assert(max[plane] >= min[plane]);

        unsigned int const valueCtThisPlane =
            ROUNDUP((max[plane] - min[plane] + 1), step[plane])/step[plane];

        if (INT_MAX / valueCtThisPlane < product)
            pm_error("Uncomputably large number of pixels (greater than %u)",
                     INT_MAX);

        product *= valueCtThisPlane;
    }
    assert(product < INT_MAX);

    return product;
}



static void
permuteHigherPlanes(unsigned int   const depth,
                    const sample * const min,
                    const sample * const max,
                    const sample * const step,
                    unsigned int   const nextPlane,
                    tuple *        const tuplerow,
                    int *          const colP,
                    tuple          const lowerPlanes) {
/*----------------------------------------------------------------------------
   Create all the possible permutations of tuples whose lower-numbered planes
   contain the values from 'lowerPlanes'.  I.e. vary the higher-numbered
   planes according to min[], max[], and step[].

   Write them sequentially into *tuplerow, starting at *colP.  Adjust
   *colP to next the column after the ones we write.

   lower-numbered means with plane numbers less than 'nextPlane'.

   We modify 'lowerPlanes' in the higher planes to undefined values.
-----------------------------------------------------------------------------*/
    if (nextPlane == depth - 1) {
        /* lowerPlanes[] contains values for all the planes except the
           highest, so we just vary the highest plane and combine that
           with lowerPlanes[] and output that to tuplerow[].
        */
        sample value;

        for (value = min[nextPlane];
             value <= max[nextPlane];
             value += step[nextPlane]) {

            unsigned int plane;

            for (plane = 0; plane < nextPlane; ++plane)
                tuplerow[*colP][plane] = lowerPlanes[plane];

            tuplerow[*colP][nextPlane] = value;

            ++(*colP);
        }
    } else {
        sample value;

        for (value = min[nextPlane];
             value <= max[nextPlane];
             value += step[nextPlane]) {
            /* We do something sleazy here and use Caller's lowerPlanes[]
               variable as a local variable, modifying it in the higher
               plane positions.  That's just for speed.
            */
            lowerPlanes[nextPlane] = value;

            permuteHigherPlanes(depth, min, max, step,
                                nextPlane+1, tuplerow, colP, lowerPlanes);
        }
    }
}



int
main(int argc, const char **argv) {

    struct CmdlineInfo cmdline;
    struct pam pam;
    int col;
    tuple lowerPlanes;
        /* This is working storage passed to permuteHigherPlanes(),
           which we call.  Note that because we always pass zero as the
           "planes" argument to permuteHigherPlanes(), none of the
           "lower planes" value is defined as an input to
           permuteHigherPlanes().
        */
    tuple * tuplerow;
    sample * min;   /* malloc'ed array */
    sample * max;   /* malloc'ed array */
    sample * step;  /* malloc'ed array */

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    computeMinMaxStep(cmdline.depth, cmdline.maxval,
                      cmdline.min, cmdline.max, cmdline.step,
                      &min, &max, &step);

    pam.size = sizeof(pam);
    pam.len = PAM_STRUCT_SIZE(tuple_type);
    pam.file = stdout;
    pam.format = PAM_FORMAT;
    pam.plainformat = 0;
    pam.width = imageWidth(cmdline.depth, min, max, step);
    pam.height = 1;
    pam.depth = cmdline.depth;
    pam.maxval = cmdline.maxval;
    strcpy(pam.tuple_type, cmdline.tupletype);

    pnm_writepaminit(&pam);

    tuplerow = pnm_allocpamrow(&pam);

    lowerPlanes = pnm_allocpamtuple(&pam);

    col = 0;

    permuteHigherPlanes(pam.depth, min, max, step,
                        0, tuplerow, &col, lowerPlanes);

    if (col != pam.width)
        pm_error("INTERNAL ERROR: Wrote %d columns; should have written %d.",
                 col, pam.width);

    pnm_writepamrow(&pam, tuplerow);

    pnm_freepamrow(tuplerow);

    destroyCmdline(&cmdline);

    return 0;
}



