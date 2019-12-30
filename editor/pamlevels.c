#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "netpbm/pam.h"
#include "netpbm/pm_system.h"
#include "netpbm/pm_gamma.h"
#include "netpbm/nstring.h"
#include "netpbm/ppm.h"

#include "shhopt.h"
#include "mallocvar.h"

/* ----------------------------- Type aliases ------------------------------ */

typedef unsigned char uchar;
typedef unsigned int  uint;
typedef struct   pam  pam;

typedef struct {
/*----------------------------------------------------------------------------
  An RGB triple, in linear intensity or linear brightness; user's choice.
-----------------------------------------------------------------------------*/
    double _[3];
} Rgb;

typedef struct {
/*----------------------------------------------------------------------------
  A quadratic polynomial
-----------------------------------------------------------------------------*/
    double coeff[3];
} Polynomial;

typedef struct {
/*----------------------------------------------------------------------------
  A set of source or target sample values, in some plane.

  These are either intensity-linear or brightness-linear; user's choice.

  There could be two or three values; user must know which.
-----------------------------------------------------------------------------*/
    double _[3];
} SampleSet;

/* ------------------------- Parse transformations ------------------------- */

typedef struct {
/*----------------------------------------------------------------------------
  A mapping of one source color to one target color, encoded in linear RGB
-----------------------------------------------------------------------------*/
    tuplen from;
    tuplen to;
} Trans;

typedef struct {
    const char * from;  /* color specifications  */
    const char * to;    /*   as they appear on commandline */
    unsigned int  hasFrom;      /* "from" part is present */
    unsigned int  hasTo;        /* "to" part is present */
    char  nameFromS[3]; /* short option name */
    char  nameToS  [3];
    char  nameFromL[6]; /* long option name */
    char  nameToL  [6];
} TransArg;

typedef struct {
    TransArg _[3];
} TransArgSet;

typedef struct {
    unsigned int n;
        /* Number of elements in 't', 2 for linear transformation; 3 for
           quadratic.
        */
    Trans t[3];
} TransSet;

typedef struct {
    unsigned int linear;
    unsigned int fitbrightness;
    TransSet     xlats; /* color mappings (-from1, -to1, etc.) */
    const char * inputFileName;  /* the input file name, "-" for stdin     */
} CmdlineInfo;



static void
optAddTrans (optEntry *     const option_def,
             unsigned int * const option_def_indexP,
             TransArg *     const xP,
             char           const index) {

    char indexc;
    uint option_def_index;

    option_def_index = *option_def_indexP;

    indexc = '0' + index;

    STRSCPY(xP->nameFromL, "from "); xP->nameFromL[4] = indexc;
    STRSCPY(xP->nameToL,   "to "  ); xP->nameToL  [2] = indexc;
    STRSCPY(xP->nameFromS, "f "   ); xP->nameFromS[1] = indexc;
    STRSCPY(xP->nameToS,   "t "   ); xP->nameToS  [1] = indexc;

    OPTENT3(0, xP->nameFromL, OPT_STRING, &xP->from, &xP->hasFrom, 0);
    OPTENT3(0, xP->nameFromS, OPT_STRING, &xP->from, &xP->hasFrom, 0);
    OPTENT3(0, xP->nameToL,   OPT_STRING, &xP->to,   &xP->hasTo,   0);
    OPTENT3(0, xP->nameToS,   OPT_STRING, &xP->to,   &xP->hasTo,   0);

    *option_def_indexP = option_def_index;
}



static void
parseColor(const char * const text,
           tuplen *     const colorP) {
/*----------------------------------------------------------------------------
  Parses color secification in <text>, converts it into linear RGB,
  and stores the result in <colorP>.
-----------------------------------------------------------------------------*/
    const char * const lastsc = strrchr(text, ':');

    const char * colorname;
    double mul;
    tuplen unmultipliedColor;
    tuplen color;

    if (lastsc) {
        /* Specification contains a colon.  It might be the colon that
           introduces the optional multiplier, or it might just be the colon
           after the type specifier, e.g. "rgbi:...".
        */

        if (strstr(text, "rgb") == text && strchr(text, ':') == lastsc) {
            /* The only colon present is the one on the type specifier.
               So there is no multiplier.
            */
            mul = 1.0;
            colorname = pm_strdup(text);
        } else {
            /* There is a multiplier (possibly invalid, though). */
            const char * const mulstart = lastsc + 1;

            char * endP;
            char colorbuf[50];

            errno = 0;
            mul = strtod(mulstart, &endP);
            if (errno != 0 || endP == mulstart)
                pm_error("Invalid sample multiplier: '%s'", mulstart);

            strncpy(colorbuf, text, lastsc - text);
            colorbuf[lastsc - text] = '\0';
            colorname = pm_strdup(colorbuf);
        }
    } else {
        mul = 1.0;
        colorname = pm_strdup(text);
    }

    unmultipliedColor = pnm_parsecolorn(colorname);

    pm_strfree(colorname);

    MALLOCARRAY_NOFAIL(color, 3);

    {
        /* Linearize and apply multiplier */
        unsigned int i;
        for (i = 0; i < 3; ++i)
            color[i] = pm_ungamma709(unmultipliedColor[i]) * mul;
    }
    free(unmultipliedColor);

    *colorP = color;
}



static void
parseTran (TransArg const transArg,
           Trans *  const rP) {

    parseColor(transArg.from, &rP->from);
    parseColor(transArg.to,   &rP->to);
}



static void
calcTrans(TransArgSet   const transArgs,
          TransSet *    const transP) {
/*----------------------------------------------------------------------------
   Interpret transformation option (-from1, etc.) values 'transArg'
   as transformations, *transP.
-----------------------------------------------------------------------------*/
    unsigned int xi;

    for (transP->n = 0, xi = 0; xi < 3; ++xi) {
        const TransArg * const xformP = &transArgs._[xi];

        if (xformP->hasFrom || xformP->hasTo) {
            if (!xformP->hasFrom || !xformP->hasTo)
                pm_error("Mapping %u incompletely specified - "
                         "you specified -fromN or -toN but not the other",
                    xi + 1);
            parseTran(*xformP, &transP->t[transP->n++]);
        }
    }
    if (transP->n < 2)
        pm_error("You must specify at least two mappings with "
                 "-from1, -to1, etc.  You specified %u", transP->n);
}



static void
parseCommandLine(int                 argc,
                 const char **       argv,
                 CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    TransArgSet xlations; /* color mapping as read from command line */

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;  /* incremented by OPTENT3 */

    OPTENT3(0, "fitbrightness",          OPT_FLAG, NULL,
            &cmdlineP->fitbrightness, 0);
    OPTENT3(0, "linear",                 OPT_FLAG, NULL,
            &cmdlineP->linear,        0);

    {
        unsigned int i;
        for (i = 0; i < 3; ++i)
            optAddTrans(option_def, &option_def_index,
                        &xlations._[i], i + 1);
    }

    opt.opt_table     = option_def;
    opt.short_allowed = 0;
    opt.allowNegNum   = 0;

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);

    if (cmdlineP->linear && cmdlineP->fitbrightness) {
        pm_error("You cannot use -linear and -fitbrightness together");
        /* Note: It actually makes sense to use them together; we're just not
           willing to put the effort into something it's unlikely anyone will
           want.
        */
    }

    calcTrans(xlations, &cmdlineP->xlats);

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else {
        cmdlineP->inputFileName = argv[1];

        if (argc-1 > 1)
            pm_error("Too many arguments.  "
                     "The only possible non-option argument "
                     "is the input file name");
    }

    free(option_def);
}



static void
errResolve(void) {
    pm_error( "Cannot resolve the transformations");
}



static double
sqr(double const x) {
    return x * x;
}



static void
solveOnePlane(SampleSet    const f,
              SampleSet    const t,
              unsigned int const n,
              Polynomial * const solutionP) {
/*----------------------------------------------------------------------------
  Find the transformation that maps f[i] to t[i] for 0 <= i < n.
-----------------------------------------------------------------------------*/
    double const eps = 0.00001;

    double a, b, c;

    /* I have decided against generic methods of solving systems of linear
       equations in favour of simple explicit formulas, with no memory
       allocation and tedious matrix processing.
    */

    switch (n) {
    case 3: {
        double const aDenom =
            sqr( f._[0] ) * ( f._[1] - f._[2] ) -
            sqr( f._[2] ) * ( f._[1] - f._[0] ) -
            sqr( f._[1] ) * ( f._[0] - f._[2] );

        if (fabs(aDenom) < eps)
            errResolve();

        a = (t._[1] * (f._[2] - f._[0]) - t._[0] * (f._[2] - f._[1]) -
             t._[2] * (f._[1] - f._[0]))
            / aDenom;
    } break;
    case 2:
        a = 0.0;
        break;
    default:
        a = 0.0; /* to avoid a warning that <a> "may be uninitialized". */
        pm_error("INTERNAL ERROR: solve(): impossible value of n: %u", n);
    }

    {
        double const bDenom = f._[1] - f._[0];

        if (fabs(bDenom) < eps)
            errResolve();

        b = (t._[1] - t._[0] + a * (sqr(f._[0]) - sqr(f._[1]))) / bDenom;
    }

    c = -a * sqr(f._[0]) - b * f._[0] + t._[0];

    solutionP->coeff[0] = a; solutionP->coeff[1] = b; solutionP->coeff[2] = c;
}



static void
chanData(TransSet     const ta,
         bool         const fittingBrightness,
         unsigned int const plane,
         SampleSet *  const fromP,
         SampleSet *  const toP) {
/*----------------------------------------------------------------------------
  Collate transformations from 'ta' for plane 'plane'.
-----------------------------------------------------------------------------*/
    unsigned int i;

    for (i = 0; i < ta.n; ++i) {
        if (fittingBrightness) { /* working with gamma-compressed values */
            fromP->_[i] = pm_gamma709(ta.t[i].from[plane]);
            toP->  _[i] = pm_gamma709(ta.t[i].to  [plane]);
        } else { /* working in linear RGB */
            fromP->_[i] = ta.t[i].from[plane];
            toP->  _[i] = ta.t[i].to  [plane];
        }
    }
}



typedef struct {
    Polynomial _[3];  /* One per plane */
} Solution;



static void
solveFmCmdlineOpts(CmdlineInfo  const cmdline,
                   unsigned int const depth,
                   Solution *   const solutionP) {
/*----------------------------------------------------------------------------
   Compute the function that will transform the tuples, based on what the user
   requested ('cmdline').

   The function takes intensity-linear tuples for the normal levels function,
   or brightness-linear for the brightness approximation levels function.

   The transformed image has 'depth' planes.
-----------------------------------------------------------------------------*/
    unsigned int plane;
    SampleSet from, to;
    /* This initialization to bypass the "may be uninitialized" warning: */
    to  ._[0] = 0; to.  _[1] = 0; to  ._[2] = 0;
    from._[0] = 1; from._[1] = 0; from._[2] = 0;

    for (plane = 0; plane < depth; ++plane) {

        chanData(cmdline.xlats, cmdline.fitbrightness, plane, &from, &to);
        solveOnePlane(from, to, cmdline.xlats.n, &solutionP->_[plane]);
    }
}



static samplen
xformedSample(samplen    const value,
              Polynomial const polynomial) {
/*----------------------------------------------------------------------------
  'sample' transformed by 'polynomial'.
-----------------------------------------------------------------------------*/
    double const res =
        (polynomial.coeff[0] * value + polynomial.coeff[1]) * value +
        polynomial.coeff[2];

    return MAX(0.0f, MIN(1.0f, res));
}



static void
pamlevels(CmdlineInfo const cmdline) {

    unsigned int row;
    pam      inPam, outPam;
    Solution solution;
    tuplen * tuplerown;
    FILE   * ifP;

    ifP = pm_openr(cmdline.inputFileName);

    pnm_readpaminit(ifP, &inPam, PAM_STRUCT_SIZE(tuple_type));

    outPam = inPam;
    outPam.file = stdout;

    solveFmCmdlineOpts(cmdline, inPam.depth, &solution);

    tuplerown = pnm_allocpamrown(&inPam);

    pnm_writepaminit(&outPam);

    for (row = 0; row < inPam.height; ++row) {
        unsigned int col;

        pnm_readpamrown(&inPam, tuplerown);

        if (!cmdline.linear && !cmdline.fitbrightness)
            pnm_ungammarown(&inPam, tuplerown);

        for (col = 0; col < inPam.width; ++col) {
            unsigned int plane;

            for (plane = 0; plane < inPam.depth; ++plane) {
                tuplerown[col][plane] =
                    xformedSample(tuplerown[col][plane], solution._[plane]);
            }
        }
        if (!cmdline.linear && !cmdline.fitbrightness)
            pnm_gammarown(&inPam, tuplerown);

        pnm_writepamrown(&outPam, tuplerown);
    }
    pnm_freepamrown(tuplerown);
    pm_close(ifP);
}



static void
freeCmdLineInfo(CmdlineInfo cmdline) {
/*----------------------------------------------------------------------------
  Free any memory that has been dynamically allocated in <cmdline>.
-----------------------------------------------------------------------------*/
    TransSet * const xxP = &cmdline.xlats;

    uint x;

    for (x = 0; x < xxP->n; ++x) {
        free(xxP->t[x].from);
        free(xxP->t[x].to);
    }
}



int main(int    argc, const char * argv[]) {

    CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    pamlevels(cmdline);

    freeCmdLineInfo(cmdline);

    return 0;
}



