#include <string.h>
#include <nstring.h>
#include <pm_gamma.h>
#include <pam.h>

#include "shhopt.h"
#include "mallocvar.h"

typedef unsigned int  uint;

/* specification of a cirtular "region" over which to measre the avg. color: */
typedef struct {
    uint         x;        /* coordinates of the center                      */
    uint         y;        /* of the region;                                 */
    char const * label;    /* optional label supplied on the command line    */
} RegSpec;

/* represents a single color measurement over a "region": */
typedef struct {
    uint         area;     /* area in pixels over which to average the color */
    /* cumulative normalised intensity-proportiunal value of the region:     */
    double       color[3];
} RegData;

/*command-line parameters: */
typedef struct {
    uint         linear;
    uint         radius;
    uint         regN;      /* number of regions                             */
    uint         maxLbLen;  /* maximum label length                          */
    RegSpec *    regSpecs;
        /* list of points to sample, dymamically allocated*/
    char const * formatStr; /* output color format as string                 */
    uint         formatId;  /* the Id of the selected color format           */
    uint         formatArg; /* the argument to the color formatting function */
    char const * infile;
} CmdlineInfo;

/* Generic pointer to a color-formatting function. Returns the textual
   representation of the color <tuple> in terms of the image pointed-to
   by <pamP>. <param> is a generic integer parameter that depends on the
   specific funcion and may denote precison or maxval.
*/
typedef char const *
(*FormatColor)(struct pam * const pamP,
               tuple        const color,
               uint         const param);

/* The color format specificaiton: */
typedef struct ColorFormat {
    /* format id (compared against the -format command-line argument):       */
    char        const * id;
    /* function that returns converts a color into this format:              */
    FormatColor const   formatColor;
    /* meaning of the <param> argument of <formatColor>():                   */
    char        const * argName;
    uint        const   defParam;   /* default value of that argument        */
    uint        const   maxParam;   /* maximum value of that argument        */
} ColorFormat;



static char const *
fcInt(struct pam * const pamP,
      tuple        const color,
      uint         const param) {
/* format <color> as an integer tuple with maxval <param> */
    return pnm_colorspec_rgb_integer(pamP, color, param);
}



static char const *
fcNorm(struct pam * const pamP,
       tuple        const color,
       uint         const param) {
/* format <color> as normalised tuple with precision <param> */
    return pnm_colorspec_rgb_norm(pamP, color, param);
}



static char const *
fcX11(struct pam * const pamP,
      tuple        const color,
      uint         const param) {
/* format <color> as hexadecimal tuple with <param> digits*/
    return pnm_colorspec_rgb_x11(pamP, color, param);
}



#define FormatsN 3

static int const DefaultFormat = 0;
/* Table with the full information about color formats */
ColorFormat formats[ FormatsN ] = {
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
parseRegSpec(char const * const s) {
/*----------------------------------------------------------------------------
  Parse region specification <s> from the command line and return its
  structured representation.  A specification is of the format <x,y[:label].
-----------------------------------------------------------------------------*/
    char* end, *start;
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
    }
    while (1 == 0);

    pm_error("Wrong region specification: %s", s);

    return res; /* to avoid the false warning that nothing is returned */
}



static void
parseColorFmt(CmdlineInfo * const cmdLineP) {
/*----------------------------------------------------------------------------
  Parse the color format specificaction from the command line stored in the
  <formatStr> member of <cmdLineP> and save it into members <formatId> and
  <formatArg>.  A format specification is <format>[:<arg>].
-----------------------------------------------------------------------------*/
    const int     FmtNotFound = -1;
    const char *  ErrSpec = "Wrong color format specification: ";
    const char *  formatStr;
          char *  colonLoc; /* location of the colon in the specification */
    uint          n, f;
    ColorFormat * formatP;

    formatStr = cmdLineP->formatStr;
    colonLoc  = strchr(formatStr, ':');
    if (colonLoc != NULL) n = colonLoc - formatStr;
    else                  n = strlen(formatStr);

    cmdLineP->formatId = FmtNotFound;

    for (f = 0; f < FormatsN; f++) {
        if (strncmp(formatStr, formats[f].id, n) == 0) {
            cmdLineP->formatId = f;
            break;
        }
    }
    if (cmdLineP->formatId == FmtNotFound) {
        pm_error("Color format not recognised.");
    }
    formatP = &formats[cmdLineP->formatId];
    if (colonLoc != NULL) {
        long int arg;
        char *argStart, *argEnd;

        argStart = colonLoc + 1;
        if (*argStart == '\0')
            pm_error("%sthe colon should be followed by %s.",
                ErrSpec, formatP->argName);

        arg = strtol(argStart, &argEnd, 10);
        if (*argEnd != '\0')
            pm_error("%sfailed to parse the %s: %s.",
                ErrSpec, formatP->argName, argStart);

        if (arg < 1)
            pm_error("%s%s must be greater than zero.",
                ErrSpec, formatP->argName);

        if (arg > formatP->maxParam)
            pm_error("%s%s cannot exceed %i.",
                ErrSpec, formatP->argName, formatP->maxParam);
        cmdLineP->formatArg = arg;
    }
    else
        cmdLineP->formatArg = formatP->defParam;
}



static CmdlineInfo
parseCommandLine(int argc, char const ** argv) {
/*----------------------------------------------------------------------------
  Parse the command-line arguments and store them in a form convenient for the
  program.
-----------------------------------------------------------------------------*/
    int         r;
    uint        formatSet;
    CmdlineInfo cmdLine;
    optStruct3  opt;
    uint        option_def_index = 0;

    optEntry * option_def;
    MALLOCARRAY_NOFAIL(option_def, 100);

    cmdLine.radius    = 0;

    opt.opt_table     = option_def;
    opt.short_allowed = 0;
    opt.allowNegNum   = 0;

    OPTENT3(0, "infile",    OPT_STRING, &cmdLine.infile,    NULL,       0);
    OPTENT3(0, "radius",    OPT_INT,    &cmdLine.radius,    NULL,       0);
    OPTENT3(0, "format",    OPT_STRING, &cmdLine.formatStr, &formatSet, 0);
    OPTENT3(0, "linear",    OPT_FLAG,   &cmdLine.linear,    NULL,       0);
    OPTENT3(0,  0,          OPT_END,    NULL,               NULL,       0);

    cmdLine.radius = 0;
    cmdLine.linear = 0;
    cmdLine.infile = "-";

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);

    if (formatSet) {
        parseColorFmt(&cmdLine);
    } else {
        cmdLine.formatId  = DefaultFormat;
        cmdLine.formatArg = formats[DefaultFormat].defParam;
    }

    cmdLine.regN    = argc - 1;
    MALLOCARRAY_NOFAIL(cmdLine.regSpecs, cmdLine.regN);

    cmdLine.maxLbLen = 0;
    if (argc < 2)
        pm_error("No regions specified.");

    for (r = 0; r < argc - 1; r++) {
        size_t lbLen;
        cmdLine.regSpecs[r] = parseRegSpec(argv[r+1]);
        lbLen = strlen(cmdLine.regSpecs[r].label);
        if (lbLen > cmdLine.maxLbLen)
            cmdLine.maxLbLen = lbLen;
    }

    free(option_def);
    return cmdLine;
}



static RegData * allocRegSamples(uint n) {
/*----------------------------------------------------------------------------
  Allocate an array of <n> initialised region samles.  The array should be
  freed after use.
-----------------------------------------------------------------------------*/
    uint r;
    RegData * regSamples;
    regSamples = calloc(n, sizeof(RegData));
    for (r = 0; r < n; r++) {
        uint l;

        regSamples[r].area = 0;

        for (l = 0; l < 3; l++)
            regSamples[r].color[l] = 0.0;
    }
    return regSamples;
}



static uint getYmax(struct pam * const pamP,
                    CmdlineInfo  const cmdLine) {
/*----------------------------------------------------------------------------
  Find the maximum row in the image that contains a pixel from a region.
-----------------------------------------------------------------------------*/
    uint ymax, r, ycmax;
    ycmax = 0;
    for (r = 0; r < cmdLine.regN; r++) {
        RegSpec spec = cmdLine.regSpecs[r];
        if (spec.y >= pamP->height || spec.x >= pamP->width)
            pm_error("Region at %i,%i is outside the image boundaries.",
                     spec.x, spec.y);

        if (spec.y > ycmax)
            ycmax = spec.y;
    }
    ymax = ycmax + cmdLine.radius;
    if (ymax > pamP->height - 1)
        ymax = pamP->height - 1;
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
  Update region sample <dataP> with the data from horisontal chord lying in
  row <row> and going from <x0> to <x1>. <linear> denotes whether <pamP> is
  true PPM or the linear variation.
-----------------------------------------------------------------------------*/
    uint x;

    for (x = x0; x <= x1; x++) {
        uint l;

        for (l = 0; l < 3; l++) {
            double val;

            val = (double)row[x][l] / pamP->maxval;
            /* convert to intensity because brightness is not additive: */
            if (!linear)
                val = pm_ungamma709(val);
            dataP->color[l] += val;
        }
        dataP->area++;
    }
}



static void
processRow(tuple *      const   row,
           uint         const   y,
           struct pam * const   pamP,
           CmdlineInfo  const * cmdLineP,
           RegData *    const   regSamples) {
/*----------------------------------------------------------------------------
  Reads a row from image <pamP> into allocated tuple array <row>, and updates
  region samples <regSamples[]> from it.  <y> is the position of the row.
-----------------------------------------------------------------------------*/
    uint r;

    pnm_readpamrow(pamP, row);
    for (r = 0; r < cmdLineP->regN; r++) {
        RegSpec   spec;
        RegData * dataP;
        uint      yd, xd, xd2;
        int       x0, x1;

        spec  = cmdLineP->regSpecs[r];
        dataP = &regSamples[r];
        yd    = spec.y - y;
        if (abs(yd) > cmdLineP->radius)
            continue; /* to avoid the slow root operation when possible */
        xd2 = sqri(cmdLineP->radius) - sqri(yd);
        xd = (int)(sqrt((double)xd2) + 0.5);
        x0 = spec.x - xd;
        x1 = spec.x + xd;

        /* clip horisontal chord to image boundaries: */
        if (x0 < 0)
            x0 = 0;
        if (x1 >= pamP->width)
            x1 = pamP->width - 1;

        readChord(dataP, cmdLineP->linear, pamP, row, x0, x1);
    }
}



static RegData *
getColors(struct pam * const pamP,
          CmdlineInfo  const cmdLine) {
/*----------------------------------------------------------------------------
  Scans image <pamP> and collects color data for the regions.
-----------------------------------------------------------------------------*/
    uint      y, ymax;
    RegData * samples;
    tuple *   row;
    FILE *    inFile;

    inFile = pm_openr(cmdLine.infile);
    pnm_readpaminit(inFile, pamP, PAM_STRUCT_SIZE(tuple_type));

    ymax = getYmax( pamP, cmdLine );

    samples = allocRegSamples( cmdLine.regN );
    row     = pnm_allocpamrow(pamP);
    y       = 0;
    for (y = 0; y <= ymax; y++)
        processRow( row, y, pamP, &cmdLine, samples );

    pnm_freepamrow(row);
    pm_close(inFile);
    return samples;
}



static char const *
formatColor(RegData      const data,
            CmdlineInfo  const cmdLine,
            struct pam * const pamP,
            tuple        const tup) {
/*----------------------------------------------------------------------------
  Format the color of region sample <data> according to the format specified
  in <cmdLine>.  The image <pamP> and tuple <tup> are required by the Netpbm
  formatting functions.
-----------------------------------------------------------------------------*/
    uint l;

    for (l = 0; l < 3; l++)
        tup[l] = pm_gamma709(data.color[l]/data.area) * pamP->maxval;

    return formats[cmdLine.formatId].
        formatColor(pamP, tup, cmdLine.formatArg);
}



static void
printColors(struct pam * const pamP,
            CmdlineInfo  const cmdLine,
            FILE *       const outChan,
            RegData      const regSamples[]) {
/*----------------------------------------------------------------------------
  Prints the colors or <regSamples> to channel <outChan> in the format
  specified in <cmdLine>. <pamP> is required by the formatting function.
-----------------------------------------------------------------------------*/
    char  fmt[20];
    uint  r;
    tuple tup;

    tup = pnm_allocpamtuple(pamP);
    sprintf(fmt, "%%%is: %%s\n", cmdLine.maxLbLen);
    for (r = 0; r < cmdLine.regN; r++) {
        RegSpec      spec;
        RegData      data;
        char const * color;

        spec  = cmdLine.regSpecs[r];
        data  = regSamples[r];
        color = formatColor( data, cmdLine, pamP, tup );
        fprintf(outChan, fmt, spec.label, color);
        pm_strfree(color);
    }
    pnm_freepamtuple(tup);
}



int
main(int argc, char const *argv[]) {

    RegData *   regSamples;
    CmdlineInfo cmdLine;
    struct pam  pam;

    pm_proginit(&argc, argv);

    cmdLine    = parseCommandLine(argc, argv);

    regSamples = getColors(&pam, cmdLine);

    printColors(&pam, cmdLine, stdout, regSamples);

    free(cmdLine.regSpecs); /* Asymmetrical: maybe write freeCommandLine() ? */
    free(regSamples);

    return 0;
}



