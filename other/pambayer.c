/*

  Bayer matrix conversion tool

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 2 of the License,
  or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
  USA

  Copyright Alexandre Becoulet <diaxen AT free DOT fr>

  Completely rewritten for Netpbm by Bryan Henderson August 2005.
*/

#include <unistd.h>
#include <stdio.h>

#include "pm_c_util.h"
#include "pam.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "nstring.h"


enum BayerType {
    BAYER1,
    BAYER2,
    BAYER3,
    BAYER4
};

struct CmdlineInfo {
    const char *   inputFilespec;
    enum BayerType bayerType;
    unsigned int   nointerpolate;
    unsigned int   subchannel;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;
    unsigned int typeSpec;
    unsigned int type;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "type",          OPT_UINT, &type,
            &typeSpec,                      0);
    OPTENT3(0, "nointerpolate", OPT_FLAG, NULL,
            &cmdlineP->nointerpolate,       0);
    OPTENT3(0, "subchannel",    OPT_FLAG, NULL,
            &cmdlineP->subchannel,          0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We may have parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 < 1)
        cmdlineP->inputFilespec = "-";
    else if (argc-1 > 1)
        pm_error("There is at most one argument -- the input file.  "
                 "You specified %u", argc-1);
    else
        cmdlineP->inputFilespec = argv[1];

    if (cmdlineP->nointerpolate && cmdlineP->subchannel)
        pm_error("You cannot use -nointerpolate with -subchannel");

    if (!typeSpec)
        pm_error("You must specify the -type option");
    else {
        switch (type) {
        case 1: cmdlineP->bayerType = BAYER1; break;
        case 2: cmdlineP->bayerType = BAYER2; break;
        case 3: cmdlineP->bayerType = BAYER3; break;
        case 4: cmdlineP->bayerType = BAYER4; break;
        }
    }
}



static void
calc4(const struct pam * const pamP,
      tuple **           const intuples,
      tuple **           const outtuples,
      unsigned int       const plane,
      bool               const noInterpolation,
      unsigned int       const xoffset,
      unsigned int       const yoffset) {
/*----------------------------------------------------------------------------
    X . X
    . . .
    X . X

  For the Plane 'plane' sample values, an even pixel of outtuples[] gets the
  same value as intuples[][].  An odd pixel of outtuples[] gets the mean of
  the four surrounding even pixels, north, south, east, and west.  But zero if
  Caller says 'noInterpolation'.

  (even/odd is with respect to ('xoffset', 'yoffset')).
-----------------------------------------------------------------------------*/
    unsigned int row;

    /* Do the even rows -- the even column pixels get copied from the input,
       while the odd column pixels get the mean of adjacent even ones
    */
    for (row = yoffset; row < pamP->height; row += 2) {
        unsigned int col;
        for (col = xoffset; col + 2 < pamP->width; col += 2) {
            outtuples[row][col][plane] = intuples[row][col][0];
            outtuples[row][col + 1][plane] =
                noInterpolation ?
                0 :
                (intuples[row][col][0] + intuples[row][col + 2][0]) / 2;
        }
    }

    /* Do the odd rows -- every pixel is the mean of the one above and below */
    for (row = yoffset; row + 2 < pamP->height; row += 2) {
        unsigned int col;
        for (col = xoffset; col < pamP->width; ++col) {
            outtuples[row + 1][col][plane] =
                noInterpolation ?
                0 :
                (outtuples[row][col][plane] +
                 outtuples[row + 2][col][plane]) / 2;
        }
    }
}



static void
calc5(const struct pam * const pamP,
      tuple **           const intuples,
      tuple **           const outtuples,
      unsigned int       const plane,
      bool               const noInterpolation,
      unsigned int       const xoffset,
      unsigned int       const yoffset) {
/*----------------------------------------------------------------------------
   . X .
   X . X
   . X .

  For the Plane 'plane' sample values, an pixel on an even diagonal of
  outtuples[] gets the same value as intuples[][].  An pixel on an odd
  diagonal gets the mean of the four surrounding even pixels, north,
  south, east, and west.  But zero if Caller says 'noInterpolation'.

  (even/odd is with respect to ('xoffset', 'yoffset')).
-----------------------------------------------------------------------------*/
    unsigned int row;
    unsigned int j;

    j = 0;  /* initial value */

    for (row = yoffset; row + 2 < pamP->height; ++row) {
        unsigned int col;
        for (col = xoffset + j; col + 2 < pamP->width; col += 2) {
            outtuples[row][col + 1][plane] = intuples[row][col + 1][0];
            outtuples[row + 1][col + 1][plane] =
                noInterpolation ?
                0 :
                (intuples[row][col + 1][0] +
                 intuples[row + 1][col][0] +
                 intuples[row + 2][col + 1][0] +
                 intuples[row + 1][col + 2][0]) / 4;
        }
        j = 1 - j;
    }
}



struct CompAction {
    unsigned int xoffset;
    unsigned int yoffset;
    void (*calc)(const struct pam * const pamP,
                 tuple **           const intuples,
                 tuple **           const outtuples,
                 unsigned int       const plane,
                 bool               const noInterpolation,
                 unsigned int       const xoffset,
                 unsigned int       const yoffset);
};



static struct CompAction const comp1[3] = {
/*----------------------------------------------------------------------------
  G B G B
  R G R G
  G B G B
  R G R G
-----------------------------------------------------------------------------*/

    { 0, 1, calc4 },
    { 0, 1, calc5 },
    { 1, 0, calc4 }
};

static struct CompAction const comp2[3] = {
/*----------------------------------------------------------------------------
  R G R G
  G B G B
  R G R G
  G B G B
-----------------------------------------------------------------------------*/
    { 0, 0, calc4 },
    { 0, 0, calc5 },
    { 1, 1, calc4 }
};

static struct CompAction const comp3[3] = {
/*----------------------------------------------------------------------------
  B G B G
  G R G R
  B G B G
  G R G R
-----------------------------------------------------------------------------*/
    { 1, 1, calc4 },
    { 0, 0, calc5 },
    { 0, 0, calc4 }
};

static struct CompAction const comp4[3] = {
/*----------------------------------------------------------------------------
  G R G R
  B G B G
  G R G R
  B G B G
-----------------------------------------------------------------------------*/
    { 1, 0, calc4 },
    { 0, 1, calc5 },
    { 0, 1, calc4 }
};



static void
makeOutputPam(const struct pam * const inpamP,
              struct pam *       const outpamP,
              bool               const wantSubchannel) {

    outpamP->size   = sizeof(*outpamP);
    outpamP->len    = PAM_STRUCT_SIZE(tuple_type);
    outpamP->file   = stdout;
    outpamP->format = PAM_FORMAT;
    outpamP->plainformat = 0;
    outpamP->maxval = inpamP->maxval;
    outpamP->bytes_per_sample = inpamP->bytes_per_sample;

    if (wantSubchannel) {
        outpamP->width  = inpamP->width/2;
        outpamP->height = inpamP->height/2;
        outpamP->depth  = 1;
        STRSCPY(outpamP->tuple_type, "BAYERSUBCHANNEL");
    } else {
        outpamP->width  = inpamP->width;
        outpamP->height = inpamP->height;
        outpamP->depth  = 3;
        STRSCPY(outpamP->tuple_type, "RGB");
    }
}



struct XyOffset {
/*----------------------------------------------------------------------------
   A two-dimensional offset within a matrix.
-----------------------------------------------------------------------------*/
    unsigned int row;
    unsigned int col;
};



static struct XyOffset
offsetForType(enum BayerType const bayerType) {
/*----------------------------------------------------------------------------
   The offset within a bayer matrix of Type 'bayerType' where you find the

     G B
     R G

   square
-----------------------------------------------------------------------------*/
    struct XyOffset retval;

    switch (bayerType) {
        case BAYER1:
            retval.row = 0;
            retval.col = 0;
            break;
        case BAYER2:
            retval.row = 0;
            retval.col = 1;
            break;
        case BAYER3:
            retval.row = 1;
            retval.col = 0;
            break;
        case BAYER4:
            retval.row = 1;
            retval.col = 1;
            break;
    }
    return retval;
}



static void
calcSubchannel(const struct pam * const pamP,
               tuple **           const intuples,
               tuple **           const outtuples,
               enum BayerType     const bayerType) {

    struct XyOffset const offset = offsetForType(bayerType);

    unsigned int row;

    for (row = offset.row; row < pamP->height; row += 2) {
        unsigned int col;

        for (col = offset.col; col < pamP->width; col += 2) {
            outtuples[row/2][col/2][0] = intuples[row][col][0];
        }
    }
}



static const struct CompAction *
actionTableForType(enum BayerType const bayerType) {

    const struct CompAction * retval;

    switch (bayerType) {
    case BAYER1: retval = comp1; break;
    case BAYER2: retval = comp2; break;
    case BAYER3: retval = comp3; break;
    case BAYER4: retval = comp4; break;
    }
    return retval;
}



static void
calcImage(struct pam *   const inpamP,
          tuple **       const intuples,
          tuple **       const outtuples,
          enum BayerType const bayerType,
          bool           const wantNoInterpolate) {

    const struct CompAction * const compActionTable =
        actionTableForType(bayerType);

    unsigned int plane;

    for (plane = 0; plane < 3; ++plane) {

        struct CompAction const compAction = compActionTable[plane];

        compAction.calc(inpamP, intuples, outtuples, plane,
                        wantNoInterpolate,
                        compAction.xoffset, compAction.yoffset);
    }
}


            int
main(int argc, const char **argv) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    struct pam inpam;
    struct pam outpam;
    tuple ** intuples;
    tuple ** outtuples;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFilespec);

    intuples = pnm_readpam(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    makeOutputPam(&inpam, &outpam, !!cmdline.subchannel);

    outtuples = pnm_allocpamarray(&outpam);

    if (cmdline.subchannel)
        calcSubchannel(&inpam, intuples, outtuples, cmdline.bayerType);
    else
        calcImage(&inpam, intuples, outtuples,cmdline.bayerType,
                  !!cmdline.nointerpolate);

    pnm_writepam(&outpam, outtuples);

    pnm_freepamarray(outtuples, &outpam);
    pnm_freepamarray(intuples, &inpam);

    return 0;
}



