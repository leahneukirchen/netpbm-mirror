/* pbmreduce.c - read a portable bitmap and reduce it N times
**
** Copyright (C) 1989 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include "pm_c_util.h"
#include "pbm.h"
#include "mallocvar.h"
#include "shhopt.h"
#include <assert.h>

#define SCALE 1024
#define HALFSCALE 512


enum Halftone {QT_FS, QT_THRESH};

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char *  inputFilespec;
    enum Halftone halftone;
    int           value;
    unsigned int  randomseed;
    unsigned int  randomseedSpec;
    int           scale;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo *cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;
    unsigned int floydOpt, thresholdOpt;
    unsigned int valueSpec;
    float        value;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "floyd",       OPT_FLAG,  NULL,
            &floydOpt,                      0);
    OPTENT3(0, "fs",          OPT_FLAG,  NULL,
            &floydOpt,                      0);
    OPTENT3(0, "threshold",   OPT_FLAG,  NULL,
            &thresholdOpt,                  0);
    OPTENT3(0, "value",       OPT_FLOAT, &value,
            &valueSpec,                     0);
    OPTENT3(0, "randomseed",  OPT_UINT,  &cmdlineP->randomseed,
            &cmdlineP->randomseedSpec,      0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We may have parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (floydOpt + thresholdOpt == 0)
        cmdlineP->halftone = QT_FS;
    else if (!!floydOpt + !!thresholdOpt > 1)
        pm_error("Cannot specify both floyd and threshold");
    else {
        if (floydOpt)
            cmdlineP->halftone = QT_FS;
        else {
            cmdlineP->halftone = QT_THRESH;
            if (cmdlineP->randomseedSpec)
                pm_message("-randomseed value has no effect with -threshold");
        }
    }

    if (!valueSpec)
        cmdlineP->value = HALFSCALE;
    else {
        if (value < 0.0)
            pm_error("-value cannot be negative.  You specified %f", value);
        if (value > 1.0)
            pm_error("-value cannot be greater than one.  You specified %f",
                     value);
        else
            cmdlineP->value = value * SCALE;
    }

    if (argc-1 > 0) {
        char * endptr;   /* ptr to 1st invalid character in scale arg */
        unsigned int scale;

        scale = strtol(argv[1], &endptr, 10);
        if (*argv[1] == '\0') 
            pm_error("Scale argument is a null string.  Must be a number.");
        else if (*endptr != '\0')
            pm_error("Scale argument contains non-numeric character '%c'.",
                     *endptr);
        else if (scale < 2)
            pm_error("Scale argument must be at least 2.  "
                     "You specified %d", scale);
        else if (scale > INT_MAX / scale)
            pm_error("Scale argument too large.  You specified %d", scale);
        else 
            cmdlineP->scale = scale;

        if (argc-1 > 1) {
            cmdlineP->inputFilespec = argv[2];

            if (argc-1 > 2)
                pm_error("Too many arguments (%d).  There are at most two "
                         "non-option arguments: "
                         "scale factor and the file name",
                         argc-1);
        } else
            cmdlineP->inputFilespec = "-";
    } else
        pm_error("You must specify the scale factor as an argument");

    free(option_def);
}



struct FS {
  int * thiserr;
  int * nexterr;
};


static void
initializeFloydSteinberg(struct FS  * const fsP,
                         int          const newcols,
                         unsigned int const seed,
                         bool         const seedSpec) {

    unsigned int col;

    MALLOCARRAY(fsP->thiserr, newcols + 2);
    MALLOCARRAY(fsP->nexterr, newcols + 2);

    if (fsP->thiserr == NULL || fsP->nexterr == NULL)
        pm_error("out of memory");

    srand(seedSpec ? seed : pm_randseed());

    for (col = 0; col < newcols + 2; ++col)
        fsP->thiserr[col] = (rand() % SCALE - HALFSCALE) / 4;
        /* (random errors in [-SCALE/8 .. SCALE/8]) */
}



/*
    Scanning method
    
    In Floyd-Steinberg dithering mode horizontal direction of scan alternates
    between rows; this is called "serpentine scanning".
    
    Example input (14 x 7), N=3:
    
    111222333444xx    Fractional pixels on the right edge and bottom edge (x)
    111222333444xx    are ignored; their values do not influence output. 
    111222333444xx
    888777666555xx
    888777666555xx
    888777666555xx
    xxxxxxxxxxxxxx
    
    Output (4 x 2):
    
    1234
    8765

*/



enum Direction { RIGHT_TO_LEFT, LEFT_TO_RIGHT };


static enum Direction
oppositeDir(enum Direction const arg) {

    switch (arg) {
    case LEFT_TO_RIGHT: return RIGHT_TO_LEFT;
    case RIGHT_TO_LEFT: return LEFT_TO_RIGHT;
    }
    assert(false);  /* All cases handled above */
}



int
main(int argc, const char * argv[]) {

    FILE * ifP;
    struct CmdlineInfo cmdline;
    bit ** bitslice;
    bit * newbitrow;
    int rows, cols;
    int format;
    unsigned int newrows, newcols;
    unsigned int row;
    enum Direction direction;
    struct FS fs;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFilespec);

    pbm_readpbminit(ifP, &cols, &rows, &format);

    bitslice = pbm_allocarray(cols, cmdline.scale);

    if (rows < cmdline.scale || cols < cmdline.scale)
        pm_error("Scale argument (%u) too large for image", cmdline.scale);
    else {
        newrows = rows / cmdline.scale;
        newcols = cols / cmdline.scale;
    }
    pbm_writepbminit( stdout, newcols, newrows, 0 );
    newbitrow = pbm_allocrow_packed( newcols );

    if (cmdline.halftone == QT_FS)
        initializeFloydSteinberg(&fs, newcols,
                                 cmdline.randomseed, cmdline.randomseedSpec);
    else {
        /* These variables are meaningless in this case, and the values
           should never be used.
        */
        fs.thiserr = NULL;
        fs.nexterr = NULL;
    }

    for (row = 0, direction = LEFT_TO_RIGHT; row < newrows; ++row) {
        unsigned int const colChars = pbm_packed_bytes(newcols);

        unsigned int colChar;
        unsigned int subrow;
        unsigned int col;
        int limitCol;
        int startCol;
        int step;
   
        for (colChar = 0; colChar < colChars; ++colChar)
            newbitrow[colChar] = 0x00;  /* Clear to white */
 
        for (subrow = 0; subrow < cmdline.scale; ++subrow)
            pbm_readpbmrow(ifP, bitslice[subrow], cols, format);

        if (cmdline.halftone == QT_FS) {
            unsigned int col;
            for (col = 0; col < newcols + 2; ++col)
                fs.nexterr[col] = 0;
        }
        switch (direction) {
        case LEFT_TO_RIGHT: {
            startCol = 0;
            limitCol = newcols;
            step = +1;  
        } break;
        case RIGHT_TO_LEFT: {
            startCol = newcols - 1;
            limitCol = -1;
            step = -1;
        } break;
        }

        for (col = startCol; col != limitCol; col += step) {
            int const n = cmdline.scale;
            unsigned int sum;
            int sumScaled;
            unsigned int subrow;

            for (subrow = 0, sum = 0; subrow < n; ++subrow) {
                unsigned int subcol;
                for (subcol = 0; subcol < n; ++subcol) {
                    assert(row * n + subrow < rows);
                    assert(col * n + subcol < cols);
                    if (bitslice[subrow][col * n + subcol] == PBM_WHITE)
                        ++sum;
                }
            }

            sumScaled = (sum * SCALE) / (SQR(n));

            if (cmdline.halftone == QT_FS)
                sumScaled += fs.thiserr[col + 1];

            if (sumScaled >= cmdline.value) {
                if (cmdline.halftone == QT_FS)
                    sumScaled = sumScaled - cmdline.value - HALFSCALE;
            } else
                newbitrow[col/8] |= (PBM_BLACK << (7 - col%8));

            if (cmdline.halftone == QT_FS) {
                switch (direction) {
                case LEFT_TO_RIGHT: {
                    fs.thiserr[col + 2] += ( sumScaled * 7 ) / 16;
                    fs.nexterr[col    ] += ( sumScaled * 3 ) / 16;
                    fs.nexterr[col + 1] += ( sumScaled * 5 ) / 16;
                    fs.nexterr[col + 2] += ( sumScaled     ) / 16;
                    break;
                }
                case RIGHT_TO_LEFT: {
                    fs.thiserr[col    ] += ( sumScaled * 7 ) / 16;
                    fs.nexterr[col + 2] += ( sumScaled * 3 ) / 16;
                    fs.nexterr[col + 1] += ( sumScaled * 5 ) / 16;
                    fs.nexterr[col    ] += ( sumScaled     ) / 16;
                    break;
                }
                }
            }
        }

        pbm_writepbmrow_packed(stdout, newbitrow, newcols, 0);

        if (cmdline.halftone == QT_FS) {
            int * const temperr = fs.thiserr;
            fs.thiserr = fs.nexterr;
            fs.nexterr = temperr;
            direction  = oppositeDir(direction);
        }
    }

    free(fs.thiserr);
    free(fs.nexterr);

    pbm_freerow(newbitrow);
    pbm_freearray(bitslice, cmdline.scale);
    pm_close(ifP);
    pm_close(stdout);

    return 0;
}


