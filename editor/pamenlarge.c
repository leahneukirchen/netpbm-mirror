/*=============================================================================
                             pamenlarge
===============================================================================
  By Bryan Henderson 2004.09.26.  Contributed to the public domain by its
  author.

  The design and code for the fast processing of PBMs is by Akira Urushibata
  in March 2010 and substantially improved in February 2019.
=============================================================================*/

#include <stdbool.h>
#include <assert.h>

#include "netpbm/mallocvar.h"
#include "netpbm/pm_c_util.h"
#include "netpbm/pam.h"
#include "netpbm/pbm.h"
#include "netpbm/shhopt.h"
#include "netpbm/nstring.h"


struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFilespec;
    unsigned int xScaleFactor;
    unsigned int yScaleFactor;
};



static void
parseCommandLine(int                  argc,
                 const char        ** argv,
                 struct CmdlineInfo * cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optStruct3 opt;  /* set by OPTENT3 */
    optEntry * option_def;
    unsigned int option_def_index;

    unsigned int scale;
    unsigned int xscaleSpec;
    unsigned int yscaleSpec;
    unsigned int scaleSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "xscale", OPT_UINT, &cmdlineP->xScaleFactor,  &xscaleSpec, 0);
    OPTENT3(0, "yscale", OPT_UINT, &cmdlineP->yScaleFactor,  &yscaleSpec, 0);
    OPTENT3(0, "scale",  OPT_UINT, &scale,                   &scaleSpec, 0);

    opt.opt_table = option_def;
    opt.short_allowed = false; /* We have some short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (scaleSpec && scale == 0)
        pm_error("-scale must be positive.  You specified zero");

    if (xscaleSpec && cmdlineP->xScaleFactor == 0)
        pm_error("-xscale must be positive.  You specified zero");

    if (yscaleSpec && cmdlineP->yScaleFactor == 0)
        pm_error("-yscale must be positive.  You specified zero");

    if (scaleSpec && xscaleSpec)
        pm_error("You cannot specify both -scale and -xscale");

    if (scaleSpec && yscaleSpec)
        pm_error("You cannot specify both -scale and -yscale");

    if (scaleSpec) {
        cmdlineP->xScaleFactor = scale;
        cmdlineP->yScaleFactor = scale;
    }

    if (xscaleSpec && !yscaleSpec)
        cmdlineP->yScaleFactor = 1;

    if (yscaleSpec && !xscaleSpec)
        cmdlineP->xScaleFactor = 1;

    if (scaleSpec || xscaleSpec || yscaleSpec) {
        /* Scale options specified.  Naked scale argument not allowed */

        if ((argc-1) > 1)
            pm_error("Too many arguments (%u).  With a scale option, "
                     "the only argument is the "
                     "optional file specification", argc-1);

        if (argc-1 > 0)
            cmdlineP->inputFilespec = argv[1];
        else
            cmdlineP->inputFilespec = "-";
    } else {
        /* scale must be specified in an argument */
        if ((argc-1) != 1 && (argc-1) != 2)
            pm_error("Wrong number of arguments (%d).  Without scale options, "
                     "you must supply 1 or 2 arguments:  scale and "
                     "optional file specification", argc-1);

        {
            const char * error;   /* error message of pm_string_to_uint */
            unsigned int scale;

            pm_string_to_uint(argv[1], &scale, &error);

            if (error == NULL) {
                if (scale == 0)
                    pm_error("Scale argument must be positive.  "
                             "You specified zero");
                else
                    cmdlineP->xScaleFactor = cmdlineP->yScaleFactor = scale;
            } else
                pm_error("Invalid scale factor: %s", error);

        }
        if (argc-1 > 1)
            cmdlineP->inputFilespec = argv[2];
        else
            cmdlineP->inputFilespec = "-";
    }
    free(option_def);
}



static void
makeOutputRowMap(tuple **     const outTupleRowP,
                 struct pam * const outpamP,
                 struct pam * const inpamP,
                 tuple *      const inTuplerow) {
/*----------------------------------------------------------------------------
   Create a tuple *outTupleRowP which is actually a row of pointers into
   inTupleRow[], so as to map input pixels to output pixels by stretching.
-----------------------------------------------------------------------------*/
    tuple * newtuplerow;
    int col;

    MALLOCARRAY_NOFAIL(newtuplerow, outpamP->width);

    for (col = 0 ; col < inpamP->width; ++col) {
        unsigned int const scaleFactor = outpamP->width / inpamP->width;
        unsigned int subcol;

        for (subcol = 0; subcol < scaleFactor; ++subcol)
            newtuplerow[col * scaleFactor + subcol] = inTuplerow[col];
    }
    *outTupleRowP = newtuplerow;
}



static void
validateComputableDimensions(unsigned int const width,
                             unsigned int const height,
                             unsigned int const xScaleFactor,
                             unsigned int const yScaleFactor) {
/*----------------------------------------------------------------------------
   Make sure that multiplication for output image width and height do not
   overflow.

   See validateComputetableSize() in libpam.c and pbm_readpbminitrest() in
   libpbm2.c
-----------------------------------------------------------------------------*/
    unsigned int const maxWidthHeight = INT_MAX - 2;
    unsigned int const maxScaleFactor = maxWidthHeight / MAX(height, width);
    unsigned int const greaterScaleFactor = MAX(xScaleFactor, yScaleFactor);

    if (greaterScaleFactor > maxScaleFactor)
        pm_error("Scale factor '%u' too large.  "
                 "The maximum for this %u x %u input image is %u.",
                 greaterScaleFactor, width, height, maxScaleFactor);
}


static unsigned char const pair[7][4] = {
    { 0x00 , 0x7F , 0x80 , 0xFF},
    { 0x00 , 0x3F , 0xC0 , 0xFF},
    { 0x00 , 0x1F , 0xE0 , 0xFF},
    { 0x00 , 0x0F , 0xF0 , 0xFF},
    { 0x00 , 0x07 , 0xF8 , 0xFF},
    { 0x00 , 0x03 , 0xFC , 0xFF},
    { 0x00 , 0x01 , 0xFE , 0xFF} };



static void
enlargePbmRowHorizontallySmall(const unsigned char * const inrow,
                               unsigned int          const inColChars,
                               unsigned int          const xScaleFactor,
                               unsigned char *       const outrow) {
/*----------------------------------------------------------------------------
   Fast routines for scale factors 1-13.

   Using a temp value "inrowChar" makes a difference.  We know that inrow
   and outrow don't overlap, but the compiler does not and emits code
   which reads inrow[colChar] each time fearing that a write to outrow[x]
   may have altered the value.  (The first "const" for inrow in the above
   argument list is not enough for the compiler.)
-----------------------------------------------------------------------------*/

    static unsigned char const dbl[16] = {
        0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F,
        0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF };

    static unsigned char const trp1[8] = {
        0x00, 0x03, 0x1C, 0x1F, 0xE0, 0xE3, 0xFC, 0xFF };

    static unsigned char const trp2[16] = {
        0x00, 0x01, 0x0E, 0x0F, 0x70, 0x71, 0x7E, 0x7F,
        0x80, 0x81, 0x8E, 0x8F, 0xF0, 0xF1, 0xFE, 0xFF };

    static unsigned char const trp3[8] = {
        0x00, 0x07, 0x38, 0x3F, 0xC0, 0xC7, 0xF8, 0xFF };

    static unsigned char const quin2[8] = {
        0x00, 0x01, 0x3E, 0x3F, 0xC0, 0xC1, 0xFE, 0xFF };

    static unsigned char const quin4[8] = {
        0x00, 0x03, 0x7C, 0x7F, 0x80, 0x83, 0xFC, 0xFF };

    static unsigned char const * quad = pair[3];

    unsigned int colChar;

    switch (xScaleFactor) {
    case 1:  break; /* outrow set to inrow */

    case 2:  /* Make outrow using prefabricated parts (same for 3, 5). */
        for (colChar = 0; colChar < inColChars; ++colChar) {
            unsigned char const inrowChar = inrow[colChar];
            outrow[colChar*2]   = dbl[ inrowChar >> 4];
            outrow[colChar*2+1] = dbl[(inrowChar & 0x0F) >> 0];
            /* Possible outrow overrun by one byte. */
        }
        break;

    case 3:
        for (colChar = 0; colChar < inColChars; ++colChar) {
            unsigned char const inrowChar = inrow[colChar];
            outrow[colChar*3]   = trp1[ inrowChar >> 5];
            outrow[colChar*3+1] = trp2[(inrowChar >> 2) & 0x0F];
            outrow[colChar*3+2] = trp3[(inrowChar >> 0) & 0x07];
        }
        break;

    case 4:
        for (colChar = 0; colChar < inColChars; ++colChar) {
            unsigned char const inrowChar = inrow[colChar];
            unsigned int i;
            for (i = 0; i < 4; ++i)
                outrow[colChar*4+i] =
                    quad[(inrowChar >> (6 - 2 * i)) & 0x03];
        }
        break;

    case 5:
        for (colChar = 0; colChar < inColChars; ++colChar) {
            unsigned char const inrowChar = inrow[colChar];
            outrow[colChar*5]   = pair [4][(inrowChar >> 6) & 0x03];
            outrow[colChar*5+1] = quin2[(inrowChar >> 4) & 0x07] >> 0;
            outrow[colChar*5+2] = quad [(inrowChar >> 3) & 0x03] >> 0;
            outrow[colChar*5+3] = quin4[(inrowChar >> 1) & 0x07] >> 0;
            outrow[colChar*5+4] = pair [2][(inrowChar >> 0) & 0x03];
        }
        break;

    case 6:  /* Compound of 2 and 3 */
        for (colChar = 0; colChar < inColChars; ++colChar) {
            unsigned char const inrowChar = inrow[colChar];
            unsigned char const hi = dbl[(inrowChar & 0xF0) >> 4];
            unsigned char const lo = dbl[(inrowChar & 0x0F) >> 0];

            outrow[colChar*6]   = trp1[hi >> 5];
            outrow[colChar*6+1] = trp2[(hi >> 2) & 0x0F];
            outrow[colChar*6+2] = trp3[hi & 0x07];

            outrow[colChar*6+3] = trp1[lo >> 5];
            outrow[colChar*6+4] = trp2[(lo >> 2) & 0x0F];
            outrow[colChar*6+5] = trp3[lo & 0x07];
        }
        break;

    case 7:
        /* This approach can be used for other scale values.
           Good for architectures which provide wide registers
           such as SSE.
        */
        for (colChar = 0; colChar < inColChars; ++colChar) {
            unsigned char const inrowChar = inrow[colChar];
            uint32_t hi, lo;

            hi = inrowChar >> 4;
            hi = ((((hi>>1) * 0x00082080) | (0x01 & hi)) & 0x00204081 ) * 0x7F;
            hi >>= 4;
            outrow[colChar*7]   =  (unsigned char) ( hi >> 16);
            outrow[colChar*7+1] =  (unsigned char) ((hi >>  8) & 0xFF);
            outrow[colChar*7+2] =  (unsigned char) ((hi >>  0) & 0xFF);

            lo = inrowChar & 0x001F;
            lo = ((((lo>>1) * 0x02082080) | (0x01 & lo)) & 0x10204081 ) * 0x7F;
            outrow[colChar*7+3] =  (unsigned char) ((lo >> 24) & 0xFF);
            outrow[colChar*7+4] =  (unsigned char) ((lo >> 16) & 0xFF);
            outrow[colChar*7+5] =  (unsigned char) ((lo >>  8) & 0xFF);
            outrow[colChar*7+6] =  (unsigned char) ((lo >>  0) & 0xFF);
        }
        break;

    case 8:
        for (colChar = 0; colChar < inColChars; ++colChar) {
            unsigned char const inrowChar = inrow[colChar];
            unsigned int i;
            for (i = 0; i < 8; ++i) {
                outrow[colChar*8+i] =
                    ((inrowChar >> (7-i)) & 0x01) *0xFF;
            }
        }
        break;

    case 9:
        for (colChar = 0; colChar < inColChars; ++colChar) {
            unsigned char const inrowChar = inrow[colChar];
            outrow[colChar*9]   =  ((inrowChar >> 7) & 0x01) * 0xFF;
            outrow[colChar*9+1] =  pair[0][(inrowChar >> 6) & 0x03];
            outrow[colChar*9+2] =  pair[1][(inrowChar >> 5) & 0x03];
            outrow[colChar*9+3] =  pair[2][(inrowChar >> 4) & 0x03];
            outrow[colChar*9+4] =  pair[3][(inrowChar >> 3) & 0x03];
            outrow[colChar*9+5] =  pair[4][(inrowChar >> 2) & 0x03];
            outrow[colChar*9+6] =  pair[5][(inrowChar >> 1) & 0x03];
            outrow[colChar*9+7] =  pair[6][(inrowChar >> 0) & 0x03];
            outrow[colChar*9+8] =  (inrowChar & 0x01) * 0xFF;
        }
        break;

    case 10:
        for (colChar = 0; colChar < inColChars; ++colChar) {
            unsigned char const inrowChar = inrow[colChar];
            outrow[colChar*10]   = ((inrowChar >> 7) & 0x01 ) * 0xFF;
            outrow[colChar*10+1] = pair[1][(inrowChar >> 6) & 0x03];
            outrow[colChar*10+2] = quad[(inrowChar >> 5) & 0x03];
            outrow[colChar*10+3] = pair[5][(inrowChar >> 4) & 0x03];
            outrow[colChar*10+4] = ((inrowChar >> 4) & 0x01) * 0xFF;
            outrow[colChar*10+5] = ((inrowChar >> 3) & 0x01) * 0xFF;
            outrow[colChar*10+6] = pair[1][(inrowChar >> 2) & 0x03];
            outrow[colChar*10+7] = quad[(inrowChar >> 1) & 0x03];
            outrow[colChar*10+8] = pair[5][(inrowChar >> 0) & 0x03];
            outrow[colChar*10+9] = ((inrowChar >> 0) & 0x01) * 0xFF;
        }
        break;

    case 11:
        for (colChar = 0; colChar < inColChars; ++colChar) {
            unsigned char const inrowChar = inrow[colChar];
            outrow[colChar*11]   = ((inrowChar >> 7) & 0x01 ) * 0xFF;
            outrow[colChar*11+1] = pair[2][(inrowChar >> 6) & 0x03];
            outrow[colChar*11+2] = pair[5][(inrowChar >> 5) & 0x03];
            outrow[colChar*11+3] = ((inrowChar >> 5) & 0x01) * 0xFF;
            outrow[colChar*11+4] = pair[0][(inrowChar >> 4) & 0x03];
            outrow[colChar*11+5] = quad[(inrowChar >> 3) & 0x03];
            outrow[colChar*11+6] = pair[6][(inrowChar >> 2) & 0x03];
            outrow[colChar*11+7] = ((inrowChar >> 2) & 0x01) * 0xFF;
            outrow[colChar*11+8] = pair[1][(inrowChar >> 1) & 0x03];
            outrow[colChar*11+9] = pair[4][(inrowChar >> 0) & 0x03];
            outrow[colChar*11+10] = ((inrowChar >> 0) & 0x01) * 0xFF;
        }
        break;

    case 12:
        for (colChar = 0; colChar < inColChars; ++colChar) {
            unsigned char const inrowChar = inrow[colChar];
            outrow[colChar*12+ 0] = ((inrowChar >> 7) & 0x01) * 0xFF;
            outrow[colChar*12+ 1] = quad[(inrowChar >> 6) & 0x03];
            outrow[colChar*12+ 2] = ((inrowChar >> 6) & 0x01) * 0xFF;
            outrow[colChar*12+ 3] = ((inrowChar >> 5) & 0x01) * 0xFF;
            outrow[colChar*12+ 4] = quad[(inrowChar >> 4) & 0x03];
            outrow[colChar*12+ 5] = ((inrowChar >> 4) & 0x01) * 0xFF;
            outrow[colChar*12+ 6] = ((inrowChar >> 3) & 0x01) * 0xFF;
            outrow[colChar*12+ 7] = quad[(inrowChar >> 2) & 0x03];
            outrow[colChar*12+ 8] = ((inrowChar >> 2) & 0x01) * 0xFF;
            outrow[colChar*12+ 9] = ((inrowChar >> 1) & 0x01) * 0xFF;
            outrow[colChar*12+10] = quad[(inrowChar >> 0) & 0x03];
            outrow[colChar*12+11] = ((inrowChar >> 0) & 0x01) * 0xFF;
        }
        break;

    case 13:
      /* Math quiz: 13 is the last entry here.
         Is this an arbitrary choice?
         Or is there something which makes 13 necessary?

         If you like working on questions like this you may like
         number/group theory.  However don't expect a straightforward
         answer from a college math textbook.  - afu
      */
         for (colChar = 0; colChar < inColChars; ++colChar) {
            unsigned char const inrowChar = inrow[colChar];
            outrow[colChar*13+ 0] = ((inrowChar >> 7) & 0x01) * 0xFF;
            outrow[colChar*13+ 1] = pair[4][(inrowChar >> 6) & 0x03];
            outrow[colChar*13+ 2] = ((inrowChar >> 6) & 0x01) * 0xFF;
            outrow[colChar*13+ 3] = pair[1][(inrowChar >> 5) & 0x03];
            outrow[colChar*13+ 4] = pair[6][(inrowChar >> 4) & 0x03];
            outrow[colChar*13+ 5] = ((inrowChar >> 4) & 0x01) * 0xFF;
            outrow[colChar*13+ 6] = quad[(inrowChar >> 3) & 0x03];
            outrow[colChar*13+ 7] = ((inrowChar >> 3) & 0x01) * 0xFF;
            outrow[colChar*13+ 8] = pair[0][(inrowChar >> 2) & 0x03];
            outrow[colChar*13+ 9] = pair[5][(inrowChar >> 1) & 0x03];
            outrow[colChar*13+10] = ((inrowChar >> 1) & 0x01) * 0xFF;
            outrow[colChar*13+11] = pair[2][(inrowChar >> 0) & 0x03];
            outrow[colChar*13+12] = ((inrowChar >> 0) & 0x01) * 0xFF;
        }
        break;

    default:
        pm_error("Internal error");
    }
}


/*
  General method for scale values 14 and above

  First notice that all output characters are either entirely 0, entirely 1
  or a combination with the change from 0->1 or 1->0 happening only once.
  (Sequences like 00111000 never appear when scale value is above 8).

  Let us call the chars which are entirely 0 or 1 "solid" and those which
  may potentially contain both "transitional".   For scale values 6 - 14
  each input character expands to output characters aligned as follows:

  6 : TTTTTT
  7 : TTTTTTT
  8 : SSSSSSSS
  9 : STTTTTTTS
  10: STSTSSTSTS
  11: STTSTTTSTTS
  12: STSSTSSTSSTS
  13: STSTTSTSTTSTS
  14: STSTSTSSTSTSTS

  Above 15 we insert strings of solid chars as necessary:

  22: SsTSsTSsTSsSsTSsTSsTSs
  30: SssTSssTSssTSssSssTSssTSssTSss
  38: SsssTSsssTSsssTSsssSsssTSsssTSsssTSsss
*/


struct OffsetInit {
  unsigned int scale;
  const char * alignment;
};


static struct OffsetInit const offsetInit[8] = {
  /* 0: single char copied from output of enlargePbmRowHorizontallySmall()
     1: stretch of solid chars

     Each entry is symmetrical left-right and has exactly eight '2's
   */

  {  8, "22222222" },               /* 8n+0 */
  {  9, "21121212121212112" },      /* 8n+1 */
  { 10, "211212112211212112" },     /* 8n+2 */
  { 11, "2112121121211212112" },    /* 8n+3 */
  {  4, "212212212212" },           /* 8n+4 */
  { 13, "211211211212112112112" },  /* 8n+5 */
  {  6, "21212122121212" },         /* 8n+6 */
  {  7, "212121212121212" }         /* 8n+7 */
};

  /*   Relationship between 'S' 'T' in previous comment and '1' '2' here

     11: STTSTTTSTTS
     19: sSTsTsSTsTsTSsTsTSs
         2112121121211212112           # table entry for 8n+3
     27: ssSTssTssSTssTssTSssTssTSss
         2*112*12*112*12*112*12*112*
     35: sssSTsssTsssSTsssTsssTSsssTsssTSsss
         2**112**12**112**12**112**12**112**
     42: ssssSTssssTssssSTssssTssssTSssssTssssTSssss
         2***112***12***112***12***112***12***112***
  */


struct OffsetTable {
    unsigned int offsetSolid[8];
    unsigned int offsetTrans[13];
    unsigned int scale;
    unsigned int solidChars;
};



static void
setupOffsetTable(unsigned int         const xScaleFactor,
                 struct OffsetTable * const offsetTableP) {

    unsigned int i, j0, j1, dest;
    struct OffsetInit const classEntry = offsetInit[xScaleFactor % 8];
    unsigned int const scale = classEntry.scale;
    unsigned int const solidChars = xScaleFactor / 8 - (scale > 8 ? 1 : 0);

    for (i = j0 = j1 = dest = 0; classEntry.alignment[i] != '\0'; ++i) {
      switch (classEntry.alignment[i]) {
        case '1': offsetTableP->offsetTrans[j0++] = dest++;
                  break;

        case '2': offsetTableP->offsetSolid[j1++] = dest;
                  dest += solidChars;
                  break;

        default:  pm_error("Internal error. Abnormal alignment value");
        }
    }

    offsetTableP->scale = scale;
    offsetTableP->solidChars = solidChars;
}



static void
enlargePbmRowFractional(unsigned char         const inrowChar,
                        unsigned int          const outColChars,
                        unsigned int          const xScaleFactor,
                        unsigned char       * const outrow,
                        struct OffsetTable  * const tableP) {

/*----------------------------------------------------------------------------
  Routine called from enlargePbmRowHorizontallyGen() to process the final
  fractional inrow char.

  outrow : write position for this function (not left edge of entire row)
----------------------------------------------------------------------------*/

    unsigned int i;

    /* Deploy (most) solid chars */

    for (i = 0; i < 7; ++i) {
        unsigned int j;
        unsigned char const bit8 = (inrowChar >> (7 - i) & 0x01) * 0xFF;

        if (tableP->offsetSolid[i] >= outColChars)
            break;
        else
            for (j = 0; j < tableP->solidChars; ++j) {
                outrow[j + tableP->offsetSolid[i]] = bit8;
            }
     }
    /* If scale factor is a multiple of 8 we are done. */

    if (tableP->scale != 8) {
        unsigned char outrowTemp[16];

        enlargePbmRowHorizontallySmall(&inrowChar, 1,
                                       tableP->scale, outrowTemp);

        for (i = 0 ; i < tableP->scale; ++i) {
            unsigned int const offset = tableP->offsetTrans[i];
            if (offset >= outColChars)
                break;
            else
                outrow[offset] = outrowTemp[i];
            }

    }

}



static void
enlargePbmRowHorizontallyGen(const unsigned char * const inrow,
                             unsigned int          const inColChars,
                             unsigned int          const outColChars,
                             unsigned int          const xScaleFactor,
                             unsigned char       * const outrow,
                             struct OffsetTable  * const tableP) {

/*----------------------------------------------------------------------------
  We iterate through inrow.
  Output chars are deployed according to offsetTable.

  All transitional chars and some solid chars are determined by calling
  one the fast routines in enlargePbmRowHorizontallySmall().
----------------------------------------------------------------------------*/
    unsigned int colChar;
    unsigned int const fullColChars =
        inColChars - ((inColChars * xScaleFactor == outColChars) ? 0 : 1);

    for (colChar = 0; colChar < fullColChars; ++colChar) {
        unsigned char const inrowChar = inrow[colChar];
        char bit8[8];
        unsigned int i;

        /* Deploy most solid chars
           Some scale factors yield uneven string lengths: in such
           cases we don't handle the odd solid chars at this point
        */

        for (i = 0; i < 8; ++i)
            bit8[i] = (inrowChar >> (7 - i) & 0x01) * 0xFF;

        for (i = 0; i < tableP->solidChars; ++i) {
            unsigned int base = colChar * xScaleFactor + i;
            outrow[base]              = bit8[0];
            outrow[base + tableP->offsetSolid[1]] = bit8[1];
            outrow[base + tableP->offsetSolid[2]] = bit8[2];
            outrow[base + tableP->offsetSolid[3]] = bit8[3];
            outrow[base + tableP->offsetSolid[4]] = bit8[4];
            outrow[base + tableP->offsetSolid[5]] = bit8[5];
            outrow[base + tableP->offsetSolid[6]] = bit8[6];
            outrow[base + tableP->offsetSolid[7]] = bit8[7];
        }

        /* If scale factor is a multiple of 8 we are done */

        if (tableP->scale != 8) {
            /* Deploy transitional chars and any remaining solid chars */
            unsigned char outrowTemp[16];
            unsigned int base = colChar * xScaleFactor;

            enlargePbmRowHorizontallySmall(&inrowChar, 1,
                                           tableP->scale, outrowTemp);

            /* There are at least 4 valid entries in offsetTrans[] */

            outrow[base + tableP->offsetTrans[0]] = outrowTemp[0];
            outrow[base + tableP->offsetTrans[1]] = outrowTemp[1];
            outrow[base + tableP->offsetTrans[2]] = outrowTemp[2];
            outrow[base + tableP->offsetTrans[3]] = outrowTemp[3];

            for (i = 4; i < tableP->scale; ++i)
                outrow[base + tableP->offsetTrans[i]] = outrowTemp[i];
        }
    }

    /* Process the fractional final inrow byte */

     if (fullColChars < inColChars) {
        unsigned int  const start = fullColChars * xScaleFactor;
        unsigned char const inrowLast = inrow[inColChars -1];

        enlargePbmRowFractional(inrowLast, outColChars - start,
                                xScaleFactor, &outrow[start], tableP);
        }

}



static void
enlargePbm(struct pam * const inpamP,
           unsigned int const xScaleFactor,
           unsigned int const yScaleFactor,
           FILE *       const ofP) {

    enum ScaleMethod {METHOD_USEINPUT, METHOD_SMALL, METHOD_GENERAL};
    enum ScaleMethod const scaleMethod =
        xScaleFactor == 1 ? METHOD_USEINPUT :
        scaleMethod <= 13 ? METHOD_SMALL :
        METHOD_GENERAL;

    unsigned int const outcols = inpamP->width * xScaleFactor;
    unsigned int const outrows = inpamP->height * yScaleFactor;
    unsigned int const inColChars  = pbm_packed_bytes(inpamP->width);
    unsigned int const outColChars = pbm_packed_bytes(outcols);

    unsigned char * inrow;
    unsigned char * outrow;
    unsigned int row;
    struct OffsetTable offsetTable;

    inrow  = pbm_allocrow_packed(inpamP->width);

    if (scaleMethod == METHOD_USEINPUT)
        outrow = inrow;
    else {
        /* Allow writes beyond outrow data end when using the table method.
        */
        unsigned int const rightPadding =
            scaleMethod == METHOD_GENERAL ? 0 : (xScaleFactor - 1) * 8;

        outrow = pbm_allocrow_packed(outcols + rightPadding);

        if (scaleMethod == METHOD_GENERAL)
            setupOffsetTable(xScaleFactor, &offsetTable);
    }

    pbm_writepbminit(ofP, outcols, outrows, 0);

    for (row = 0; row < inpamP->height; ++row) {
        unsigned int i;

        pbm_readpbmrow_packed(inpamP->file, inrow, inpamP->width,
                              inpamP->format);

        if (outcols % 8 > 0)           /* clean final partial byte */
            pbm_cleanrowend_packed(inrow, inpamP->width);

        switch (scaleMethod) {
        case METHOD_USEINPUT:
            /* Nothing to do */
            break;
        case METHOD_SMALL:
            enlargePbmRowHorizontallySmall(inrow, inColChars,
                                           xScaleFactor, outrow);
            break;
        case METHOD_GENERAL:
            enlargePbmRowHorizontallyGen(inrow, inColChars, outColChars,
                                         xScaleFactor, outrow,
                                         &offsetTable);
            break;
        }

        for (i = 0; i < yScaleFactor; ++i)
            pbm_writepbmrow_packed(ofP, outrow, outcols, 0);
    }

    if (outrow != inrow)
        pbm_freerow(outrow);

    pbm_freerow(inrow);
}



static void
enlargeGeneral(struct pam * const inpamP,
               unsigned int const xScaleFactor,
               unsigned int const yScaleFactor,
               FILE *       const ofP) {
/*----------------------------------------------------------------------------
   Enlarge the input image described by *pamP.

   Assume the dimensions won't cause an arithmetic overflow.

   This works on all kinds of images, but is slower than enlargePbm on
   PBM.
-----------------------------------------------------------------------------*/
    struct pam outpam;
    tuple * tuplerow;
    tuple * newtuplerow;
    unsigned int row;

    outpam = *inpamP;
    outpam.file   = ofP;
    outpam.width  = inpamP->width  * xScaleFactor;
    outpam.height = inpamP->height * yScaleFactor;

    pnm_writepaminit(&outpam);

    tuplerow = pnm_allocpamrow(inpamP);

    makeOutputRowMap(&newtuplerow, &outpam, inpamP, tuplerow);

    for (row = 0; row < inpamP->height; ++row) {
        pnm_readpamrow(inpamP, tuplerow);
        pnm_writepamrowmult(&outpam, newtuplerow, yScaleFactor);
    }

    free(newtuplerow);

    pnm_freepamrow(tuplerow);
}



int
main(int           argc,
     const char ** const argv) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    struct pam inpam;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFilespec);

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    assert(cmdline.xScaleFactor > 0);
    assert(cmdline.yScaleFactor > 0);

    validateComputableDimensions(inpam.width, inpam.height,
                                 cmdline.xScaleFactor, cmdline.yScaleFactor);

    if (PNM_FORMAT_TYPE(inpam.format) == PBM_TYPE)
        enlargePbm(&inpam, cmdline.xScaleFactor, cmdline.yScaleFactor,
                   stdout);
    else
        enlargeGeneral(&inpam, cmdline.xScaleFactor, cmdline.yScaleFactor,
                       stdout);

    pm_close(ifP);
    pm_close(stdout);

    return 0;
}


