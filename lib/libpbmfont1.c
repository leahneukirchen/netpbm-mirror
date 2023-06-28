/*
**
** Routines for loading a PBM sheet font file
**
** Copyright (C) 1991 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <assert.h>
#include <string.h>

#include "netpbm/pm_c_util.h"
#include "netpbm/mallocvar.h"
#include "netpbm/nstring.h"

#include "pbm.h"
#include "pbmfont.h"


/*----------------------------------------------------------------------------

  The routines in this file reads a font bitmap representing
  the following text:

  (0,0)
     M ",/^_[`jpqy| M

     /  !"#$%&'()*+ /
     < ,-./01234567 <
     > 89:;<=>?@ABC >
     @ DEFGHIJKLMNO @
     _ PQRSTUVWXYZ[ _
     { \]^_`abcdefg {
     } hijklmnopqrs }
     ~ tuvwxyz{|}~  ~

     M ",/^_[`jpqy| M

  The bitmap must be cropped exactly to the edges.

  The characters in the border you see are irrelevant except for
  character size compuations.  The 12 x 8 array in the center is
  the font.  The top left character there belongs to code point
  32, and the code points increase in standard reading order, so
  the bottom right character is code point 127.  You can't define
  code points < 32 or > 127 with this font format.

  The characters in the top and bottom border rows must include a
  character with the lowest reach of any in the font (e.g. "y",
  "_") and one with the highest reach (e.g. '"').  The characters
  in the left and right border columns must include characters
  with the rightmost and leftmost reach of any in the font
  (e.g. "M" for both).

  The border must be separated from the font by one blank text
  row or text column.
-----------------------------------------------------------------------------*/


static unsigned int const firstCodePoint = 32;
    /* This is the code point of the first character in a pbmfont font.
       In ASCII, it is a space.
    */

static unsigned int const nCharsInFont = 96;
    /* The number of characters in a pbmfont font.  A pbmfont font defines
       characters at position 32 (ASCII space) through 127, so that's 96.
    */


static void
findFirstBlankRow(const bit **   const font,
                  unsigned int   const fcols,
                  unsigned int   const frows,
                  unsigned int * const browP) {

    unsigned int row;
    bool foundBlank;

    for (row = 0, foundBlank = false; row < frows / 6 && !foundBlank; ++row) {
        unsigned int col;
        bit col0Value = font[row][0];
        bool rowIsBlank;
        rowIsBlank = true;  /* initial assumption */
        for (col = 1; col < fcols; ++col)
            if (font[row][col] != col0Value)
                rowIsBlank = false;

        if (rowIsBlank) {
            foundBlank = true;
            *browP = row;
        }
    }

    if (!foundBlank)
        pm_error("couldn't find blank pixel row in font");
}



static void
findFirstBlankCol(const bit **   const font,
                  unsigned int   const fcols,
                  unsigned int   const frows,
                  unsigned int * const bcolP) {

    unsigned int col;
    bool foundBlank;

    for (col = 0, foundBlank = false; col < fcols / 6 && !foundBlank; ++col) {
        unsigned int row;
        bit row0Value = font[0][col];
        bool colIsBlank;
        colIsBlank = true;  /* initial assumption */
        for (row = 1; row < frows; ++row)
            if (font[row][col] != row0Value)
                colIsBlank = false;

        if (colIsBlank) {
            foundBlank = true;
            *bcolP = col;
        }
    }

    if (!foundBlank)
        pm_error("couldn't find blank pixel column in font");
}



static void
computeCharacterSize(const bit **   const font,
                     unsigned int   const fcols,
                     unsigned int   const frows,
                     unsigned int * const cellWidthP,
                     unsigned int * const cellHeightP,
                     unsigned int * const charWidthP,
                     unsigned int * const charHeightP) {

    unsigned int firstBlankRow;
    unsigned int firstBlankCol;
    unsigned int heightLast11Rows;

    findFirstBlankRow(font, fcols, frows, &firstBlankRow);

    findFirstBlankCol(font, fcols, frows, &firstBlankCol);

    heightLast11Rows = frows - firstBlankRow;

    if (heightLast11Rows % 11 != 0)
        pm_error("The rows of characters in the font do not appear to "
                 "be all the same height.  The last 11 rows are %u pixel "
                 "rows high (from pixel row %u up to %u), "
                 "which is not a multiple of 11.",
                 heightLast11Rows, firstBlankRow, frows);
    else {
        unsigned int widthLast15Cols;

        *cellHeightP = heightLast11Rows / 11;

        widthLast15Cols = fcols - firstBlankCol;

        if (widthLast15Cols % 15 != 0)
            pm_error("The columns of characters in the font do not appear to "
                     "be all the same width.  "
                     "The last 15 columns are %u pixel "
                     "columns wide (from pixel col %u up to %u), "
                     "which is not a multiple of 15.",
                     widthLast15Cols, firstBlankCol, fcols);
        else {
            *cellWidthP = widthLast15Cols / 15;

            *charWidthP = firstBlankCol;
            *charHeightP = firstBlankRow;
        }
    }
}



struct font *
pbm_dissectfont(const bit ** const fontsheet,
                unsigned int const frows,
                unsigned int const fcols) {
/*----------------------------------------------------------------------------
  Dissect PBM sheet font data, create a font structure, load bitmap data into
  it.

  Return value is a pointer to the newly created font structure

  The input bitmap data is in memory, in one byte per pixel format.

  The dissection works by finding the first blank row and column;
  i.e the lower right corner of the "M" in the upper left corner
  of the matrix.  That gives the height and width of the
  maximum-sized character, which is not too useful.  But the
  distance from there to the opposite side is an integral
  multiple of the cell size, and that's what we need.  Then it's
  just a matter of filling in all the coordinates.

  Struct font has fields 'oldfont', 'fcols', 'frows' for backward
  compatibility.  If there is any need to load data stored in this format
  feed the above three, in order, as arguments to this function:

    pbm_dissectfont(oldfont, fcols, frows);
 ----------------------------------------------------------------------------*/

    unsigned int cellWidth, cellHeight;
        /* Dimensions in pixels of each cell of the font -- that
           includes the glyph and the white space above and to the
           right of it.  Each cell is a tile of the font image.  The
           top character row and left character row don't count --
           those cells are smaller because they are missing the white
           space.
        */
    unsigned int charWidth, charHeight;
        /* Maximum dimensions of glyph itself, inside its cell */

    unsigned int row, col;
    int ch;
    unsigned int i;
    struct font * fontP;

    computeCharacterSize(fontsheet, fcols, frows,
                         &cellWidth, &cellHeight, &charWidth, &charHeight);

    /* Now convert to a general font */

    MALLOCVAR(fontP);
    if (fontP == NULL)
        pm_error("out of memory allocating font structure");

    fontP->maxwidth  = charWidth;
    fontP->maxheight = charHeight;
    fontP->x = fontP->y = 0;

    fontP->oldfont = fontsheet;
    fontP->frows = frows;
    fontP->fcols = fcols;

    /* Now fill in the 0,0 coords. */
    row = cellHeight * 2;
    col = cellWidth  * 2;

    /* Load individual glyphs */
    for (ch = 0; ch < nCharsInFont; ++ch) {
        /* Allocate memory separately for each glyph.
           pbm_loadbdffont2() does this in exactly the same manner.
         */
        struct glyph * const glyphP =
             (struct glyph *) malloc (sizeof (struct glyph));
        char * const bmap = (char*) malloc(fontP->maxwidth * fontP->maxheight);

        unsigned int r;

        if (bmap == NULL || glyphP == NULL)
            pm_error( "out of memory allocating glyph data" );

        glyphP->width  = fontP->maxwidth;
        glyphP->height = fontP->maxheight;
        glyphP->x = glyphP->y = 0;
        glyphP->xadd = cellWidth;

        for (r = 0; r < glyphP->height; ++r) {
            unsigned int c;
            for (c = 0; c < glyphP->width; ++c)
                bmap[r * glyphP->width + c] = fontsheet[row + r][col + c];
        }
        glyphP->bmap = bmap;
        fontP->glyph[firstCodePoint + ch] = glyphP;

        col += cellWidth;
        if (col >= cellWidth * 14) {
            col = cellWidth * 2;
            row += cellHeight;
        }
    }

    /* Initialize all remaining character positions to "undefined." */
    for (i = 0; i < firstCodePoint; ++i)
        fontP->glyph[i] = NULL;

    for (i = firstCodePoint + nCharsInFont; i <= PM_FONT_MAXGLYPH; ++i)
        fontP->glyph[i] = NULL;

    return fontP;
}




struct font *
pbm_loadpbmfont(const char * const filename) {
/*----------------------------------------------------------------------------
  Read PBM font sheet data from file 'filename'.
  Load data into font structure.

  When done with object, free with pbm_destroybdffont().
-----------------------------------------------------------------------------*/

    FILE * ifP;
    bit ** fontsheet;
    int fcols, frows;
    struct font * retval;

    ifP = pm_openr(filename);

    fontsheet = pbm_readpbm(ifP, &fcols, &frows);

    if ((fcols - 1) / 16 >= pbm_maxfontwidth() ||
        (frows - 1) / 12 >= pbm_maxfontheight())
        pm_error("Absurdly large PBM font file: %s", filename);
    else if (fcols < 31 || frows < 23) {
        /* Need at least one pixel per character, and this is too small to
           have that.
        */
        pm_error("PBM font file '%s' too small to be a font file: %u x %u.  "
                 "Minimum sensible size is 31 x 23",
                 filename, fcols, frows);
    }

    pm_close(ifP);

    retval = pbm_dissectfont((const bit **)fontsheet, frows, fcols);
    return (retval);

}



struct font2 *
pbm_loadpbmfont2(const char * const filename) {
/*----------------------------------------------------------------------------
  Like pbm_loadpbmfont, but return a pointer to struct font2.

  When done with object, free with pbm_destroybdffont2().
-----------------------------------------------------------------------------*/

    const struct font * const pbmfont = pbm_loadpbmfont(filename);
    struct font2 * const retval  = pbm_expandbdffont(pbmfont);

    free ((void *)pbmfont);

    /* Overwrite some fields */

    retval->load_fn = LOAD_PBMSHEET;
    retval->default_char = (PM_WCHAR) ' ';
    retval->default_char_defined = TRUE;
    retval->name = pm_strdup("(PBM sheet font has no name)");
    retval->charset = ISO646_1991_IRV;
    retval->charset_string = pm_strdup("ASCII");
    retval->total_chars = retval->chars = nCharsInFont;

    return(retval);
}



