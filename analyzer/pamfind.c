#include <assert.h>
#include <nstring.h>

#include <pam.h>

#include "pm_c_util.h"
#include "shhopt.h"
#include "mallocvar.h"


typedef struct {
    unsigned int * target;
    unsigned int   targetDepth;
    unsigned int   machine;
    const char *   color;  /* NULL means not specified */
    const char *   inputFileName;
} CmdLineInfo;



static CmdLineInfo
parsedCommandLine(int                 argc,
                  const char ** const argv) {

    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    CmdLineInfo cmdLine;

    unsigned int targetSpec, colorSpec;
    const char ** target;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "target",  OPT_STRINGLIST, &target,          &targetSpec, 0);
    OPTENT3(0,   "color",   OPT_STRING,     &cmdLine.color,   &colorSpec,  0);
    OPTENT3(0,   "machine", OPT_FLAG,       NULL,       &cmdLine.machine,  0);
    OPTENT3(0,  0,          OPT_END,        NULL,             NULL,        0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);

    if (targetSpec) {
        if (colorSpec)
            pm_error("You cannot specify both -target and -color");
        else {
            unsigned int i;

            cmdLine.color = NULL;

            cmdLine.target = NULL;  /* initial value */

            for (i = 0, cmdLine.targetDepth = 0; target[i]; ++i) {
                unsigned int sampleVal;
                const char * error;

                pm_string_to_uint(target[i], &sampleVal, &error);
                if (error) {
                    pm_error("Invalid sample value in -target option: '%s'.  "
                             "%s", target[i], error);
                }

                REALLOCARRAY(cmdLine.target, i+1);

                cmdLine.target[cmdLine.targetDepth++] = sampleVal;
            }

            free(target);
        }
    } else if (!colorSpec)
        pm_error("You must specify either -target or -color");

    if (argc-1 < 1)
        cmdLine.inputFileName = "-";
    else {
        cmdLine.inputFileName = argv[1];

        if (argc-1 > 1)
            pm_error("Too many arguments: %u.  "
                     "The only possible argument is the input file name",
                     argc-1);
    }
    free(option_def);

    return cmdLine;
}



static void
freeCmdLine(CmdLineInfo const cmdLine) {

    if (!cmdLine.color)
        pnm_freepamtuple(cmdLine.target);
}



static tuple
targetValue(CmdLineInfo  const cmdLine,
            struct pam * const inpamP) {
/*----------------------------------------------------------------------------
   The tuple value user wants us to find in the image, per 'cmdLine'.

   The return value is to be interpreted per *inpamP.
-----------------------------------------------------------------------------*/
    tuple retval;

    if (cmdLine.color) {
        if (inpamP->depth != 3)
            pm_error("You specified -color, but the input image has "
                     "depth %u, not 3", inpamP->depth);
        else
            retval = pnm_parsecolor(cmdLine.color, inpamP->maxval);
    } else {
        if (cmdLine.targetDepth != inpamP->depth)
            pm_error("You specified a %u-tuple for -target, "
                     "but the input image of of depth %u",
                     cmdLine.targetDepth, inpamP->depth);
        else {
            unsigned int i;

            retval = pnm_allocpamtuple(inpamP);

            for (i = 0; i < inpamP->depth; ++i)
                retval[i] = cmdLine.target[i];
        }
    }

    return retval;
}



static void
printHeader(FILE *       const ofP,
            struct pam * const inpamP,
            tuple        const target) {

    unsigned int plane;

    fprintf(ofP, "Locations containing tuple ");

    fprintf(ofP, "(");

    for (plane = 0; plane < inpamP->depth; ++plane) {
        fprintf(ofP, "%u", (unsigned)target[plane]);
        if (plane + 1 < inpamP->depth)
            fprintf(ofP, "/");
    }

    fprintf(ofP, ")");

    fprintf(ofP, "/%u", (unsigned)inpamP->maxval);

    fprintf(ofP, ":\n");
}



static unsigned int
decimalDigitCt(unsigned int const n) {
/*----------------------------------------------------------------------------
   Minimum number of digits needed to display 'n' in decimal.
-----------------------------------------------------------------------------*/
    unsigned int digitCt;

    if (n == 0)
        digitCt = 1;
    else {
        unsigned int x;

        for (digitCt = 0, x = n; x > 0;) {
            ++digitCt;
            x /= 10;
        }
        assert(digitCt > 0);
    }
    return digitCt;
}



static void
pamfind(FILE *       const ifP,
        struct pam * const inpamP,
        CmdLineInfo  const cmdLine,
        FILE *       const ofP) {

    pnm_readpaminit(ifP, inpamP, PAM_STRUCT_SIZE(tuple_type));

    {
        tuple * const inputRow = pnm_allocpamrow(inpamP);
        tuple   const target   = targetValue(cmdLine, inpamP);

        unsigned int row;
        const char * fmt;

        if (cmdLine.machine) {
            pm_asprintf(&fmt, "%%0%uu %%0%uu\n",
                        decimalDigitCt(inpamP->height-1),
                        decimalDigitCt(inpamP->width-1));
        } else {
            printHeader(ofP, inpamP, target);
            fmt = pm_strdup("(%u, %u)\n");
        }

        for (row = 0; row < inpamP->height; ++row) {
            unsigned int col;

            pnm_readpamrow(inpamP, inputRow);

            for (col = 0; col < inpamP->width; ++col) {

                if (pnm_tupleequal(inpamP, target, inputRow[col])) {
                    fprintf(ofP, fmt, row, col);
                }
            }
        }
        pm_strfree(fmt);
        pnm_freepamtuple(target);
        pnm_freepamrow(inputRow);
    }
}



int
main(int argc, const char *argv[]) {

    FILE * ifP;
    CmdLineInfo cmdLine;
    struct pam  inpam;

    pm_proginit(&argc, argv);

    cmdLine = parsedCommandLine(argc, argv);

    ifP = pm_openr(cmdLine.inputFileName);

    pamfind(ifP, &inpam, cmdLine, stdout);

    freeCmdLine(cmdLine);

    pm_close(inpam.file);

    return 0;
}



