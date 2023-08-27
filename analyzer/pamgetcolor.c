#define _C99_SOURCE  /* Make sure snprintf() is in stdio.h */

#include <string.h>
#include <stdio.h>

#include <nstring.h>
#include <pm_gamma.h>
#include <pam.h>

#include "pm_c_util.h"
#include "shhopt.h"
#include "mallocvar.h"

typedef unsigned int  uint;

typedef struct {
/*----------------------------------------------------------------------------
  Specification of a circular "region" over which to measure the average color
-----------------------------------------------------------------------------*/
    uint         x;        /* coordinates of the center                      */
    uint         y;        /* of the region;                                 */
    const char * label;    /* optional label supplied on the command line    */
} RegSpec;

typedef struct {
/*----------------------------------------------------------------------------
  Represents a single color measurement over a "region"
-----------------------------------------------------------------------------*/
    uint         area;     /* area in pixels over which to average the color */
    /* cumulative normalized intensity-proportiunal value of the region:     */
    double       color[3];
} RegData;

typedef struct {
/*----------------------------------------------------------------------------
  All the information the user supplied in the command line, in a form easy
  for the program to use.
-----------------------------------------------------------------------------*/
    uint         linear;
    uint         radius;
    uint         regN;      /* number of regions                             */
    uint         maxLbLen;  /* maximum label length                          */
    RegSpec *    regSpecs;
        /* list of points to sample, dynamically allocated*/
    const char * formatStr; /* output color format as string                 */
    uint         formatId;  /* the Id of the selected color format           */
    uint         formatArg; /* the argument to the color formatting function */
    const char * infile;
} CmdLineInfo;

/* Generic pointer to a color-formatting function. Returns the textual
   representation of the color <tuple> in terms of the image pointed to
   by <pamP>. <param> is a generic integer parameter that depends on the
   specific function and may denote precision or maxval.
*/
typedef const char *
(*FormatColor)(struct pam * const pamP,
               tuple        const color,
               uint         const param);

typedef struct ColorFormat {
/*----------------------------------------------------------------------------
  The color format specification
-----------------------------------------------------------------------------*/
    char        const * id;
        /* format id (compared against the -format command-line argument) */
    FormatColor const   formatColor;
        /* function that returns converts a color into this format */
    char        const * argName;
        /* meaning of the <param> argument of <formatColor>() */
    uint        const   defParam;
        /* default value of that argument        */
    uint        const   maxParam;
        /* maximum value of that argument        */
} ColorFormat;



static const char *
fcInt(struct pam * const pamP,
      tuple        const color,
      uint         const param) {
/*----------------------------------------------------------------------------
  Format 'color' as an integer tuple with maxval 'param'
-----------------------------------------------------------------------------*/
    return pnm_colorspec_rgb_integer(pamP, color, param);
}



static const char *
fcNorm(struct pam * const pamP,
       tuple        const color,
       uint         const param) {
/*----------------------------------------------------------------------------
  Format 'color' as normalized tuple with precision 'param'
-----------------------------------------------------------------------------*/
    return pnm_colorspec_rgb_norm(pamP, color, param);
}



static const char *
fcX11(struct pam * const pamP,
      tuple        const color,
      uint         const param) {
/*----------------------------------------------------------------------------
  Format 'color' as hexadecimal tuple with 'param' digits
-----------------------------------------------------------------------------*/
    return pnm_colorspec_rgb_x11(pamP, color, param);
}



static int const defaultFormat = 0;

/* Table with the full information about color formats */
ColorFormat const formats[ 3 ] = {
    /*   Id     Function  Argument name  Default  Max   */
    {   "int",  &fcInt,   "maxval",      255,     65535  },
    {   "norm", &fcNorm,  "digit count",   3,         6  },
    {   "x11",  &fcX11,   "digit count",   2,         4  }
};



static inline uint
sqri(int const v) {

    return v * v;
}



static RegSpec
parsedRegSpec(const char * const s) {
/*----------------------------------------------------------------------------
  The region specification represented by command line argument 's'.

  's' is of the format x,y[:label].
-----------------------------------------------------------------------------*/
    char * end;
    char * start;
    RegSpec res;

    start = (char *)s;

    res.x = strtol(start, &end, 10);
    do {
        if (start == end)
            break; /* x not parsed */
        start = end;
        if (*end != ',')
            break;  /* no comma after x */
        start = end + 1;

        res.y = strtol(start, &end, 10);
        if (start == end)
            break; /* y not parsed */

        /* these multiple returns to avoid goto and deep nesting: */
        if (*end == '\0') { /* no label specified */
            res.label = (char *)s;
            return res;
        }
        if (*end == ':') { /* a label specified */
            res.label = end + 1;
            if (*res.label == '\0')
                break; /* empty label */
            return res;
        }
    } while (false);

    pm_error("Wrong region specification: %s", s);

    return res; /* to avoid the false warning that nothing is returned */
}



static void
parseColorFmt(const char * const formatStr,
              uint *       const formatIdP,
              uint *       const formatArgP) {
/*----------------------------------------------------------------------------
  Parse the color format specification string 'formatStr' as
  *formatIdP and *formatArgP.

  A format specification string is of format format[:arg].
-----------------------------------------------------------------------------*/
    const char *  const errSpec = "Wrong color format specification: ";

    const char *  colonLoc; /* location of the colon in the specification */
    uint          n, f;
    const ColorFormat * formatP;
    uint formatId;
    bool found;

    colonLoc  = strchr(formatStr, ':');
    if (colonLoc != NULL) n = colonLoc - formatStr;
    else                  n = strlen(formatStr);

    for (f = 0, found = false; f < ARRAY_SIZE(formats) && !found; ++f) {
        if (strncmp(formatStr, formats[f].id, n) == 0) {
            found = true;
            formatId = f;
        }
    }
    if (!found)
        pm_error("Color format not recognized.");

    *formatIdP = formatId;

    formatP = &formats[formatId];

    if (colonLoc) {
        long int arg;
        const char * argStart;
        char * argEnd;

        argStart = colonLoc + 1;

        if (*argStart == '\0')
            pm_error("%sthe colon should be followed by %s.",
                errSpec, formatP->argName);

        arg = strtol(argStart, &argEnd, 10);

        if (*argEnd != '\0')
            pm_error("%sfailed to parse the %s: %s.",
                errSpec, formatP->argName, argStart);

        if (arg < 1)
            pm_error("%s%s must be greater than zero.",
                errSpec, formatP->argName);

        if (arg > formatP->maxParam)
            pm_error("%s%s cannot exceed %i.",
                errSpec, formatP->argName, formatP->maxParam);

        *formatArgP = arg;
    } else
        *formatArgP = formatP->defParam;
}



static CmdLineInfo
parsedCommandLine(int                 argc,
                  const char ** const argv) {

    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    CmdLineInfo cmdLine;

    uint infileSpec, radiusSpec, formatSpec, linearSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "infile",    OPT_STRING, &cmdLine.infile,    &infileSpec, 0);
    OPTENT3(0, "radius",    OPT_INT,    &cmdLine.radius,    &radiusSpec, 0);
    OPTENT3(0, "format",    OPT_STRING, &cmdLine.formatStr, &formatSpec, 0);
    OPTENT3(0, "linear",    OPT_FLAG,   &cmdLine.linear,    &linearSpec, 0);
    OPTENT3(0,  0,          OPT_END,    NULL,               NULL,        0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);

    if (!infileSpec)
        cmdLine.infile = "-";

    if (!radiusSpec)
        cmdLine.radius = 0;

    if (formatSpec) {
        parseColorFmt(cmdLine.formatStr,
                      &cmdLine.formatId, &cmdLine.formatArg);
    } else {
        cmdLine.formatId  = defaultFormat;
        cmdLine.formatArg = formats[defaultFormat].defParam;
    }

    if (!linearSpec)
        cmdLine.radius = 0;

    if (argc-1 < 1)
        pm_error("No regions specified.");

    cmdLine.regN = argc - 1;

    MALLOCARRAY(cmdLine.regSpecs, cmdLine.regN);

    if (!cmdLine.regSpecs)
        pm_error("Could not get memory for %u region specifications",
                 cmdLine.regN);

    {
        uint r;
        uint maxLbLen;

        for (r = 0, maxLbLen = 0; r < argc - 1; ++r) {
            size_t lbLen;
            cmdLine.regSpecs[r] = parsedRegSpec(argv[r+1]);
            lbLen = strlen(cmdLine.regSpecs[r].label);
            maxLbLen = MAX(maxLbLen, lbLen);
        }
        cmdLine.maxLbLen = maxLbLen;
    }

    free(option_def);

    return cmdLine;
}



static void
freeCommandLine(CmdLineInfo const cmdLine) {

    free(cmdLine.regSpecs);
}



static RegData *
allocRegSamples(uint n) {
/*----------------------------------------------------------------------------
  Allocate an array of <n> initialized region samples.  The array should be
  freed after use.
-----------------------------------------------------------------------------*/
    uint r;
    RegData * regSamples;
    regSamples = calloc(n, sizeof(RegData));

    for (r = 0; r < n; ++r) {
        uint l;

        regSamples[r].area = 0;

        for (l = 0; l < 3; ++l)
            regSamples[r].color[l] = 0.0;
    }
    return regSamples;
}



static uint
getYmax(struct pam * const pamP,
        CmdLineInfo  const cmdLine) {
/*----------------------------------------------------------------------------
  Find the maximum row in the image that contains a pixel from a region.
-----------------------------------------------------------------------------*/
    uint ymax, r, ycmax;
    ycmax = 0;

    for (r = 0; r < cmdLine.regN; ++r) {
        RegSpec spec = cmdLine.regSpecs[r];
        if (spec.y >= pamP->height || spec.x >= pamP->width)
            pm_error("Region at %i,%i is outside the image boundaries.",
                     spec.x, spec.y);

        if (spec.y > ycmax)
            ycmax = spec.y;
    }
    ymax = MIN(pamP->height - 1, ycmax + cmdLine.radius);

    return ymax;
}



static void
readChord(RegData *    const dataP,
          uint         const linear,
          struct pam * const pamP,
          tuple *      const row,
          uint         const x0,
          uint         const x1) {
/*----------------------------------------------------------------------------
  Update region sample *dataP with the data from horizontal chord lying in row
  'row' and going from 'x0' to 'x1'. 'linear' means tuples in 'row' are the
  intensity-linear values as opposed to normal libnetpbm gamma-adjusted
  values.
-----------------------------------------------------------------------------*/
    uint x;

    for (x = x0; x <= x1; ++x) {
        uint l;

        for (l = 0; l < 3; ++l) {
            double val;

            val = (double)row[x][l] / pamP->maxval;
            /* convert to intensity because brightness is not additive: */
            if (!linear)
                val = pm_ungamma709(val);
            dataP->color[l] += val;
        }
        ++dataP->area;
    }
}



static void
processRow(tuple *             const row,
           uint                const y,
           struct pam *        const pamP,
           const CmdLineInfo * const cmdLineP,
           RegData *           const regSamples) {
/*----------------------------------------------------------------------------
  Read a row from image described by *pamP into 'row', and update region
  samples regSamples[] from it.  'y' is the position of the row.
-----------------------------------------------------------------------------*/
    uint r;

    pnm_readpamrow(pamP, row);

    for (r = 0; r < cmdLineP->regN; ++r) {
        RegSpec   const spec = cmdLineP->regSpecs[r];
        RegData * const dataP = &regSamples[r];
        int       const yd = (int)spec.y - (int)y;

        if (abs(yd) > cmdLineP->radius) {
            /* Row is entirely above or below the region; Avoid the slow root
               operation
            */
        } else {
            uint const xd2 = sqri(cmdLineP->radius) - sqri(yd);
            uint const xd  = ROUNDU(sqrt((double)xd2));

            int x0, x1;

            x0 = spec.x - xd;  /* initial value */
            x1 = spec.x + xd;  /* initial value */

            /* clip horizontal chord to image boundaries: */
            if (x0 < 0)
                x0 = 0;
            if (x1 >= pamP->width)
                x1 = pamP->width - 1;

            readChord(dataP, cmdLineP->linear, pamP, row, x0, x1);
        }
    }
}



static RegData *
colorsFmImage(struct pam * const pamP,
              CmdLineInfo  const cmdLine) {
/*----------------------------------------------------------------------------
  Color data for the regions requested by 'cmdLine' in the image described by
  *pamP.
-----------------------------------------------------------------------------*/
    uint      y, ymax;
    RegData * samplesP;
    tuple *   row;
    FILE *    ifP;

    ifP = pm_openr(cmdLine.infile);

    pnm_readpaminit(ifP, pamP, PAM_STRUCT_SIZE(tuple_type));

    ymax = getYmax(pamP, cmdLine);

    samplesP = allocRegSamples(cmdLine.regN);
    row      = pnm_allocpamrow(pamP);

    for (y = 0; y <= ymax; ++y)
        processRow(row, y, pamP, &cmdLine, samplesP);

    pnm_freepamrow(row);
    pm_close(ifP);

    return samplesP;
}



static const char *
outputColorSpec(RegData      const data,
                CmdLineInfo  const cmdLine,
                struct pam * const pamP,
                tuple        const tup) {
/*----------------------------------------------------------------------------
  Color of region sample 'data' formatted for output as requested by
  'cmdLine'.

  *pamP tells how to interpret 'data'.

  'tup' is working space for internal use.
-----------------------------------------------------------------------------*/
    uint l;

    for (l = 0; l < 3; ++l)
        tup[l] = pm_gamma709(data.color[l]/data.area) * pamP->maxval;

    return formats[cmdLine.formatId].
        formatColor(pamP, tup, cmdLine.formatArg);
}



static void
printColors(struct pam *    const pamP,
            CmdLineInfo     const cmdLine,
            FILE *          const ofP,
            const RegData * const regSamples) {
/*----------------------------------------------------------------------------
  Print the colors regSamples[] to *ofP in the format
  requested by 'cmdLine'.

  *pamP tells how to interpret regSamples[]
-----------------------------------------------------------------------------*/
    char  fmt[20];
    uint  r;
    tuple tup;

    tup = pnm_allocpamtuple(pamP);

    snprintf(fmt, sizeof(fmt), "%%%is: %%s\n", cmdLine.maxLbLen);

    for (r = 0; r < cmdLine.regN; ++r) {
        RegSpec      spec;
        RegData      data;
        const char * color;

        spec  = cmdLine.regSpecs[r];

        data  = regSamples[r];

        color = outputColorSpec(data, cmdLine, pamP, tup);

        fprintf(ofP, fmt, spec.label, color);

        pm_strfree(color);
    }
    pnm_freepamtuple(tup);
}



int
main(int argc, const char *argv[]) {

    RegData *   regSamples;
    CmdLineInfo cmdLine;
    struct pam  pam;

    pm_proginit(&argc, argv);

    cmdLine = parsedCommandLine(argc, argv);

    regSamples = colorsFmImage(&pam, cmdLine);

    printColors(&pam, cmdLine, stdout, regSamples);

    freeCommandLine(cmdLine);
    free(regSamples);

    return 0;
}



