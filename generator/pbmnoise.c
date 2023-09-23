/* pbmnoise.c - create a random bitmap of a specified size
                with a specified ratio of white/black pixels

   Written by Akira F Urushibata and contributed to the public domain
   December 2021
*/

#include <math.h>
#include <string.h>
#include <assert.h>

#include "pm_c_util.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "rand.h"
#include "nstring.h"

#include "pbm.h"



static void
parseFraction(const char *   const fraction,
              unsigned int * const numeratorP,
              unsigned int * const precisionP) {

    unsigned int numerator, denominator;

    sscanf(fraction, "%u/%u", &numerator, &denominator);

    if (denominator > 65536)
        pm_error("Denominator (%u) too large.", denominator);
    else if (denominator == 0 || (denominator & (denominator - 1)) != 0)
        pm_error("Denominator must be a power of two.  You specified %u.",
                 denominator);
    else if (numerator > denominator)
        pm_error("Invalid fraction (%s).  Denominator must be larger than "
                 "numerator.", fraction);
    else {
        /* Reduce fraction to lowest terms */
        unsigned int numerator2, denominator2;
            /* The fraction reduced to lowest terms */
        unsigned int precision2;

        if (numerator == 0) { /* all white image */
            numerator2   = 0;
            denominator2 = 1;
            precision2   = 0;
        } else if (numerator == denominator) { /* all black */
            numerator2   = 1;
            denominator2 = 1;
            precision2   = 0;
        } else {
            numerator2   = numerator;   /* initial value */
            denominator2 = denominator; /* initial value */

            while ((numerator2 & 0x01) != 0x01) {
                denominator2 /= 2;
                numerator2   /= 2;
            }
            precision2 = 1;
        }

      if (denominator != denominator2)
          pm_message("Ratio %u/%u = %u/%u",
                     numerator, denominator, numerator2, denominator2);

      *precisionP = (precision2 == 0) ? 0 : pm_maxvaltobits(denominator2 - 1);
          /* pm_maxvaltobits(N):  Max of N is 65535 */

      *numeratorP = numerator2;
    }
}



static void
setRatio(const char *   const ratioArg,
         unsigned int * const numeratorP,
         unsigned int * const precisionP) {
/*----------------------------------------------------------------------------
    Convert string "ratioArg" to ratio: numerator / (2 ^ precision) The input
    string must be in fraction "n/d" form and the denominator must be a power
    of 2.

    Ratio is the probability of one binary digit being "1".  The ratio of "1"
    (=PBM black) pixels in the entire output image will be close to this
    value.

    Most invalid strings are rejected here.
----------------------------------------------------------------------------*/
    if (strspn(ratioArg, "0123456789/") == strlen(ratioArg) &&
             ratioArg[0] != '/' &&
             ratioArg[strlen(ratioArg) - 1] != '/' &&
             strchr(ratioArg, '/') != NULL &&
             strchr(ratioArg, '/') == strrchr(ratioArg, '/'))
        parseFraction(ratioArg, numeratorP, precisionP);
    else
        pm_error("Invalid ratio: '%s'", ratioArg);
}



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    unsigned int width;
    unsigned int height;
    unsigned int numerator;
    unsigned int precision;
    unsigned int randomseed;
    unsigned int randomseedSpec;
    unsigned int bswap;   /* boolean */
    unsigned int pack;    /* boolean */
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry *option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;
    const char * ratioArg;
    unsigned int ratioSpec;
    const char * endianArg;
    unsigned int endianSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "ratio",         OPT_STRING, &ratioArg,
            &ratioSpec,                     0);
    OPTENT3(0, "randomseed",    OPT_UINT,   &cmdlineP->randomseed,
            &cmdlineP->randomseedSpec,      0);
    OPTENT3(0, "endian",        OPT_STRING, &endianArg,
            &endianSpec,                    0);
    OPTENT3(0, "pack",          OPT_FLAG,   NULL,
            &cmdlineP->pack,                0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We may have parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
    /* Uses and sets argc, argv, and some of *cmdlineP and others. */
    free(option_def);

    if (ratioSpec)
        setRatio(ratioArg, &cmdlineP->numerator, &cmdlineP->precision);
    else {
        /* Set ratio to default: 1/2 */
        cmdlineP->numerator = 1;
        cmdlineP->precision = 1;
    }

    if (!endianSpec)
        cmdlineP->bswap = false;
    else {
        if      (streq(endianArg, "native"))
            cmdlineP->bswap = false;
        else if (streq(endianArg, "swap"))
            cmdlineP->bswap = true;
        else if (streq(endianArg, "big"))
            cmdlineP->bswap = (BYTE_ORDER == LITTLE_ENDIAN);
        else if (streq(endianArg, "little"))
            cmdlineP->bswap = (BYTE_ORDER != LITTLE_ENDIAN);
        else
            pm_error("Invalid value '%s' for -endian argument.", endianArg);
    }

    if (argc-1 != 2)
        pm_error("Wrong number of arguments (%d).  There are two "
                 "non-option arguments: width and height in pixels",
                 argc-1);
    else {
        cmdlineP->width  = pm_parse_width (argv[1]);
        cmdlineP->height = pm_parse_height(argv[2]);
    }
}



static void
writeSingleColorRaster(unsigned int const cols,
                       unsigned int const rows,
                       bit          const color,
                       FILE *       const ofP) {
/*-----------------------------------------------------------------------------
  Generate a single-color raster of color 'color', dimensions
  'cols' by 'rows', to output file *ofP.
-----------------------------------------------------------------------------*/
    unsigned int const lastColChar = (cols - 1) / 8;

    unsigned char * bitrow0;
    unsigned int i;

    bitrow0 = pbm_allocrow_packed(cols + 32);

    for (i = 0; i <= lastColChar; ++i)
        bitrow0[i] = color * 0xff;

    if (color != 0)
        pbm_cleanrowend_packed(bitrow0, cols);

    /* row end trimming, not necessary with white */

    {
        unsigned int row;
        for (row = 0; row < rows; ++row)
            pbm_writepbmrow_packed(ofP, bitrow0, cols, 0);
    }
    pbm_freerow(bitrow0);
}



static uint32_t
randombits(unsigned int       const precision,
           unsigned int       const numerator,
           struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
  Generate 32 random bits so that for each bit the probability of "1"
  being generated is numerator / (2 ^ precision).

  How this works:

  Ratios such as 1/8, 7/8, 1/16, 15/16, 1/32, 31/32 are straightforward.  How
  do you get intermediate values such as 3/8, 5/8, 3/16, 5/16, 7/16?

  Imagine a set of 10 bits which are 90% 1, 10% 0 and a random number source
  which produces 1 and 0 in even proportions.

  Conduct "and" and "or" on these bits:

           0011111111 (90%)       0011111111 (90%)
      and) 0101010101 (50%)   or) 0101010101 (50%)
      ---------------------   --------------------
           0001010101 (45%)       0111111111 (95%)

  It can be seen from this example that an "and" operation gives a new ratio
  which is halfway between the old ratio (90% in this example) and 0%, while
  "or" gives one at the middle of the old ratio and 100% The corresponding
  binary operations for fixed-point fractions are: "right-shift by one and
  insert a 0 behind the fixed point" and "right-shift by one and insert a 1
  behind the fixed point" respecatbly.

  115/128 = 89.84% (near 90%)  In binary fix-point: 0.1110011
  0.01110011 = 115/256 = 44.92%
  0.11110011 = 243/256 = 94.92%

  So to achieve the desired ratio, start at the LSB (right end) of
  'numerator'.  Initialize the output bits to zero.  Conduct an "and" for each
  0 and an "or" for each 1 with a freshly drawn random number until the fixed
  point is reached.

  An "and" operation of a random number and zero always yields zero.  To avoid
  waste, we reduce terms to eliminate the trailing zeroes in 'numerator' and
  indicate the fixed point with 'precision'.  Each time the program is
  executed the location of the fixed point is set anew, but it stays constant
  until the program exits.
----------------------------------------------------------------------------*/
    unsigned int i;
    uint32_t mask;
    uint32_t retval;

    for (i = 0, mask = 0x01, retval=0x00000000;
         i < precision;
         ++i, mask <<= 1) {

        uint32_t const randval = pm_rand32(randStP);

        if ((numerator & mask) != 0 )
            retval |= randval;
        else
            retval &= randval;
    }
    return retval;
}



static uint32_t
swapWord(uint32_t const word) {
/*----------------------------------------------------------------------------
  Swap four bytes.
  This swap method works regardless of native system endianness.
----------------------------------------------------------------------------*/
    uint32_t const retval =
        ((word >> 24) & 0xff) |
        ((word >>  8) & 0xff00) |
        ((word <<  8) & 0xff0000) |
        ((word << 24) & 0xff000000)
        ;

    return retval;
}



static void
swapBitrow(unsigned char * const bitrow,
           unsigned int    const words,
           bool            const bswap) {
/*----------------------------------------------------------------------------
  Modify bits in 'bitrow', swapping as indicated.
----------------------------------------------------------------------------*/
    uint32_t * const bitrowByWord = (uint32_t *) bitrow;

    unsigned int wordCnt;

    for (wordCnt=0; wordCnt < words; ++wordCnt) {
        uint32_t const inWord = bitrowByWord[wordCnt];

        bitrowByWord[wordCnt] = bswap ? swapWord(inWord) : inWord;
    }
}



static void
pbmnoise(FILE *             const ofP,
         unsigned int       const cols,
         unsigned int       const rows,
         unsigned int       const numerator,
         unsigned int       const precision,
         bool               const bswap,
         struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
  Default method of constructing rows.

  Generate pixels in units of 32 bits.

  If cols is not a multiple of 32, discard pixels beyond row end.
-----------------------------------------------------------------------------*/
    unsigned int const words = (cols + 31) / 32;

    unsigned char * bitrow;
    unsigned int row;
    unsigned int wordCnt;

    bitrow = pbm_allocrow_packed(cols + 32);

    for (row = 0; row < rows; ++row) {
        uint32_t * const bitrowByWord = (uint32_t *) bitrow;

        for (wordCnt = 0; wordCnt < words; ++wordCnt)
            bitrowByWord[wordCnt] = randombits(precision, numerator, randStP);

        if (bswap)
            swapBitrow(bitrow, words, bswap);

        pbm_cleanrowend_packed(bitrow, cols);
        pbm_writepbmrow_packed(ofP, bitrow, cols, 0);
    }
    pbm_freerow(bitrow);
}



static void
pbmnoise_packed(FILE *             const ofP,
                unsigned int       const cols,
                unsigned int       const rows,
                unsigned int       const numerator,
                unsigned int       const precision,
                bool               const bswap,
                struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
  Alternate method of constructing rows.
  Like the default pbmnoise(), generate pixels in units of 32 bits
  but carry over unused pixel data at row end to the next row.
-----------------------------------------------------------------------------*/
    unsigned char * bitrow0;
    uint32_t * bitrowByWord;
    unsigned int offset;
    unsigned int row;
    uint32_t wordSave;    /* Pixels carried over to next row */

    bitrow0 = pbm_allocrow_packed(cols + 63);
    bitrowByWord = (uint32_t *) bitrow0;

    for (row = 0, offset = 0; row < rows; ++row) {
        if (offset == 0) {
            unsigned int const words = (cols + 31 ) / 32;

            unsigned int wordCnt;

            for (wordCnt = 0; wordCnt< words; ++wordCnt) {
                bitrowByWord[wordCnt] =
                    randombits(precision, numerator, randStP);
            }

            if (bswap)
                swapBitrow(bitrow0, words, bswap);

            wordSave = bitrowByWord[words - 1];

            pbm_cleanrowend_packed(bitrow0, cols);
            pbm_writepbmrow_packed(ofP, bitrow0, cols, 0);
            offset = cols % 32;
        } else {
            unsigned int const wordsToFetch = (cols - (32 - offset) + 31) / 32;
            unsigned int const lastWord = wordsToFetch;

            unsigned int wordCnt;

            bitrowByWord[0] = wordSave;

            for (wordCnt = 0; wordCnt < wordsToFetch; ++wordCnt) {
                bitrowByWord[wordCnt + 1] =
                    randombits(precision, numerator, randStP);
            }

            if (bswap)
                swapBitrow((unsigned char *) & bitrowByWord[1],
                           wordsToFetch, bswap);

            wordSave = bitrowByWord [lastWord];

            pbm_writepbmrow_bitoffset(ofP, bitrow0, cols, 0, offset);
            offset = (offset + cols) % 32;
        }
    }
    pbm_freerow(bitrow0);
}


int
main(int argc, const char *argv[]) {

    struct CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    pbm_writepbminit(stdout, cmdline.width, cmdline.height, 0);

    if (cmdline.precision == 0) {
        bit color;

        if (cmdline.numerator == 0)
            color = PBM_WHITE;
        else {
            assert (cmdline.numerator == 1);
            color = PBM_BLACK;
        }
        writeSingleColorRaster(cmdline.width, cmdline.height, color, stdout);
    } else if (cmdline.width % 32 == 0 || !cmdline.pack) {
        struct pm_randSt randSt;

        pm_randinit(&randSt);
        pm_srand2(&randSt, cmdline.randomseedSpec, cmdline.randomseed);

        pbmnoise(stdout, cmdline.width, cmdline.height,
                 cmdline.numerator, cmdline.precision,
                 cmdline.bswap, &randSt);

        pm_randterm(&randSt);
    } else {
        struct pm_randSt randSt;

        pm_randinit(&randSt);
        pm_srand2(&randSt, cmdline.randomseedSpec, cmdline.randomseed);

        pbmnoise_packed(stdout, cmdline.width, cmdline.height,
                        cmdline.numerator, cmdline.precision,
                        cmdline.bswap, &randSt);

        pm_randterm(&randSt);
    }
    return 0;
}


