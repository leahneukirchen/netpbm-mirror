#include <assert.h>
#include <string.h>
#include <limits.h>
#include <math.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pam.h"

static double const EPSILON = 1.0e-5;

enum Function {FN_ADD, FN_SUBTRACT, FN_MULTIPLY, FN_DIVIDE, FN_DIFFERENCE,
               FN_MINIMUM, FN_MAXIMUM, FN_MEAN, FN_EQUAL, FN_COMPARE,
               FN_AND, FN_OR, FN_NAND, FN_NOR, FN_XOR,
               FN_SHIFTLEFT, FN_SHIFTRIGHT
              };



static bool
isDyadic(enum Function const function) {

    bool retval;

    switch (function) {
    case FN_ADD:
    case FN_MULTIPLY:
    case FN_MINIMUM:
    case FN_MAXIMUM:
    case FN_MEAN:
    case FN_EQUAL:
    case FN_AND:
    case FN_NAND:
    case FN_OR:
    case FN_NOR:
    case FN_XOR:
        retval = FALSE;
        break;
    case FN_SUBTRACT:
    case FN_DIFFERENCE:
    case FN_COMPARE:
    case FN_DIVIDE:
    case FN_SHIFTLEFT:
    case FN_SHIFTRIGHT:
        retval = TRUE;
        break;
    }
    return retval;
}



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    enum Function function;
    unsigned int operandCt;
    const char ** operandFileNames;
    double closeness;
};



static void
parseCommandLine(int argc, const char ** const argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions2 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int addSpec, subtractSpec, multiplySpec, divideSpec,
        differenceSpec,
        minimumSpec, maximumSpec, meanSpec, equalSpec, compareSpec,
        andSpec, orSpec, nandSpec, norSpec, xorSpec,
        shiftleftSpec, shiftrightSpec, closenessSpec;

    float closeness;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "add",         OPT_FLAG,   NULL, &addSpec,        0);
    OPTENT3(0, "subtract",    OPT_FLAG,   NULL, &subtractSpec,   0);
    OPTENT3(0, "multiply",    OPT_FLAG,   NULL, &multiplySpec,   0);
    OPTENT3(0, "divide",      OPT_FLAG,   NULL, &divideSpec,     0);
    OPTENT3(0, "difference",  OPT_FLAG,   NULL, &differenceSpec, 0);
    OPTENT3(0, "minimum",     OPT_FLAG,   NULL, &minimumSpec,    0);
    OPTENT3(0, "maximum",     OPT_FLAG,   NULL, &maximumSpec,    0);
    OPTENT3(0, "mean",        OPT_FLAG,   NULL, &meanSpec,       0);
    OPTENT3(0, "equal",       OPT_FLAG,   NULL, &equalSpec,      0);
    OPTENT3(0, "compare",     OPT_FLAG,   NULL, &compareSpec,    0);
    OPTENT3(0, "and",         OPT_FLAG,   NULL, &andSpec,        0);
    OPTENT3(0, "or",          OPT_FLAG,   NULL, &orSpec,         0);
    OPTENT3(0, "nand",        OPT_FLAG,   NULL, &nandSpec,       0);
    OPTENT3(0, "nor",         OPT_FLAG,   NULL, &norSpec,        0);
    OPTENT3(0, "xor",         OPT_FLAG,   NULL, &xorSpec,        0);
    OPTENT3(0, "shiftleft",   OPT_FLAG,   NULL, &shiftleftSpec,  0);
    OPTENT3(0, "shiftright",  OPT_FLAG,   NULL, &shiftrightSpec, 0);
    OPTENT3(0, "closeness",   OPT_FLOAT,  &closeness, &closenessSpec, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (addSpec + subtractSpec + multiplySpec + divideSpec + differenceSpec +
        minimumSpec + maximumSpec + meanSpec + equalSpec + compareSpec +
        andSpec + orSpec + nandSpec + norSpec + xorSpec +
        shiftleftSpec + shiftrightSpec > 1)
        pm_error("You may specify only one function");

    if (addSpec)
        cmdlineP->function = FN_ADD;
    else if (subtractSpec)
        cmdlineP->function = FN_SUBTRACT;
    else if (multiplySpec)
        cmdlineP->function = FN_MULTIPLY;
    else if (divideSpec)
        cmdlineP->function = FN_DIVIDE;
    else if (differenceSpec)
        cmdlineP->function = FN_DIFFERENCE;
    else if (minimumSpec)
        cmdlineP->function = FN_MINIMUM;
    else if (maximumSpec)
        cmdlineP->function = FN_MAXIMUM;
    else if (meanSpec)
        cmdlineP->function = FN_MEAN;
    else if (equalSpec)
        cmdlineP->function = FN_EQUAL;
    else if (compareSpec)
        cmdlineP->function = FN_COMPARE;
    else if (andSpec)
        cmdlineP->function = FN_AND;
    else if (orSpec)
        cmdlineP->function = FN_OR;
    else if (nandSpec)
        cmdlineP->function = FN_NAND;
    else if (norSpec)
        cmdlineP->function = FN_NOR;
    else if (xorSpec)
        cmdlineP->function = FN_XOR;
    else if (shiftleftSpec)
        cmdlineP->function = FN_SHIFTLEFT;
    else if (shiftrightSpec)
        cmdlineP->function = FN_SHIFTRIGHT;
    else
        pm_error("You must specify a function (e.g. '-add')");

    if (closenessSpec) {
        if (cmdlineP->function != FN_EQUAL)
            pm_error("-closeness is valid only with -equal");
        else {
            if (closeness < 0 || closeness > 100)
                pm_error("-closeness value %f is not a valid percentage",
                         closeness);
            cmdlineP->closeness = (double)closeness/100;
        }
    } else
        cmdlineP->closeness = EPSILON;

    if (argc-1 < 2)
        pm_error("You must specify at least two arguments: the files which "
                 "are the operands of the function.  You specified %u",
                 argc-1);
    else {
        if (isDyadic(cmdlineP->function) && argc-1 > 2)
            pm_error("You specified %u arguments, but a dyadic function.  "
                     "For a dyadic function, you must specify 2 arguments:  "
                     "the operands of the function", argc-1);
        else {
            cmdlineP->operandCt = argc-1;
            cmdlineP->operandFileNames = &argv[1];
        }
    }
}



enum category {
    CATEGORY_FRACTIONAL_ARITH,
        /* Arithmetic in which each sample represents a the fraction
           sample/maxval.
        */
    CATEGORY_BITSTRING,
        /* And, Or, etc.  Maxval isn't a scale factor at all; it's a mask. */
    CATEGORY_SHIFT
        /* Left argument is a bit string, but right argument is a whole
           number (left maxval is a mask; right maxval is meaningless).
        */
};



static enum category
functionCategory(enum Function const function) {

    enum category retval;

    switch (function) {
    case FN_ADD:
    case FN_SUBTRACT:
    case FN_DIFFERENCE:
    case FN_MINIMUM:
    case FN_MAXIMUM:
    case FN_MEAN:
    case FN_EQUAL:
    case FN_COMPARE:
    case FN_MULTIPLY:
    case FN_DIVIDE:
        retval = CATEGORY_FRACTIONAL_ARITH;
        break;
    case FN_AND:
    case FN_OR:
    case FN_NAND:
    case FN_NOR:
    case FN_XOR:
        retval = CATEGORY_BITSTRING;
        break;
    case FN_SHIFTLEFT:
    case FN_SHIFTRIGHT:
        retval = CATEGORY_SHIFT;
        break;
    }
    return retval;
}



static int
outFmtForCompare(int const format1,
                 int const format2) {
/*----------------------------------------------------------------------------
   The format for the output image, given that the function is Compare and
   'format1' and 'format2' are the input image formats.

   Note that Compare is special because its maxval is always 2, and that
   won't work for PBM.
-----------------------------------------------------------------------------*/
    int const tentativeFormat = MAX(format1, format2);

    int retval;

    switch (tentativeFormat) {
    case PBM_FORMAT:  retval = PGM_FORMAT;      break;
    case RPBM_FORMAT: retval = RPGM_FORMAT;     break;
    default:          retval = tentativeFormat; break;
    }
    return retval;
}



static unsigned int
maxDepth(const struct pam * const pam,
         unsigned int       const pamCt) {

    unsigned int maxDepthSoFar;
    unsigned int i;

    assert(pamCt >= 1);

    maxDepthSoFar = pam[0].depth;
    for (i = 1; i < pamCt; ++i) {
        if (pam[i].depth > maxDepthSoFar)
            maxDepthSoFar = pam[i].depth;
    }
    return maxDepthSoFar;
}



static int
maxFormat(const struct pam * const pam,
          unsigned int       const pamCt) {

    int maxFormatSoFar;
    unsigned int i;

    assert(pamCt >= 1);

    maxFormatSoFar = pam[0].format;
    for (i = 1; i < pamCt; ++i) {
        if (pam[i].format > maxFormatSoFar)
            maxFormatSoFar = pam[i].format;
    }
    return maxFormatSoFar;
}



static sample
maxMaxval(const struct pam * const pam,
          unsigned int       const pamCt) {

    sample maxMaxvalSoFar;
    unsigned int i;

    assert(pamCt >= 1);

    maxMaxvalSoFar = pam[0].maxval;
    for (i = 1; i < pamCt; ++i) {
        if (pam[i].maxval > maxMaxvalSoFar)
            maxMaxvalSoFar = pam[i].maxval;
    }
    return maxMaxvalSoFar;
}



static bool
maxvalsAreEqual(const struct pam * const pam,
                unsigned int       const pamCt) {

    bool equalSoFar;
    unsigned int i;

    assert(pamCt >= 1);

    equalSoFar = true;

    for (i = 1; i < pamCt; ++i) {
        if (pam[i].maxval != pam[0].maxval)
            equalSoFar = false;
    }
    return equalSoFar;
}



static void
computeOutputType(struct pam *       const outpamP,
                  const struct pam * const inpam,   /* array */
                  unsigned int       const operandCt,
                  enum Function      const function) {

    assert(operandCt >= 1);

    outpamP->size        = sizeof(struct pam);
    outpamP->len         = PAM_STRUCT_SIZE(tuple_type);
    outpamP->file        = stdout;
    outpamP->plainformat = FALSE;
    outpamP->height      = inpam[0].height;
    outpamP->width       = inpam[0].width;
    outpamP->depth       = maxDepth(inpam, operandCt);


    if (function == FN_COMPARE) {
        assert(operandCt == 2);
        outpamP->format = outFmtForCompare(inpam[0].format, inpam[1].format);
    } else
        outpamP->format = maxFormat(inpam, operandCt);

    switch (functionCategory(function)) {
    case CATEGORY_FRACTIONAL_ARITH:
        if (function == FN_COMPARE)
            outpamP->maxval = 2;
        else if (function == FN_EQUAL)
            outpamP->maxval = 1;
        else
            outpamP->maxval = maxMaxval(inpam, operandCt);
        break;
    case CATEGORY_BITSTRING:
        if (maxvalsAreEqual(inpam, operandCt))
            pm_error("For a bit string operation, the maxvals of the "
                     "operand images must be the same.  Yours differ");

        if (pm_bitstomaxval(pm_maxvaltobits(inpam[0].maxval)) !=
            inpam[0].maxval)
            pm_error("For a bit string operation, the maxvals of the inputs "
                     "must be a full binary count, i.e. a power of two "
                     "minus one such as 0xff.  You have 0x%x",
                     (unsigned)inpam[0].maxval);

        outpamP->maxval = inpam[0].maxval;
        break;
    case CATEGORY_SHIFT:
        if (pm_bitstomaxval(pm_maxvaltobits(inpam[0].maxval)) !=
            inpam[0].maxval)
            pm_error("For a bit shift operation, the maxval of the left "
                     "input image "
                     "must be a full binary count, i.e. a power of two "
                     "minus one such as 0xff.  You have 0x%x",
                     (unsigned)inpam[0].maxval);
        outpamP->maxval = inpam[0].maxval;
    }
    outpamP->bytes_per_sample = (pm_maxvaltobits(outpamP->maxval)+7)/8;

    if (outpamP->maxval > 1 &&
        strneq(inpam[0].tuple_type, "BLACKANDWHITE", 13)) {

        strcpy(outpamP->tuple_type, "");
    } else
        strcpy(outpamP->tuple_type, inpam[0].tuple_type);
}



static samplen
samplenSum(samplen      const operands[],
           unsigned int const operandCt) {

    unsigned int i;
    samplen total;

    for (i = 1, total = operands[0]; i < operandCt; ++i) {
        total += operands[1];
        if (total > 1.)
            total = 1.;
    }
    return total;
}



static samplen
samplenProduct(samplen      const operands[],
               unsigned int const operandCt) {

    unsigned int i;
    double product;

    for (i = 1, product = operands[0]; i < operandCt; ++i)
        product *= operands[i];

    return product;
}



static samplen
samplenMin(samplen      const operands[],
           unsigned int const operandCt) {

    unsigned int i;
    samplen min;

    for (i = 1, min = operands[0]; i < operandCt; ++i) {
        if (operands[i] < min)
            min = operands[i];
    }
    return min;
}



static samplen
samplenMax(samplen      const operands[],
           unsigned int const operandCt) {

    unsigned int i;
    samplen max;

    for (i = 1, max = operands[0]; i < operandCt; ++i) {
        if (operands[i] > max)
            max = operands[i];
    }
    return max;
}



static samplen
samplenMean(samplen      const operands[],
            unsigned int const operandCt) {

    unsigned int i;
    double sum;

    for (i = 0, sum = 0.; i < operandCt; ++i)
        sum += operands[i];

    return sum / operandCt;
}



static samplen
samplenEqual(samplen      const operands[],
             unsigned int const operandCt,
             double       const closeness) {

    unsigned int i;
    bool allEqual;

    for (i = 1, allEqual = true; i < operandCt; ++i) {
        if (fabs(operands[i]- operands[0]) > closeness)
            allEqual = false;
    }

    return allEqual ? 1.0 : 0.0;
}



static samplen
applyNormalizedFunction(enum Function const function,
                        samplen       const operands[],
                        unsigned int  const operandCt,
                        double        const closeness) {

    samplen result;

    switch (function) {
    case FN_ADD:
        result = samplenSum(operands, operandCt);
        break;
    case FN_SUBTRACT:
        result = MAX(0., operands[0] - operands[1]);
        break;
    case FN_MULTIPLY:
        result = samplenProduct(operands, operandCt);
        break;
    case FN_DIVIDE:
        result = (operands[1] > operands[0]) ?
        operands[0] / operands[1] : 1.;
        break;
    case FN_DIFFERENCE:
        result = operands[0] > operands[1] ?
            operands[0] - operands[1] : operands[1] - operands[0];
        break;
    case FN_MINIMUM:
        result = samplenMin(operands, operandCt);
        break;
    case FN_MAXIMUM:
        result = samplenMax(operands, operandCt);
        break;
    case FN_MEAN:
        result = samplenMean(operands, operandCt);
        break;
    case FN_EQUAL:
        result = samplenEqual(operands, operandCt, closeness);
        break;
    case FN_COMPARE:
        result =
            operands[0] > operands[1] ?
            1. : operands[0] < operands[1] ?
            0. : .5;
        break;
    default:
        pm_error("Internal error.  applyNormalizedFunction() called "
                 "for a function it doesn't know how to do: %u", function);
    }

    return result;
}



static void
doNormalizedArith(const struct pam * const inpam,  /* array */
                  unsigned int       const operandCt,
                  const struct pam * const outpamP,
                  enum Function      const function,
                  double             const closeness) {

    tuplen ** tuplerown;
        /* tuplerown[0] is the current row in the first operand image */

    tuplen * tuplerownOut;
    unsigned int row;
    samplen * operands;
        /* operand[0] is the first operand in the current one-sample
           computation
        */
    unsigned int * plane;
        /* plane[0] is the plane number in the first operand image for
           the current one-sample computation.  plane[1] is the plane number
           in the second operand image, etc.
         */
    unsigned int i;

    MALLOCARRAY_NOFAIL(operands, operandCt);
    MALLOCARRAY_NOFAIL(plane, operandCt);
    MALLOCARRAY_NOFAIL(tuplerown, operandCt);

    for (i = 0; i < operandCt; ++i)
        tuplerown[i] = pnm_allocpamrown(&inpam[i]);
    tuplerownOut = pnm_allocpamrown(outpamP);

    for (row = 0; row < outpamP->height; ++row) {
        unsigned int i;
        unsigned int col;

        for (i = 0; i < operandCt; ++i)
            pnm_readpamrown(&inpam[i], tuplerown[i]);

        for (col = 0; col < outpamP->width; ++col) {
            unsigned int outplane;

            for (outplane = 0; outplane < outpamP->depth; ++outplane) {
                unsigned int i;
                unsigned int op;

                for (i = 0; i < operandCt; ++i)
                    plane[i] = MIN(outplane, inpam[i].depth-1);

                for (op = 0; op < operandCt; ++op)
                    operands[op] = tuplerown[op][col][plane[op]];

                tuplerownOut[col][outplane] =
                    applyNormalizedFunction(function, operands, operandCt,
                                            closeness);
                assert(tuplerownOut[col][outplane] >= 0.);
                assert(tuplerownOut[col][outplane] <= 1.);
            }
        }
        pnm_writepamrown(outpamP, tuplerownOut);
    }

    for (i = 0; i < operandCt; ++i)
        pnm_freepamrown(tuplerown[i]);
    free(tuplerown);
    pnm_freepamrown(tuplerownOut);
    free(plane);
    free(operands);
}



static sample
sampleSum(sample       const operands[],
          unsigned int const operandCt,
          sample       const maxval) {

    unsigned int i;
    sample total;

    for (i = 1, total = operands[0]; i < operandCt; ++i) {
        total += operands[i];
        if (total > maxval)
            total = maxval;
    }
    return total;
}



static sample
sampleProduct(sample       const operands[],
              unsigned int const operandCt,
              sample       const maxval) {

    unsigned int i;
    double product;

    for (i = 0, product = 1.0; i < operandCt; ++i) {
        product *= ((double)operands[i]/maxval);
    }
    return (sample)(product * maxval + 0.5);
}



static sample
sampleMin(sample       const operands[],
          unsigned int const operandCt) {

    unsigned int i;
    sample min;

    for (i = 1, min = operands[0]; i < operandCt; ++i) {
        if (operands[i] < min)
            min = operands[i];
    }
    return min;
}



static sample
sampleMax(sample       const operands[],
          unsigned int const operandCt) {

    unsigned int i;
    sample max;

    for (i = 1, max = operands[0]; i < operandCt; ++i) {
        if (operands[i] > max)
            max = operands[i];
    }
    return max;
}



static sample
sampleMean(sample       const operands[],
           unsigned int const operandCt) {

    unsigned int i;
    unsigned int sum;

    for (i = 0, sum = 0; i < operandCt; ++i) {
        sum += operands[i];
        if (UINT_MAX - operands[i] < sum)
            pm_error("Arithmetic overflow adding samples for mean");
    }
    return ROUNDDIV(sum, operandCt);
}



static sample
sampleEqual(sample       const operands[],
            unsigned int const operandCt,
            sample       const maxval) {

    unsigned int i;
    bool allEqual;

    for (i = 1, allEqual = true; i < operandCt; ++i) {
        if (operands[i] != operands[0])
            allEqual = false;
    }
    return allEqual ? maxval : 0;
}



static sample
sampleAnd(sample       const operands[],
          unsigned int const operandCt) {

    unsigned int i;
    sample accum;

    for (i = 1, accum = operands[0]; i < operandCt; ++i) {
        accum &= operands[i];
    }
    return accum;
}



static sample
sampleOr(sample       const operands[],
         unsigned int const operandCt) {

    unsigned int i;
    sample accum;

    for (i = 1, accum = operands[0]; i < operandCt; ++i) {
        accum |= operands[i];
    }
    return accum;
}



static sample
sampleNand(sample       const operands[],
           unsigned int const operandCt,
           sample       const maxval) {

    unsigned int i;
    sample accum;

    for (i = 1, accum = operands[0]; i < operandCt; ++i) {
        accum &= operands[i];
    }
    return ~accum & maxval;
}



static sample
sampleNor(sample       const operands[],
          unsigned int const operandCt,
          sample       const maxval) {

    unsigned int i;
    sample accum;

    for (i = 1, accum = operands[0]; i < operandCt; ++i) {
        accum |= operands[i];
    }
    return ~accum & maxval;
}



static sample
sampleXor(sample       const operands[],
          unsigned int const operandCt) {

    unsigned int i;
    sample accum;

    for (i = 1, accum = operands[0]; i < operandCt; ++i) {
        accum ^= operands[i];
    }
    return accum;
}



static sample
applyUnNormalizedFunction(enum Function const function,
                          sample        const operands[],
                          unsigned int  const operandCt,
                          sample        const maxval) {
/*----------------------------------------------------------------------------
   Apply function 'function' to the arguments operands[] (there are
   'operandCt' of them), assuming both are based on the same maxval
   'maxval'.  Return a value which is also a fraction of 'maxval'.

   Exception: for the shift operations, operands[1] is not based on any
   maxval.  It is an absolute bit count.

   We assume 'operandCount' is sensible for 'function'.  E.g. if
   'function' is FN_DIFFERENCE, 'operandCt' is 2.

   For a function that has a concept of left and right argument,
   operands[0] is the left and operands[1] is the right.
-----------------------------------------------------------------------------*/
    sample result;

    switch (function) {
    case FN_ADD:
        result = sampleSum(operands, operandCt, maxval);
        break;
    case FN_SUBTRACT:
        result = MAX(0, (int)operands[0] - (int)operands[1]);
        break;
    case FN_DIFFERENCE:
        result = operands[0] > operands[1] ?
            operands[0] - operands[1] : operands[1] - operands[0];
        break;
    case FN_MINIMUM:
        result = sampleMin(operands, operandCt);
        break;
    case FN_MAXIMUM:
        result = sampleMax(operands, operandCt);
        break;
    case FN_MEAN:
        result = sampleMean(operands, operandCt);
        break;
    case FN_EQUAL:
        result = sampleEqual(operands, operandCt, maxval);
        break;
    case FN_COMPARE:
        result = operands[0] > operands[1] ?
            2 : operands[0] < operands[1] ? 0 : 1;
        break;
    case FN_MULTIPLY:
        result = sampleProduct(operands, operandCt, maxval);
        break;
    case FN_DIVIDE:
        result = (operands[1] > operands[0]) ?
            ROUNDDIV(operands[0] * maxval, operands[1]) : maxval;
        break;

    case FN_AND:
        result = sampleAnd(operands, operandCt);
        break;
    case FN_OR:
        result = sampleOr(operands, operandCt);
        break;
    case FN_NAND:
        result = sampleNand(operands, operandCt, maxval);
        break;
    case FN_NOR:
        result = sampleNor(operands, operandCt, maxval);
        break;
    case FN_XOR:
        result = sampleXor(operands, operandCt);
        break;
    case FN_SHIFTLEFT:
        result = (operands[0] << operands[1]) & maxval;
        break;
    case FN_SHIFTRIGHT:
        result = operands[0] >> operands[1];
        break;
    default:
        pm_error("Internal error.  applyUnNormalizedFunction() called "
                 "for a function it doesn't know how to do: %u", function);
    }

    return result;
}



static void
doUnNormalizedArith(const struct pam * const inpam,  /* array */
                    unsigned int       const operandCt,
                    const struct pam * const outpamP,
                    enum Function      const function) {
/*----------------------------------------------------------------------------
   Take advantage of the fact that both inputs and the output use the same
   maxval to do the computation without time-consuming normalization of
   sample values.
-----------------------------------------------------------------------------*/
    sample const maxval = outpamP->maxval;

    tuple ** tuplerow;
        /* tuplerow[0] is the current row in the first operand image */

    tuple * tuplerowOut;
    unsigned int row;
    sample * operands;
        /* operand[0] is the first operand in the current one-sample
           computation
        */
    unsigned int * plane;
        /* plane[0] is the plane number in the first operand image for
           the current one-sample computation.  plane[1] is the plane number
           in the second operand image, etc.
         */
    unsigned int i;

    MALLOCARRAY_NOFAIL(operands, operandCt);
    MALLOCARRAY_NOFAIL(plane, operandCt);
    MALLOCARRAY_NOFAIL(tuplerow, operandCt);

    for (i = 0; i < operandCt; ++i)
        tuplerow[i]   = pnm_allocpamrow(&inpam[i]);

    tuplerowOut = pnm_allocpamrow(outpamP);

    for (row = 0; row < outpamP->height; ++row) {
        unsigned int i;
        unsigned int col;

        for (i = 0; i < operandCt; ++i)
            pnm_readpamrow(&inpam[i], tuplerow[i]);

        for (col = 0; col < outpamP->width; ++col) {
            unsigned int outplane;

            for (outplane = 0; outplane < outpamP->depth; ++outplane) {
                unsigned int op;
                unsigned int i;

                for (i = 0; i < operandCt; ++i)
                    plane[i] = MIN(outplane, inpam[i].depth-1);

                for (op = 0; op < operandCt; ++op)
                    operands[op] = tuplerow[op][col][plane[op]];

                tuplerowOut[col][outplane] =
                    applyUnNormalizedFunction(function, operands, operandCt,
                                              maxval);

                assert(tuplerowOut[col][outplane] >= 0);
                assert(tuplerowOut[col][outplane] <= outpamP->maxval);
            }
        }
        pnm_writepamrow(outpamP, tuplerowOut);
    }

    for (i = 0; i < operandCt; ++i)
        pnm_freepamrow(tuplerow[i]);
    free(tuplerow);
    pnm_freepamrow(tuplerowOut);
    free(plane);
    free(operands);
}



int
main(int argc, const char *argv[]) {

    struct CmdlineInfo cmdline;
    struct pam * inpam;  /* malloc'ed array */
    FILE **      ifP;    /* malloc'ed array */
    struct pam   outpam;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    MALLOCARRAY(inpam, cmdline.operandCt);
    MALLOCARRAY(ifP,   cmdline.operandCt);

    if (!inpam || !ifP)
        pm_error("Failed to allocate arrays for %u operands",
                 cmdline.operandCt);

    {
        unsigned int i;
        for (i = 0; i < cmdline.operandCt; ++i) {
            ifP[i] = pm_openr(cmdline.operandFileNames[i]);

            pnm_readpaminit(ifP[i], &inpam[i], PAM_STRUCT_SIZE(tuple_type));

            if (i > 0) {
                if (inpam[i].width != inpam[0].width ||
                    inpam[i].height != inpam[0].height) {
                    pm_error("The images must be the same width and height.  "
                             "The first is %ux%ux%u, but another is %ux%ux%u",
                             inpam[0].width, inpam[0].height, inpam[0].depth,
                             inpam[i].width, inpam[i].height, inpam[i].depth);
                }
            }
        }
    }
    computeOutputType(&outpam, inpam, cmdline.operandCt, cmdline.function);

    pnm_writepaminit(&outpam);

    switch (functionCategory(cmdline.function)) {
    case CATEGORY_FRACTIONAL_ARITH:
        if (maxvalsAreEqual(inpam, cmdline.operandCt) &&
            inpam[0].maxval == outpam.maxval)
            doUnNormalizedArith(inpam, cmdline.operandCt, &outpam,
                                cmdline.function);
        else
            doNormalizedArith(inpam, cmdline.operandCt, &outpam,
                              cmdline.function, cmdline.closeness);
        break;
    case CATEGORY_BITSTRING:
    case CATEGORY_SHIFT:
        doUnNormalizedArith(inpam, cmdline.operandCt, &outpam,
                            cmdline.function);
        break;
    }

    {
        unsigned int i;
        for (i = 0; i < cmdline.operandCt; ++i)
            pm_close(ifP[i]);
    }

    return 0;
}



