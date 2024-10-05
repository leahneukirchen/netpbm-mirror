/* pbmtoascii.c - read a PBM image and produce ASCII graphics
**
** Copyright (C) 1988, 1992 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdbool.h>
#include <assert.h>

#include "mallocvar.h"
#include "shhopt.h"
#include "pbm.h"

enum Resolution {RESOLUTION_1X2, RESOLUTION_2X4};

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* Filespec of input file */
    enum Resolution resolution;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
  Convert program invocation arguments (argc,argv) into a format the
  program can use easily, struct CmdlineInfo.  Validate arguments along
  the way and exit program with message if invalid.

  Note that some string information we return as *cmdlineP is in the storage
  argv[] points to.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int opt1x2Spec, opt2x4Spec;
    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0,   "1x2",    OPT_FLAG, NULL, &opt1x2Spec,    0);
    OPTENT3(0,   "2x4",    OPT_FLAG, NULL, &opt2x4Spec,    0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    free(option_def);

    if (opt1x2Spec && opt2x4Spec)
        pm_error("You cannot specify both -1x2 and -2x4");
    else if (opt1x2Spec)
        cmdlineP->resolution = RESOLUTION_1X2;
    else
        cmdlineP->resolution = RESOLUTION_2X4;


    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else
        cmdlineP->inputFileName = argv[1];

    if (argc-1 > 1)
        pm_error("Too many arguments (%u).  Only possible argument is "
                 "input file name", argc-1);
}



/*
  The algorithm is based on describing the 2 or 8 pixels in a cell with a
  single integer called a signature, which we use as an index into an array to
  get the character whose glyph best matches those pixels.  The encoding is as
  follows.  Make a string of bits, with each bit being one pixel of the cell,
  1 = black, 0 = white.  The order of the string is left to right across the
  top row, then the next row down, etc.  Considering that string to be a
  binary cipher, the integer it represents is the signature.

  Example: the 2x4 cell consists of these pixels: (* = black)

      * *
      *

        *
  The bit string to represent this is 11100001.  So the signature for this
  cell is the integer 0xE1 (225).

  You index the array carr2x4 with 0xE1, and get '?' as the character to
  represent that cell.  (We don't really try very hard to match the shape;
  it's mostly important to match the density).
*/

#define SQQ '\''
#define BSQ '\\'

/* Bit-map for 1x2 mode:
**     1
**     2
*/
static char const carr1x2[4] = {
/*   0    1    2    3   */
    ' ', '"', 'o', 'M' };

/* Bit-map for 2x4 mode (hex):
**     1  2
**     4  8
**     10 20
**     40 80
** The idea here is first to preserve geometry, then to show density.
*/
#define D08 'M'
#define D07 'H'
#define D06 '&'
#define D05 '$'
#define D04 '?'
static char const carr2x4[256] = {
/*0  1    2   3    4   5    6   7    8   9    A   B    C   D    E   F  */
' ',SQQ, '`','"', '-',SQQ, SQQ,SQQ, '-','`', '`','`', '-','^','^','"',/*00-0F*/
'.',':', ':',':', '|','|', '/',D04, '/','>', '/','>', '~','+','/','*',/*10-1F*/
'.',':', ':',':', BSQ,BSQ, '<','<', '|',BSQ, '|',D04, '~',BSQ,'+','*',/*20-2F*/
'-',':', ':',':', '~',D04, '<','<', '~','>', D04,'>', '=','b','d','#',/*30-3F*/
'.',':', ':',':', ':','!', '/',D04, ':',':', '/',D04, ':',D04,D04,'P',/*40-4F*/
',','i', '/',D04, '|','|', '|','T', '/',D04, '/','7', 'r','}','/','P',/*50-5F*/
',',':', ';',D04, '>',D04, 'S','S', '/',')', '|','7', '>',D05,D05,D06,/*60-6F*/
'v',D04, D04,D05, '+','}', D05,'F', '/',D05, '/',D06, 'p','D',D06,D07,/*70-7F*/
'.',':', ':',':', ':',BSQ, ':',D04, ':',BSQ, '!',D04, ':',D04,D04,D05,/*80-8F*/
BSQ,BSQ, ':',D04, BSQ,'|', '(',D05, '<','%', D04,'Z', '<',D05,D05,D06,/*90-9F*/
',',BSQ, 'i',D04, BSQ,BSQ, D04,BSQ, '|','|', '|','T', D04,BSQ,'4','9',/*A0-AF*/
'v',D04, D04,D05, BSQ,BSQ, D05,D06, '+',D05, '{',D06, 'q',D06,D06,D07,/*B0-BF*/
'_',':', ':',D04, ':',D04, D04,D05, ':',D04, D04,D05, ':',D05,D05,D06,/*C0-CF*/
BSQ,D04, D04,D05, D04,'L', D05,'[', '<','Z', '/','Z', 'c','k',D06,'R',/*D0-DF*/
',',D04, D04,D05, '>',BSQ, 'S','S', D04,D05, 'J',']', '>',D06,'1','9',/*E0-EF*/
'o','b', 'd',D06, 'b','b', D06,'6', 'd',D06, 'd',D07, '#',D07,D07,D08 /*F0-FF*/
};



static void
makeRowOfSignatures(FILE *         const ifP,
                    unsigned int   const cols,
                    unsigned int   const rows,
                    int            const format,
                    unsigned int   const cellWidth,
                    unsigned int   const cellHeight,
                    unsigned int   const row,
                    unsigned int * const sig,
                    unsigned int   const ccols) {
/*----------------------------------------------------------------------------
   Compute the signatures for every cell in a row.

   Read the pixels from *ifP, which is positioned to the first pixel row
   of the cell row, which is row number 'row'.  The image dimensions are
   'cols' x 'rows' pixels.

   Each cell is 'cellWidth' x 'cellHeight'.

   Return the signatures as sig[], which is 'ccols' wide because that's how
   many cells you get from 'cols' pixels divided into cells 'cellWidth' pixels
   wide.
-----------------------------------------------------------------------------*/
    unsigned int b;
    unsigned int subrow;  /* row within cell */
    bit * bitrow;        /* malloc'ed array */

    bitrow = pbm_allocrow(cols);
    {
        unsigned int col;
        for (col = 0; col < ccols; ++col)
            sig[col] = 0;
    }

    b = 0x1;  /* initial value */
    for (subrow = 0; subrow < cellHeight; ++subrow) {
        if (row + subrow < rows) {
            unsigned int subcol;  /* col within cell */
            pbm_readpbmrow(ifP, bitrow, cols, format);
            for (subcol = 0; subcol < cellWidth; ++subcol) {
                unsigned int col, ccol;
                for (col = subcol, ccol = 0;
                     col < cols;
                     col += cellWidth, ++ccol) {
                    if (bitrow[col] == PBM_BLACK)
                        sig[ccol] |= b;
                }
                b <<= 1;
            }
        }
    }
    pbm_freerow(bitrow);
}



static void
findRightMargin(const unsigned int * const sig,
                unsigned int         const ccols,
                const char *         const carr,
                unsigned int *       const endColP) {
/*----------------------------------------------------------------------------
   Find the first cell of the right margin, i.e. a contiguous set of
   all-white cells at the right end of the row.
-----------------------------------------------------------------------------*/
    unsigned int endcol;

    for (endcol = ccols; endcol > 0; --endcol) {
        if (carr[sig[endcol-1]] != ' ')
            break;
    }
    *endColP = endcol;
}



static void
assembleCellRow(const unsigned int * const sig,
                unsigned int         const ccols,
                const char *         const carr,
                char *               const line) {
/*----------------------------------------------------------------------------
   Return as line[] the line of ASCII codes for the characters of one
   row of cells, ready for printing.
-----------------------------------------------------------------------------*/
    unsigned int col;

    for (col = 0; col < ccols; ++col)
        line[col] = carr[sig[col]];

    line[ccols] = '\0';
}



static void
pbmtoascii(FILE *       const ifP,
           unsigned int const cellWidth,
           unsigned int const cellHeight,
           const char * const carr) {

    int format;
    int cols, rows;
        /* Dimensions of the input in pixels */
    unsigned int ccols;
        /* Width of the output in characters */
    char * line;         /* malloc'ed array */
    unsigned int row;
    unsigned int * signatureRow;  /* malloc'ed array */
        /* This is the cell signatures of a row of cells.
           signatureRow[0] is the signature for the first (leftmost) cell
           in the row, signatureRow[1] is the signature for the next one,
           etc.  A signature is an encoding of the pixels of a cell as
           an integer, as described above.
        */
    assert(cellWidth * cellHeight <= sizeof(signatureRow[0])*8);

    pbm_readpbminit(ifP, &cols, &rows, &format);

    ccols = (cols + cellWidth - 1) / cellWidth;

    MALLOCARRAY(signatureRow, ccols);
    if (signatureRow == NULL)
        pm_error("No memory for %u columns", ccols);

    MALLOCARRAY_NOFAIL(line, ccols+1);
    if (line == NULL)
        pm_error("No memory for %u columns", ccols);

    for (row = 0; row < rows; row += cellHeight) {
        unsigned int endCol;

        makeRowOfSignatures(ifP, cols, rows, format, cellWidth, cellHeight,
                            row, signatureRow, ccols);

        findRightMargin(signatureRow, ccols, carr, &endCol);

        assembleCellRow(signatureRow, endCol, carr, line);

        puts(line);
    }
    free(signatureRow);
    free(line);
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    unsigned int gridx, gridy;
    const char * carr;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    switch (cmdline.resolution) {
    case RESOLUTION_1X2:
        gridx = 1;
        gridy = 2;
        carr = carr1x2;
        break;
    case RESOLUTION_2X4:
        gridx = 2;
        gridy = 4;
        carr = carr2x4;
        break;
    }

    pbmtoascii(ifP, gridx, gridy, carr);

    pm_close(ifP);

    return 0;
}



