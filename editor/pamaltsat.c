#include <stdbool.h>
#include <assert.h>
#include <string.h>

#include <pam.h>
#include <pm_gamma.h>
#include <nstring.h>

#include "shhopt.h"
#include "mallocvar.h"

typedef unsigned int  uint;
typedef unsigned char uchar;

typedef enum {MLog,  MSpectrum } Method; /* method identifiers */

typedef struct {
    Method       method;
    const char * name;
} MethodTableEntry;

MethodTableEntry methodTable[] = {
    {MLog,      "log"},
    {MSpectrum, "spectrum"}
};

/* Command-line arguments parsed: */
typedef struct {
    const char * inputFileName;
        /* name of the input file. "-" for stdin */
    float        strength;
    uint         linear;
    Method       method;
} CmdlineInfo;




static Method
methodFmNm(const char * const methodNm) {
/*----------------------------------------------------------------------------
   The method of saturation whose name is 'methodNm'
-----------------------------------------------------------------------------*/
    uchar  i;
    bool found;
    Method method;

    for (i = 0, found = false; i < ARRAY_SIZE(methodTable) && !found; ++i) {
        if (streq(methodNm, methodTable[i].name)) {
            found = true;
            method = methodTable[i].method;
        }
    }

    if (!found) {
        /* Issue error message and abort */
        char * methodList;
        uint   methodListLen;

        /* Allocate a buffer to store the list of known saturation methods: */
        for (i = 0, methodListLen = 0; i < ARRAY_SIZE(methodTable); ++i)
            methodListLen += strlen(methodTable[i].name) + 2;

        MALLOCARRAY(methodList, methodListLen);

        if (!methodList)
            pm_error("Failed to allocate memory for %lu saturation "
                     "method names", ARRAY_SIZE(methodTable));

        /* Fill the list of methods: */
        for (i = 0, methodList[0] = '\0'; i < ARRAY_SIZE(methodTable); ++i) {
            if (i > 0)
                strcat(methodList, ", ");
            strcat(methodList, methodTable[i].name);
        }

        pm_error("Unknown saturation method: '%s'. Known methods are: %s",
                 methodNm, methodList);

        free(methodList);
    }
    return method;
}



static CmdlineInfo
parsedCommandLine(int argc, const char ** argv) {

    CmdlineInfo cmdline;
    optStruct3 opt;

    uint option_def_index;
    uint methodSpec, strengthSpec, linearSpec;
    const char * method;

    optEntry * option_def;
    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "method",   OPT_STRING, &method,            &methodSpec,   0);
    OPTENT3(0, "strength", OPT_FLOAT,  &cmdline.strength,  &strengthSpec, 0);
    OPTENT3(0, "linear",   OPT_FLAG,   &cmdline.linear,    &linearSpec,   0);

    opt.opt_table     = option_def;
    opt.short_allowed = 0;
    opt.allowNegNum   = 0;

    pm_optParseOptions3( &argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (methodSpec)
        cmdline.method = methodFmNm(method);
    else
        cmdline.method = MSpectrum;

    if (!strengthSpec)
        pm_error("You must specify -strength");

    if (!linearSpec)
        cmdline.linear = 0;

    if (argc-1 < 1)
        cmdline.inputFileName = "-";
    else {
        cmdline.inputFileName = argv[1];
        if (argc-1 > 1)
            pm_error("Program takes at most one argument:  file name");
    }

    free(option_def);

    return cmdline;
}



typedef struct {
    double _[3];
} TupleD;

typedef struct {
/*----------------------------------------------------------------------------
  Information about a color sample in linear format
-----------------------------------------------------------------------------*/
    TupleD sample;    /* layer intensities                        */
    double maxval;    /* the highest layer intensity              */
    uint   maxl;      /* index of that layer                      */
    uint   minl;      /* index of the layer with lowest intensity */
    double intensity; /* total sample intensity                   */
} LinSampleInfo;

/* ---------------------------- Binary search ------------------------------ */
/*                    ( a minimal drop-in implementation )                   */

/* Function to search, where <data> is an arbitrary user-supplied parameter */
typedef double (binsearchFunc)(double       const x,
                               const void * const data);

/* The binary-search function. Returns such <x> from [<min>, <max>] that
   monotonically increasing function func(x, data) equals <value> within
   precision <prec>. <dataP> is an arbitary parameter to <func>. */
static double
binsearch(binsearchFunc       func,
          const void  * const dataP,
          double        const prec,
          double        const minArg,
          double        const maxArg,
          double        const value
         ) {
    double x;
    double min, max;
    bool found;

    for (min = minArg, max = maxArg, found = false; !found;) {

        x = (min + max) / 2;
        {
            double const f = func(x, dataP);

            if ((fabs(f - value)) < prec)
                found = true;
            else {
                assert(f != value);

                if (f > value) max = x;
                else           min = x;
            }
        }
    }
    return x;
}

/* ------------- Utilities not specific to saturation methods -------------- */

/* Y chromaticities in Rec.709: R       G       B  */
static double const yCoeffs[3]  = {0.3333, 0.6061, 0.0606};

static void
applyRatio(TupleD * const tupP,
           double   const ratio) {
/*----------------------------------------------------------------------------
  Multiply the components of tuple *tupP by coefficient 'ratio'.
-----------------------------------------------------------------------------*/
    uint c;

    for (c = 0; c < 3; ++c)
        tupP->_[c] = tupP->_[c] * ratio;
}



static void
getTupInfo(tuplen          const tup,
           bool            const linear,
           LinSampleInfo * const siP) {
/*----------------------------------------------------------------------------
  Convert PBM tuple <tup> into linear form with double precision siP->sample
  and obtain also additional information required for further processing.
  Return the result as *siP.
-----------------------------------------------------------------------------*/
    uint i;
    double minval;

    minval         = 1.1;
    siP->intensity = 0;
    siP->maxval    = 0.0;
    siP->maxl      = 0;

    for (i = 0; i < 3; ++i) {
        double linval;
        if (!linear)
            linval = pm_ungamma709(tup[i]);
        else
            linval = tup[i];

        siP->sample._[i] = linval;

        if (linval > siP->maxval) {
            siP->maxval = linval;
            siP->maxl   = i;
        }
        if (linval < minval) {
           siP->minl = i;
           minval    = linval;
        }
        siP->intensity += linval * yCoeffs[i];
    }
}

/* ------------------------ Logarithmic saturation ------------------------- */

/* Method and algorithm by Anton Shepelev.  */

static void
tryLogSat(double          const sat,
          LinSampleInfo * const siP,
          TupleD *        const tupsatP,
          double *        const intRatioP,
          double *        const highestP) {
/*----------------------------------------------------------------------------
  Try to increase the saturation of siP->sample by a factor 'sat' and return
  the result as *tupsatP.

  Also return as *intRatioP the ratio of intensities of 'tupin' and
  siP->sample.

  Return as *highestP the highest component the saturated color would have if
  normalized to intensity siP->intensity.

  If the return value exceeds unity saturation cannot be properly increased by
  the required factor.
-----------------------------------------------------------------------------*/
    uint   c;
    double intSat;

    for (c = 0, intSat = 0.0; c < 3; ++c) {
        tupsatP->_[c] = pow(siP->sample._[c], sat);
        intSat       = intSat + tupsatP->_[c] * yCoeffs[c];
    }

    {
        double const intRatio = siP->intensity / intSat;

        double const maxComp = tupsatP->_[siP->maxl] * intRatio;

        *intRatioP = intRatio;
        *highestP = maxComp;
    }
}



/* Structure for the binary search of maximum saturation: */
typedef struct {
    LinSampleInfo * siP;
        /* original color with precalculated information  */
    TupleD *        tupsatP;
        /* saturated color                            */
    double *        intRatioP;
        /* ratio of orignal and saturated intensities */
} MaxLogSatInfo;



static binsearchFunc binsearchMaxLogSat;

static double
binsearchMaxLogSat(double       const x,
                   const void * const dataP) {
/*----------------------------------------------------------------------------
  Target function for the generic binary search routine, for the finding
  of the maximum possible saturation of a given color. 'dataP' shall point
  to a MaxSatInfo structure.
-----------------------------------------------------------------------------*/
    const MaxLogSatInfo * const infoP = dataP;

    double highest;

    tryLogSat(x, infoP->siP, infoP->tupsatP, infoP->intRatioP, &highest);

    return highest;
}



static void
getMaxLogSat(LinSampleInfo * const siP,
             TupleD        * const tupsatP,
             double        * const intRatioP,
             double          const upperLimit
          ) {
/*  Saturates the color <siP->sample> as much as possible and stores the result
    in <tupsatP>, which must be multiplied by <*intRatioP> in order to restore
    the intensity of the original color. The range of saturation search is
    [1.0..<upperlimit>]. */
    const double PREC = 0.00001; /* precision of binary search */

    MaxLogSatInfo info;

    info.siP       = siP;
    info.tupsatP   = tupsatP;
    info.intRatioP = intRatioP;

/*  Discarding return value (maximum saturation) because upon completion of
    binsearch() info.tupsatP will contain the saturated color. The target value
    of maximum channel intensity is decreased by PREC in order to avoid
    overlow. */
    binsearch(binsearchMaxLogSat, &info, PREC, 1.0, upperLimit, 1.0 - PREC);
}



static void
saturateLog(LinSampleInfo* const siP,
            double         const sat,
            TupleD*        const tupsatP) {
/*----------------------------------------------------------------------------
  Saturate linear tuple *siP using the logarithmic saturation method.
-----------------------------------------------------------------------------*/
    double intRatio;
        /* ratio of original and saturated intensities */
    double maxlValSat;
        /* maximum component intensity in the saturated sample */

    tryLogSat(sat, siP, tupsatP, &intRatio, &maxlValSat);

    /* if we cannot saturate siP->sample by 'sat', use the maximum possible
       saturation
    */
    if (maxlValSat > 1.0)
        getMaxLogSat(siP, tupsatP, &intRatio, sat);

    /* restore the original intensity: */
    applyRatio(tupsatP, intRatio);
}



/* ------------------------- Spectrum saturation --------------------------- */

/* Method and algorithm by Anton Shepelev.  */

static void
saturateSpectrum(LinSampleInfo * const siP,
                 double          const sat,
                 TupleD *        const tupsatP) {
/*----------------------------------------------------------------------------
  Saturate linear tuple *siP using the Spectrum saturation method.
-----------------------------------------------------------------------------*/
    double k;
    double * sample;

    sample = siP->sample._; /* short-cut to the input sample data */

    if (sample[siP->minl] == sample[siP->maxl])
        k = 1.0; /* Cannot saturate a neutral sample */
    else {
        double const km1 =
            (1.0 - siP->intensity)/(siP->maxval - siP->intensity);
            /* Maximum saturation factor that keeps maximum layer intesity
               within range
            */
        double const km2 = siP->intensity/(siP->intensity - sample[siP->minl]);
            /* Maximum saturation factor  that keeps minimum layer intesity
               within range
            */

        /* To satisfy both constraints, choose the strictest: */
        double const km = km1 > km2 ? km2 : km1;

        /* Ensure the saturation factor does exceed the maximum
           possible value:
        */
        k = sat < km ? sat : km;
    }

    {
        /* Initialize the resulting sample with the input value */
        uint i;
        for (i = 0; i < 3; ++i)
            tupsatP->_[i] = sample[i];
    }

    applyRatio(tupsatP, k); /* apply the saturation factor */

    {
         /* restore the original intensity */
        uint i;
        for (i = 0; i < 3; ++i)
            tupsatP->_[i] = tupsatP->_[i] - siP->intensity * (k - 1.0);
    }
}



/* --------------------- General saturation algorithm ---------------------- */

static void
saturateTup(Method const method,
            double const sat,
            bool   const linear,
            tuplen const tup) {
/*----------------------------------------------------------------------------
  Saturate black and white tuple 'tup'
-----------------------------------------------------------------------------*/
    LinSampleInfo si;

    getTupInfo(tup, linear, &si);

    if (sat < 1.0 ||  /* saturation can always be decresed */
        si.maxval < 1.0 ) { /* there is room for increase        */

        TupleD tupsat;

        /* Dispatch saturation methods:
           (There seems too little benefit in using a table of
           function pointers, so a manual switch should suffice)
        */
        switch (method) {
            case MLog:      saturateLog     (&si, sat, &tupsat); break;
            case MSpectrum: saturateSpectrum(&si, sat, &tupsat); break;
        }

        /* Put the processed tuple back in the tuple row, gamma-adjusting it
           if required.
        */
        {
            uint i;

            for (i = 0; i < 3; ++i)
                tup[i] = linear ? tupsat._[i] : pm_gamma709(tupsat._[i]);
        }
    }
}



static void
pamaltsat(CmdlineInfo const cmdline,
          FILE *      const ofP) {

    struct pam inPam, outPam;
    tuplen *   tuplerown;
    FILE *     ifP;
    uint       row;

    ifP = pm_openr(cmdline.inputFileName);

    pnm_readpaminit(ifP, &inPam, PAM_STRUCT_SIZE(tuple_type));

    outPam = inPam;
    outPam.file = ofP;

    tuplerown = pnm_allocpamrown(&inPam);

    pnm_writepaminit(&outPam);

    for (row = 0; row < inPam.height; ++row) {
        pnm_readpamrown(&inPam, tuplerown);

        if (inPam.depth >= 3) {
            uint col;

            for (col = 0; col < inPam.width; ++col)
                saturateTup(cmdline.method, cmdline.strength, cmdline.linear,
                            tuplerown[col]);
        }

        pnm_writepamrown(&outPam, tuplerown);
    }

    pnm_freepamrown(tuplerown);
    pm_close(ifP);
}



int
main(int argc, const char ** argv) {

    CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    cmdline = parsedCommandLine(argc, argv);

    pamaltsat(cmdline, stdout);

    return 0;
}



