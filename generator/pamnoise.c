/*********************************************************************
   pamnoise -  create PAM with white noise

   by Akira F Urushibata 2026
   Contributed to the public domain by the author

   In October 1993 Frank Neumann wrote pgmnoise.

   Several improvements were made to pgmnoise.  Most notably an efficient
   method for generating random numbers with limited binary digits was
   implemented.  The following code retains none of the original pgmnoise
   code by Frank Neumann.
*********************************************************************/

#include <stdbool.h>
#include <assert.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "rand.h"
#include "shhopt.h"
#include "pam.h"

/* constants */
static unsigned long int const ceil31bits = 0x7fffffffUL;
static unsigned long int const ceil32bits = 0xffffffffUL;



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    unsigned int width;
    unsigned int height;
    unsigned int depth;
    unsigned int maxval;
    const char * tupletype;
    unsigned int randomseed;
    unsigned int randomseedSpec;
    unsigned int verbose;
};



static void
parseCommandLine(int argc,
                 const char **        const argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;
    unsigned int option_def_index;
    unsigned int widthSpec, heightSpec, depthSpec, maxvalSpec, tupletypeSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "width",        OPT_UINT,     &cmdlineP->width,
            &widthSpec,                     0);
    OPTENT3(0,   "height",       OPT_UINT,     &cmdlineP->height,
            &heightSpec,                    0);
    OPTENT3(0,   "depth",        OPT_UINT,     &cmdlineP->depth,
            &depthSpec,                     0);
    OPTENT3(0,   "maxval",       OPT_UINT,     &cmdlineP->maxval,
            &maxvalSpec,                    0);
    OPTENT3(0,   "tupletype",    OPT_STRING,   &cmdlineP->tupletype,
            &tupletypeSpec,                 0);
    OPTENT3(0,   "randomseed",   OPT_UINT,     &cmdlineP->randomseed,
            &cmdlineP->randomseedSpec,      0);
    OPTENT3(0,   "verbose",      OPT_FLAG,     NULL,
            &cmdlineP->verbose,             0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We may have parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */
    free(option_def);

    if (widthSpec) {
        if (argc-1 == 2) {
            pm_error("You cannot specify both -width and "
                     "non-option arguments");
        }
        if (cmdlineP->width < 1)
            pm_error("-width is zero; must be positive");
    } else {
        if (argc-1 == 2) {
            const char * error; /* error message of pm_string_to_uint */

            pm_string_to_uint(argv[1], &cmdlineP->width, &error);
            if (error)
                pm_error("Width argument is not an unsigned integer.  %s",
                         error);
            else if (cmdlineP->width == 0)
                pm_error("Width argument is zero; must be positive");
        } else
            cmdlineP->width = 1;
    }

    if (heightSpec) {
        if (argc-1 == 2) {
            pm_error("You cannot specify both -height and "
                     "non-option arguments");
        }
        if (cmdlineP->height < 1)
            pm_error("-height is zero; must be positive");
    } else {
        if (argc-1 == 2) {
            const char * error; /* error message of pm_string_to_uint */

            pm_string_to_uint(argv[2], &cmdlineP->height, &error);
            if (error)
                pm_error("Height argument is not an unsigned integer.  %s",
                         error);
            else if (cmdlineP->height == 0)
                pm_error("Height argument is zero; must be positive");
        } else
            cmdlineP->height = 1;
    }

    if (depthSpec) {
        if (cmdlineP->depth < 1)
            pm_error("-depth is zero; must be positive");
    } else
        cmdlineP->depth = 1;

    if (maxvalSpec) {
        if (cmdlineP->maxval > PGM_OVERALLMAXVAL)
            pm_error("-maxval too large: %u.  Maximum is %u",
                     cmdlineP->maxval, PGM_OVERALLMAXVAL);
        else if (cmdlineP->maxval == 0)
            pm_error("-maxval must not be zero");
    } else
        cmdlineP->maxval = PGM_MAXMAXVAL;

    if (!tupletypeSpec)
        cmdlineP->tupletype = "";
    else {
        struct pam pam;
        if (strlen(cmdlineP->tupletype)+1 > sizeof(pam.tuple_type))
            pm_error("The tuple type you specified is too long.  "
                     "Maximum %u characters.",
                     (unsigned)sizeof(pam.tuple_type)-1);
    }

    if (argc-1 != 0 && argc-1 != 2) {
        pm_error("Invalid number of non-option arguments: %u.  "
                 "The only possible arguments are width and height, as "
                 "backward compatibility alternataives to "
                 "-width and -height.  "
                 "So specify either zero or two arguments.",
                 argc-1);
    }
}



static unsigned int
randPool(unsigned int       const nDigits,
         struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
  Draw 'nDigits' bits from pool of random bits.  If the number of random bits
  in pool is insufficient, call pm_rand() and add N bits to it.

  N is 31 or 32.  In raw mode we use N = 32 regardless of the actual number of
  available bits.  If there are only 31 available, we use zero for the MSB.

  'nDigits' must be at most 16.

  We assume that each call to pm_rand() generates 31 or 32 bits, or
  randStP->max == 2147483647 or 4294967295.

  The underlying logic is flexible and endian-free.  The above conditions can
  be relaxed.
-----------------------------------------------------------------------------*/
    static unsigned long int hold = 0;  /* entropy pool */
    static unsigned int      len = 0;   /* number of valid bits in pool */

    unsigned int const mask = (1 << nDigits) - 1;
    unsigned int const randbits = (randStP->max == ceil31bits) ? 31 : 32;

    unsigned int retval;

    assert(randStP->max == ceil31bits || randStP->max == ceil32bits);
    assert(nDigits <= 16);

    retval = hold;  /* initial value */

    if (len > nDigits) { /* Enough bits in hold to satisfy request */
        hold >>= nDigits;
        len   -= nDigits;
    } else {            /* Load another 31 or 32 bits into hold */
        hold    = pm_rand(randStP);
        retval |= (hold << len);
        hold >>=  (nDigits - len);
        len = randbits - nDigits + len;
    }
    return (retval & mask);
}



static void
reportVerbose(struct pm_randSt * const randStP,
              gray               const maxval,
              bool               const usingPool)  {

    pm_message("random seed: %u", randStP->seed);
    pm_message("random max: %u maxval: %u", randStP->max, maxval);
    pm_message("method: %s", usingPool ? "pool" : "modulo");
}



static void
pamnoise(FILE *             const ofP,
         unsigned int       const width,
         unsigned int       const height,
         unsigned int       const depth,
         gray               const maxval,
         const char       * const tupletype,
         bool               const verbose,
         struct pm_randSt * const randStP) {

    struct pam pam;

    bool const usingPool =
        (randStP->max==ceil31bits || randStP->max==ceil32bits) &&
        !(maxval & (maxval+1));
    unsigned int const bitLen = pm_maxvaltobits(maxval);

    unsigned int row;
    tuple * tuplerow;

    /* If maxval is 2^n-1, we draw exactly n bits from the pool.
       Otherwise call pm_rand() and determine gray value by modulo.

       In the latter case, there is a minuscule skew toward 0 (=black)
       because smaller numbers are produced more frequently by modulo.
       Thus we employ the pool method only when it is certain that no
       skew will result.

       To illustrate the point, consider converting the outcome of one
       roll of a fair, six-sided die to 5 values (0 to 4) by N % 5.  The
       probability for values 1, 2, 3, 4 is 1/6, but 0 alone is 2/6.
       Average is 10/6 or 1.6667, compared to 2.0 from an ideal
       generator which produces exactly 5 values.  With two dice
       average improves to 70/36 or 1.9444.

       The more (distinct) dice we roll, or the more binary digits we
       draw, the smaller the skew.

       The pool method is economical.  But there is an additional merit:
       No bits are lost this way.  This gives us a means to check the
       integrity of the random number generator.

       - Akira Urushibata, March 2021
    */

    if (verbose)
        reportVerbose(randStP, maxval, usingPool);

    pam.size        = sizeof(pam);
    pam.len         = PAM_STRUCT_SIZE(tuple_type);
    pam.file        = stdout;
    pam.format      = PAM_FORMAT;
    pam.plainformat = 0;
    pam.width       = width;
    pam.height      = height;
    pam.depth       = depth;
    pam.maxval      = maxval;
    strcpy(pam.tuple_type, tupletype);

    pnm_writepaminit(&pam);

    tuplerow = pnm_allocpamrow(&pam);

    for (row = 0; row < height; ++row) {
        unsigned int col;
        for (col = 0; col < width; ++col) {
            unsigned int plane;
            for (plane = 0; plane < depth; ++plane) {
                tuplerow[col][plane] =
                    usingPool ?
                    randPool(bitLen, randStP) :
                    (pm_rand(randStP) % (maxval + 1));
            }
        }
        pnm_writepamrow(&pam, tuplerow);
    }
    pnm_freepamrow(tuplerow);
}



int
main(int         argc,
    const char * argv[]) {

    struct CmdlineInfo cmdline;
    struct pm_randSt   randSt;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    pm_randinit(&randSt);
    pm_srand2(&randSt, cmdline.randomseedSpec, cmdline.randomseed);

    pamnoise(stdout,
             cmdline.width, cmdline.height, cmdline.depth, cmdline.maxval,
             cmdline.tupletype, cmdline.verbose, &randSt);

    pm_randterm(&randSt);

    return 0;
}


