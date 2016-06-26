/*
**
** Font routines.
**
** BDF font code Copyright 1993 by George Phillips.
**
** Copyright (C) 1991 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
** BDF font specs available from:
** https://partners.adobe.com/public/developer/en/font/5005.BDF_Spec.pdf
** Glyph Bitmap Distribution Format (BDF) Specification
** Version 2.2
** 22 March 1993
** Adobe Developer Support
*/

#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "netpbm/pm_c_util.h"
#include "netpbm/mallocvar.h"
#include "netpbm/nstring.h"

#include "pbmfont.h"
#include "pbm.h"

static unsigned int const firstCodePoint = 32;
    /* This is the code point of the first character in a pbmfont font.
       In ASCII, it is a space.
    */

static unsigned int const nCharsInFont = 96;
    /* The number of characters in a pbmfont font.  A pbmfont font defines
       characters at position 32 (ASCII space) through 127, so that's 96.
    */


struct font *
pbm_defaultfont(const char * const name) {
/*----------------------------------------------------------------------------
   Generate the built-in font with name 'name'.
-----------------------------------------------------------------------------*/
    struct font * retval;

    if (streq(name, "bdf"))
        retval = &pbm_defaultBdffont;
    else if (streq(name, "fixed"))
        retval = &pbm_defaultFixedfont;
    else
        pm_error( "built-in font name unknown, try 'bdf' or 'fixed'");

    return retval;
}



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



struct font*
pbm_dissectfont(const bit ** const font,
                unsigned int const frows,
                unsigned int const fcols) {
    /*
       This routine expects a font bitmap representing the following text:
      
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
       0, and the code points increase in standard reading order, so
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
      
       The dissection works by finding the first blank row and column;
       i.e the lower right corner of the "M" in the upper left corner
       of the matrix.  That gives the height and width of the
       maximum-sized character, which is not too useful.  But the
       distance from there to the opposite side is an integral
       multiple of the cell size, and that's what we need.  Then it's
       just a matter of filling in all the coordinates.  */
    
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

    int row, col, ch, r, c, i;
    struct font * fn;
    struct glyph * glyph;
    char* bmap;

    computeCharacterSize(font, fcols, frows,
                         &cellWidth, &cellHeight, &charWidth, &charHeight);

    /* Now convert to a general font */

    MALLOCVAR(fn);
    if (fn == NULL)
        pm_error("out of memory allocating font structure");

    fn->maxwidth  = charWidth;
    fn->maxheight = charHeight;
    fn->x = fn->y = 0;

    fn->oldfont = font;
    fn->frows = frows;
    fn->fcols = fcols;
    
    /* Initialize all character positions to "undefined."  Those that
       are defined in the font will be filled in below.
    */
    for (i = 0; i < 256; i++)
        fn->glyph[i] = NULL;

    MALLOCARRAY(glyph, nCharsInFont);
    if ( glyph == NULL )
        pm_error( "out of memory allocating glyphs" );
    
    bmap = (char*) malloc( fn->maxwidth * fn->maxheight * nCharsInFont );
    if ( bmap == (char*) 0)
        pm_error( "out of memory allocating glyph data" );

    /* Now fill in the 0,0 coords. */
    row = cellHeight * 2;
    col = cellWidth * 2;
    for (i = 0; i < firstCodePoint; ++i)
        fn->glyph[i] = NULL;

    for ( ch = 0; ch < nCharsInFont; ++ch ) {
        glyph[ch].width = fn->maxwidth;
        glyph[ch].height = fn->maxheight;
        glyph[ch].x = glyph[ch].y = 0;
        glyph[ch].xadd = cellWidth;

        for ( r = 0; r < glyph[ch].height; ++r )
            for ( c = 0; c < glyph[ch].width; ++c )
                bmap[r * glyph[ch].width + c] = font[row + r][col + c];
    
        glyph[ch].bmap = bmap;
        bmap += glyph[ch].width * glyph[ch].height;

        fn->glyph[firstCodePoint + ch] = &glyph[ch];

        col += cellWidth;
        if ( col >= cellWidth * 14 ) {
            col = cellWidth * 2;
            row += cellHeight;
        }
    }
    for (i = firstCodePoint + nCharsInFont; i < 256; ++i)
        fn->glyph[i] = NULL;
    
    return fn;
}



struct font *
pbm_loadfont(const char * const filename) {

    FILE * fileP;
    struct font * fontP;
    char line[256];

    fileP = pm_openr(filename);
    fgets(line, 256, fileP);
    pm_close(fileP);

    if (line[0] == PBM_MAGIC1 && 
        (line[1] == PBM_MAGIC2 || line[1] == RPBM_MAGIC2)) {
        fontP = pbm_loadpbmfont(filename);
    } else if (!strncmp(line, "STARTFONT", 9)) {
        fontP = pbm_loadbdffont(filename);
        if (!fontP)
            pm_error("could not load BDF font file");
    } else {
        pm_error("font file not in a recognized format.  Does not start "
                 "with the signature of a PBM file or BDF font file");
        assert(false);
        fontP = NULL;  /* defeat compiler warning */
    }
    return fontP;
}



struct font *
pbm_loadpbmfont(const char * const filename) {

    FILE * ifP;
    bit ** font;
    int fcols, frows;

    ifP = pm_openr(filename);
    font = pbm_readpbm(ifP, &fcols, &frows);
    pm_close(ifP);
    return pbm_dissectfont((const bit **)font, frows, fcols);
}



void
pbm_dumpfont(struct font * const fontP,
             FILE *        const ofP) {
/*----------------------------------------------------------------------------
  Dump out font as C source code.
-----------------------------------------------------------------------------*/
    unsigned int i;
    unsigned int ng;

    if (fontP->oldfont)
        pm_message("Netpbm no longer has the capability to generate "
                   "a font in long hexadecimal data format");

    for (i = 0, ng = 0; i < 256; ++i) {
        if (fontP->glyph[i])
            ++ng;
    }

    printf("static struct glyph _g[%d] = {\n", ng);

    for (i = 0; i < 256; ++i) {
        struct glyph * const glyphP = fontP->glyph[i];
        if (glyphP) {
            unsigned int j;
            printf(" { %d, %d, %d, %d, %d, \"", glyphP->width, glyphP->height,
                   glyphP->x, glyphP->y, glyphP->xadd);
            
            for (j = 0; j < glyphP->width * glyphP->height; ++j) {
                if (glyphP->bmap[j])
                    printf("\\1");
                else
                    printf("\\0");
            }    
            --ng;
            printf("\" }%s\n", ng ? "," : "");
        }
    }
    printf("};\n");

    printf("struct font XXX_font = { %d, %d, %d, %d, {\n",
           fontP->maxwidth, fontP->maxheight, fontP->x, fontP->y);

    {
        unsigned int i;

        for (i = 0; i < 256; ++i) {
            if (fontP->glyph[i])
                printf(" _g + %d", ng++);
            else
                printf(" NULL");
        
            if (i != 255) printf(",");
            printf("\n");
        }
    }

    printf(" }\n};\n");
}



/* Routines for loading a BDF font file */

#define  MAXBDFLINE 1024 

/* Official Adobe document says max length of string is 65535 characters.
   However the value 1024 is sufficient for practical uses.
*/

typedef struct {
/*----------------------------------------------------------------------------
   This is an object for reading lines of a font file.  It reads and tokenizes
   them into words.
-----------------------------------------------------------------------------*/
    FILE * ifP;

    char line[MAXBDFLINE+1];
        /* This is the storage space for the words of the line.  The
           words go in here, one after another, separated by NULs.

           It also functions as a work area for readline_read().
        */
    const char * arg[32];
        /* These are the words; each entry is a pointer into line[] (above) */
} Readline;



static void
readline_init(Readline * const readlineP,
              FILE *     const ifP) {

    readlineP->ifP = ifP;

    readlineP->arg[0] = NULL;
}



static void
tokenize(char *         const s,
         const char **  const words,
         unsigned int   const maxWordCt) {
/*----------------------------------------------------------------------------
   Chop up 's' into words by changing space characters to NUL.  Return
   as 'words' pointer to the beginning of those words in 's'.

   If there are more than 'maxWordCt' words in 's', ignore the excess on
   the right.
-----------------------------------------------------------------------------*/
    unsigned int n;
    char * p;

    p = &s[0];
    n = 0;

    while (*p) {
        if (ISSPACE(*p))
            *p++ = '\0';
        else {
            words[n++] = p;
            if (n >= maxWordCt)
                break;
            while (*p && !ISSPACE(*p))
                ++p;
        }
    }
    words[n] = NULL;
}



static void
readline_read(Readline * const readlineP,
              bool *     const eofP) {
/*----------------------------------------------------------------------------
   Read a nonblank line from the file.  Make its contents available
   as readlineP->arg[].

   Return *eofP == true iff there is no nonblank line before EOF or we
   are unable to read the file.
-----------------------------------------------------------------------------*/
    bool gotLine;
    bool error;

    for (gotLine = false, error = false; !gotLine && !error; ) {
        char * rc;

        rc = fgets(readlineP->line, MAXBDFLINE+1, readlineP->ifP);
        if (rc == NULL)
            error = true;
        else {
            tokenize(readlineP->line,
                     readlineP->arg, ARRAY_SIZE(readlineP->arg));
            if (readlineP->arg[0] != NULL)
                gotLine = true;
        }
    }
    *eofP = error;
}



static void
parseBitmapRow(const char *    const hex,
               unsigned int    const glyphWidth,
               unsigned char * const bmap,
               unsigned int    const origBmapIndex,
               unsigned int *  const newBmapIndexP,
               const char **   const errorP) {
/*----------------------------------------------------------------------------
   Parse one row of the bitmap for a glyph, from the hexadecimal string
   for that row in the font file, 'hex'.  The glyph is 'glyphWidth'
   pixels wide.

   We place our result in 'bmap' at *bmapIndexP and advanced *bmapIndexP.
-----------------------------------------------------------------------------*/
    unsigned int bmapIndex;
    int i;  /* dot counter */
    const char * p;

    bmapIndex = origBmapIndex;

    for (i = glyphWidth, p = &hex[0], *errorP = NULL;
         i > 0 && !*errorP;
         i -= 4) {

        if (*p == '\0')
            pm_asprintf(errorP, "Not enough hexadecimal digits for glyph "
                        "of width %u in '%s'",
                        glyphWidth, hex);
        else {
            char const hdig = *p++;
            unsigned int hdigValue;

            if (hdig >= '0' && hdig <= '9')
                hdigValue = hdig - '0';
            else if (hdig >= 'a' && hdig <= 'f')
                hdigValue = 10 + (hdig - 'a');
            else if (hdig >= 'A' && hdig <= 'F')
                hdigValue = 10 + (hdig - 'A');
            else 
                pm_asprintf(errorP,
                            "Invalid hex digit x%02x (%c) in bitmap data '%s'",
                            (unsigned int)(unsigned char)hdig, 
                            isprint(hdig) ? hdig : '.',
                            hex);

            if (!*errorP) {
                if (i > 0)
                    bmap[bmapIndex++] = hdigValue & 0x8 ? 1 : 0;
                if (i > 1)
                    bmap[bmapIndex++] = hdigValue & 0x4 ? 1 : 0;
                if (i > 2)
                    bmap[bmapIndex++] = hdigValue & 0x2 ? 1 : 0;
                if (i > 3)
                    bmap[bmapIndex++] = hdigValue & 0x1 ? 1 : 0;
            }
        }
    }
    *newBmapIndexP = bmapIndex;
}



static void
readBitmap(Readline *      const readlineP,
           unsigned int    const glyphWidth,
           unsigned int    const glyphHeight,
           const char *    const charName,
           unsigned char * const bmap) {

    int n;
    unsigned int bmapIndex;

    bmapIndex = 0;
           
    for (n = glyphHeight; n > 0; --n) {
        bool eof;
        const char * error;

        readline_read(readlineP, &eof);

        if (eof)
            pm_error("End of file in bitmap for character '%s' in BDF "
                     "font file.", charName);

        if (!readlineP->arg[0])
            pm_error("A line that is supposed to contain bitmap data, "
                     "in hexadecimal, for character '%s' is empty", charName);

        parseBitmapRow(readlineP->arg[0], glyphWidth, bmap, bmapIndex,
                       &bmapIndex, &error);

        if (error) {
            pm_error("Error in line %d of bitmap for character '%s': %s",
                     n, charName, error);
            pm_strfree(error);
        }
    }
}



static void
createBmap(unsigned int  const glyphWidth,
           unsigned int  const glyphHeight,
           Readline *    const readlineP,
           const char *  const charName,
           const char ** const bmapP) {

    unsigned char * bmap;
    bool eof;
    
    if (glyphWidth > 0 && UINT_MAX / glyphWidth < glyphHeight)
        pm_error("Ridiculously large glyph");

    MALLOCARRAY(bmap, glyphWidth * glyphHeight);

    if (!bmap)
        pm_error("no memory for font glyph byte map");

    readline_read(readlineP, &eof);
    if (eof)
        pm_error("End of file encountered reading font glyph byte map from "
                 "BDF font file.");
    
    if (streq(readlineP->arg[0], "ATTRIBUTES")) {
        bool eof;
        readline_read(readlineP, &eof);
        if (eof)
            pm_error("End of file encountered after ATTRIBUTES in BDF "
                     "font file.");
    }                
    if (!streq(readlineP->arg[0], "BITMAP"))
        pm_error("'%s' found where BITMAP expected in definition of "
                 "character '%s' in BDF font file.",
                 readlineP->arg[0], charName);

    assert(streq(readlineP->arg[0], "BITMAP"));

    readBitmap(readlineP, glyphWidth, glyphHeight, charName, bmap);

    *bmapP = (char *)bmap;
}



static void
readExpectedStatement(Readline *    const readlineP,
                      const char *  const expected) {
/*----------------------------------------------------------------------------
  Have the readline object *readlineP read the next line from the file, but
  expect it to be a line of type 'expected' (i.e. the verb token at the
  beginning of the line is that, e.g. "STARTFONT").  If it isn't, fail the
  program.
-----------------------------------------------------------------------------*/
    bool eof;

    readline_read(readlineP, &eof);

    if (eof)
        pm_error("EOF in BDF font file where '%s' expected", expected);
    else if (!streq(readlineP->arg[0], expected))
        pm_error("Statement of type '%s' where '%s' expected in BDF font file",
                 readlineP->arg[0], expected);
}



static void
skipCharacter(Readline * const readlineP) {
/*----------------------------------------------------------------------------
  In the BDF font file being read by readline object *readlineP, skip through
  the end of the character we are presently in.
-----------------------------------------------------------------------------*/
    bool endChar;
                        
    endChar = FALSE;
                        
    while (!endChar) {
        bool eof;
        readline_read(readlineP, &eof);
        if (eof)
            pm_error("End of file in the middle of a character (before "
                     "ENDCHAR) in BDF font file.");
        endChar = streq(readlineP->arg[0], "ENDCHAR");
    }                        
}



static void
interpEncoding(const char **  const arg,
               unsigned int * const codepointP,
               bool *         const badCodepointP) {
/*----------------------------------------------------------------------------
   With arg[] being the ENCODING statement from the font, return as
   *codepointP the codepoint that it indicates (code point is the character
   code, e.g. in ASCII, 48 is '0').

   But if the statement doesn't give an acceptable codepoint return
   *badCodepointP == TRUE.
-----------------------------------------------------------------------------*/

    bool gotCodepoint;
    bool badCodepoint;
    unsigned int codepoint;

    if (atoi(arg[1]) >= 0) {
        codepoint = atoi(arg[1]);
        gotCodepoint = true;
    } else {
      if (atoi(arg[1]) == -1 && arg[2] != NULL) {
            codepoint = atoi(arg[2]);
            gotCodepoint = true;
        } else
            gotCodepoint = false;
    }
    if (gotCodepoint) {
        if (codepoint > 255)
            badCodepoint = true;
        else
            badCodepoint = false;
    } else
        badCodepoint = true;

    *badCodepointP = badCodepoint;
    *codepointP    = codepoint;
}



static void
readEncoding(Readline *     const readlineP,
             unsigned int * const codepointP,
             bool *         const badCodepointP) {

    readExpectedStatement(readlineP, "ENCODING");
    
    interpEncoding(readlineP->arg, codepointP, badCodepointP);
}



static void
validateFontLimits(const struct font * const fontP) {

    assert( pbm_maxfontheight() > 0 && pbm_maxfontwidth() > 0 );

    if (fontP->maxwidth  <= 0 ||
        fontP->maxheight <= 0 ||
        fontP->maxwidth  > pbm_maxfontwidth()  ||
        fontP->maxheight > pbm_maxfontheight() ||
        fontP->x < - fontP->maxwidth  +1 ||
        fontP->y < - fontP->maxheight +1 ||
        fontP->x > fontP->maxwidth  ||
        fontP->y > fontP->maxheight ||
        fontP->x + fontP->maxwidth  > pbm_maxfontwidth() || 
         fontP->y + fontP->maxheight > pbm_maxfontheight()
        ) {

        pm_error("Global font metric(s) out of bounds.\n"); 
    }
}



static void
validateGlyphLimits(const struct font  * const fontP,
                    const struct glyph * const glyphP,
                    const char *         const charName) {

    if (glyphP->width  == 0 ||
        glyphP->height == 0 ||
        glyphP->width  > fontP->maxwidth  ||
        glyphP->height > fontP->maxheight ||
        glyphP->x < fontP->x ||
        glyphP->y < fontP->y ||
        glyphP->x + (int) glyphP->width  > fontP->x + fontP->maxwidth  ||
        glyphP->y + (int) glyphP->height > fontP->y + fontP->maxheight ||
        glyphP->xadd > pbm_maxfontwidth() ||
        glyphP->xadd + MAX(glyphP->x,0) + (int) glyphP->width >
        pbm_maxfontwidth()
        ) {

        pm_error("Font metric(s) for char '%s' out of bounds.\n", charName);
    }
}



static void
processChars(Readline *     const readlineP,
             struct font  * const fontP) {
/*----------------------------------------------------------------------------
   Process the CHARS block in a BDF font file, assuming the file is positioned
   just after the CHARS line.  Read the rest of the block and apply its
   contents to *fontP.
-----------------------------------------------------------------------------*/
    unsigned int const nCharacters = atoi(readlineP->arg[1]);

    unsigned int nCharsDone;

    nCharsDone = 0;

    while (nCharsDone < nCharacters) {
        bool eof;

        readline_read(readlineP, &eof);
        if (eof)
            pm_error("End of file after CHARS reading BDF font file");

        if (streq(readlineP->arg[0], "COMMENT")) {
            /* ignore */
        } else if (!streq(readlineP->arg[0], "STARTCHAR"))
            pm_error("no STARTCHAR after CHARS in BDF font file");
        else {
            const char * const charName = pm_strdup(readlineP->arg[1]);

            struct glyph * glyphP;
            unsigned int codepoint;
            bool badCodepoint;

            assert(streq(readlineP->arg[0], "STARTCHAR"));

            MALLOCVAR(glyphP);

            if (glyphP == NULL)
                pm_error("no memory for font glyph for '%s' character",
                         charName);

            readEncoding(readlineP, &codepoint, &badCodepoint);

            if (badCodepoint)
                skipCharacter(readlineP);
            else if (fontP->glyph[codepoint] != NULL)
                pm_error("Multiple definition of code point %d "
                         "in font file", (unsigned int) codepoint); 
            else {
                readExpectedStatement(readlineP, "SWIDTH");
                    
                readExpectedStatement(readlineP, "DWIDTH");
                glyphP->xadd = atoi(readlineP->arg[1]);

                readExpectedStatement(readlineP, "BBX");
                glyphP->width  = atoi(readlineP->arg[1]);
                glyphP->height = atoi(readlineP->arg[2]);
                glyphP->x      = atoi(readlineP->arg[3]);
                glyphP->y      = atoi(readlineP->arg[4]);

                validateGlyphLimits(fontP, glyphP, charName);

                createBmap(glyphP->width, glyphP->height, readlineP, charName,
                           &glyphP->bmap);
                

                readExpectedStatement(readlineP, "ENDCHAR");

                assert(codepoint < 256); /* Ensured by readEncoding() */

                fontP->glyph[codepoint] = glyphP;
                pm_strfree(charName);
            }
            ++nCharsDone;
        }
    }
}



static void
processBdfFontLine(Readline     * const readlineP,
                   struct font  * const fontP,
                   bool         * const endOfFontP) {
/*----------------------------------------------------------------------------
   Process a nonblank line just read from a BDF font file.

   This processing may involve reading more lines.
-----------------------------------------------------------------------------*/
    *endOfFontP = FALSE;  /* initial assumption */

    assert(readlineP->arg[0] != NULL);  /* Entry condition */

    if (streq(readlineP->arg[0], "COMMENT")) {
        /* ignore */
    } else if (streq(readlineP->arg[0], "SIZE")) {
        /* ignore */
    } else if (streq(readlineP->arg[0], "STARTPROPERTIES")) {
        /* Read off the properties and ignore them all */
        unsigned int const propCount = atoi(readlineP->arg[1]);

        unsigned int i;
        for (i = 0; i < propCount; ++i) {
            bool eof;
            readline_read(readlineP, &eof);
            if (eof)
                pm_error("End of file after STARTPROPERTIES in BDF font file");
        }
    } else if (streq(readlineP->arg[0], "FONTBOUNDINGBOX")) {
        fontP->maxwidth  = atoi(readlineP->arg[1]);
        fontP->maxheight = atoi(readlineP->arg[2]);
        fontP->x = atoi(readlineP->arg[3]);
        fontP->y = atoi(readlineP->arg[4]);
        validateFontLimits(fontP);
    } else if (streq(readlineP->arg[0], "ENDPROPERTIES")) {
      if (fontP->maxwidth ==0)
      pm_error("Encountered ENDPROPERTIES before FONTBOUNDINGBOX " 
                   "in BDF font file");
    } else if (streq(readlineP->arg[0], "ENDFONT")) {
        *endOfFontP = true;
    } else if (streq(readlineP->arg[0], "CHARS")) {
      if (fontP->maxwidth ==0)
      pm_error("Encountered CHARS before FONTBOUNDINGBOX " 
                   "in BDF font file");
      else
        processChars(readlineP, fontP);
    } else {
        /* ignore */
    }
}



struct font *
pbm_loadbdffont(const char * const name) {

    FILE * ifP;
    Readline readline;
    struct font * fontP;
    bool endOfFont;

    ifP = fopen(name, "rb");
    if (!ifP)
        pm_error("Unable to open BDF font file name '%s'.  errno=%d (%s)",
                 name, errno, strerror(errno));

    readline_init(&readline, ifP);

    MALLOCVAR(fontP);
    if (fontP == NULL)
        pm_error("no memory for font");

    fontP->oldfont = 0;
    { 
        /* Initialize all characters to nonexistent; we will fill the ones we
           find in the bdf file later.
        */
        unsigned int i;
        for (i = 0; i < 256; ++i) 
            fontP->glyph[i] = NULL;
    }

    fontP->maxwidth = fontP->maxheight = fontP->x = fontP->y = 0;

    readExpectedStatement(&readline, "STARTFONT");

    endOfFont = FALSE;

    while (!endOfFont) {
        bool eof;
        readline_read(&readline, &eof);
        if (eof)
            pm_error("End of file before ENDFONT statement in BDF font file");

        processBdfFontLine(&readline, fontP, &endOfFont);
    }
    return fontP;
}



