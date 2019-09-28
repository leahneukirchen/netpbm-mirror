/* pbmtext.c - render text into a bitmap
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

#define _DEFAULT_SOURCE 1  /* New name for SVID & BSD source defines */
#define _BSD_SOURCE 1      /* Make sure strdup() is in string.h */
#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */

#include <string.h>
#include <math.h>
#include <limits.h>
#include <assert.h>
#include <setjmp.h>
#include <locale.h>
#include <wchar.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pm.h"
#include "pbm.h"
#include "pbmfont.h"

/* Max length of input text.  Valid for text which is part of the
   command line and also for text fed from standard input.
   Note that newline is counted as a character.
*/
#define  MAXLINECHARS 4999

/* We add one slot for the terminating NULL charter
   and another slot as a margin to detect overruns.
*/
#define  LINEBUFSIZE  (MAXLINECHARS + 2)

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const PM_WCHAR * text; /* text from command line or NULL if none */
    const char * font;    /* -font option value or NULL if none */
    const char * builtin; /* -builtin option value or NULL if none */
    float space;          /* -space option value or default */
    int lspace;           /* -lspace option value or default */
    unsigned int width;   /* -width option value or zero */
    unsigned int wchar;   /* -wchar option specified  */
    unsigned int nomargins;  /* -nomargins option specified  */
    unsigned int dryrun;     /* -dry-run option specified */
    unsigned int textdump;   /* -text-dump option specified */
    unsigned int verbose;    /* -verbose option specified */
        /* undocumented option */
    unsigned int dumpsheet; /* font data sheet in PBM format for -font */
};



static const PM_WCHAR *
textFmCmdLine(int argc, const char ** argv) {

    char * text;
    PM_WCHAR * wtext;
    unsigned int i;
    unsigned int totaltextsize;

    MALLOCARRAY(text, LINEBUFSIZE);

    if (!text)
        pm_error("Unable to allocate memory for a buffer of up to %u "
                 "characters of text", MAXLINECHARS);

    text[0] = '\0';

    for (i = 1, totaltextsize = 0; i < argc; ++i) {
        if (i > 1) {
            strcat(text, " ");
        }
        totaltextsize += strlen(argv[i]) + (i > 1 ? 1 : 0);
        if (totaltextsize > MAXLINECHARS)
           pm_error("Input text is %u characters.  "
                    "Cannot process longer than %u",
                    totaltextsize, (unsigned int) MAXLINECHARS);
        strcat(text, argv[i]);
    }
    MALLOCARRAY(wtext, totaltextsize * sizeof(PM_WCHAR));

    if (!wtext)
        pm_error("Unable to allocate memory for a buffer of up to %u "
                 "wide characters of text", totaltextsize);

    for (i = 0; i < totaltextsize + 1; ++i)
        wtext[i] = (PM_WCHAR) text[i];

    free(text);

    return wtext;
}



static void
parseCommandLine(int argc, const char ** argv,
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

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "font",       OPT_STRING, &cmdlineP->font,    NULL,   0);
    OPTENT3(0, "builtin",    OPT_STRING, &cmdlineP->builtin, NULL,   0);
    OPTENT3(0, "space",      OPT_FLOAT,  &cmdlineP->space,   NULL,   0);
    OPTENT3(0, "lspace",     OPT_INT,    &cmdlineP->lspace,  NULL,   0);
    OPTENT3(0, "width",      OPT_UINT,   &cmdlineP->width,   NULL,   0);
    OPTENT3(0, "nomargins",  OPT_FLAG,   NULL, &cmdlineP->nomargins, 0);
    OPTENT3(0, "wchar",      OPT_FLAG,   NULL, &cmdlineP->wchar,     0);
    OPTENT3(0, "verbose",    OPT_FLAG,   NULL, &cmdlineP->verbose,   0);
    OPTENT3(0, "dry-run",    OPT_FLAG,   NULL, &cmdlineP->dryrun,    0);
    OPTENT3(0, "text-dump",  OPT_FLAG,   NULL, &cmdlineP->textdump,  0);
    OPTENT3(0, "dump-sheet", OPT_FLAG,   NULL, &cmdlineP->dumpsheet, 0);

    /* Set the defaults */
    cmdlineP->font    = NULL;
    cmdlineP->builtin = NULL;
    cmdlineP->space   = 0.0;
    cmdlineP->width   = 0;
    cmdlineP->lspace  = 0;

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
    /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (cmdlineP->width > 0 && cmdlineP->nomargins) {
        pm_message("-nomargins has no effect when -width is specified");
        cmdlineP->nomargins = FALSE;
    } else if (cmdlineP->width > INT_MAX-10)
        pm_error("-width value too large");

    if (cmdlineP->space > pbm_maxfontwidth())
        pm_error("-space value too large");
    else if (cmdlineP->space < -pbm_maxfontwidth())
        pm_error("negative -space value too large");

    if (cmdlineP->lspace > pbm_maxfontheight())
        pm_error("-lspace value too large");
    else if (cmdlineP->lspace < -pbm_maxfontheight())
        pm_error("negative -lspace value too large");

    if (cmdlineP->font != NULL && cmdlineP->builtin != NULL)
        pm_error("You cannot specify both -font and -builtin");

    if (cmdlineP->textdump) {
        if (cmdlineP->dryrun)
            pm_error("You cannot specify both -dry-run and -text-dump");
        else if (cmdlineP->dumpsheet)
            pm_error("You cannot specify both -dump-sheet and -text-dump");
    }

    if (cmdlineP->dryrun && cmdlineP->dumpsheet)
        pm_error("You cannot specify both -dry-run and -dump-sheet");

    if (argc-1 == 0)
        cmdlineP->text = NULL;
    else {  /* Text to render is part of command line */
        if (cmdlineP->wchar)
            pm_error("-wchar is not valid when text is from command line");

        cmdlineP->text = textFmCmdLine(argc, argv);
    }

    free(option_def);
}



static void
reportFont(const struct font2 * const fontP) {

    pm_message("FONT:");
    pm_message("  Name: %s", fontP->name);
    pm_message("  Encoding: %s", fontP->charset_string);
    pm_message("  Origin: %s", pbmFontOrigin[fontP->load_fn]);
    pm_message("  Character dimensions: %uw x %uh",
               fontP->maxwidth, fontP->maxheight);
    pm_message("  Additional vert white space: %d pixels", fontP->y);
    pm_message("  # characters loaded: %u", fontP->chars);
}






static struct font2 *
font2FromFile(const char * const fileName,
              PM_WCHAR     const maxmaxglyph) {

    struct font2 * font2P;

    jmp_buf jmpbuf;
    int rc;

    rc = setjmp(jmpbuf);

    if (rc == 0) {
        /* This is the normal program flow */
        pm_setjmpbuf(&jmpbuf);

        font2P = pbm_loadfont2(fileName, maxmaxglyph);

        pm_setjmpbuf(NULL);
    } else {
        /* This is the second pass, after pbm_loadbdffont2 does a longjmp
           because it fails.
        */
        pm_setjmpbuf(NULL);

        pm_error("Failed to load font from file '%s'", fileName);
    }

    return font2P;
}



static void
computeFont(struct CmdlineInfo const cmdline,
            struct font2 **    const fontPP) {

    struct font2 * font2P;

    if (cmdline.font)
        font2P = font2FromFile(cmdline.font,
                               cmdline.wchar ? PM_FONT2_MAXGLYPH :
                                               PM_FONT_MAXGLYPH);
    else if (cmdline.builtin)
        font2P = pbm_defaultfont2(cmdline.builtin);
    else
        font2P = pbm_defaultfont2(cmdline.wchar ? "bdf" : "bdf");

    if (cmdline.verbose)
        reportFont(font2P);

    *fontPP = font2P;
}



struct Text {
    PM_WCHAR **  textArray;  /* malloc'ed */
        /* This is strictly characters that are in user's font - no control
           characters, no undefined code points.
        */
    unsigned int allocatedLineCount;
    unsigned int lineCount;
};



static void
allocTextArray(struct Text * const textP,
               unsigned int  const maxLineCount,
               unsigned int  const maxColumnCount) {

    unsigned int line;

    textP->allocatedLineCount = maxColumnCount > 0 ? maxLineCount : 0;
    MALLOCARRAY_NOFAIL(textP->textArray, maxLineCount);

    for (line = 0; line < maxLineCount; ++line) {
        if (maxColumnCount > 0)
            MALLOCARRAY_NOFAIL(textP->textArray[line], maxColumnCount+1);
    else
        textP->textArray[line] = NULL;
    }
    textP->lineCount = 0;
}



static void
freeTextArray(struct Text const text) {

    unsigned int line;

    for (line = 0; line < text.allocatedLineCount; ++line)
        free((PM_WCHAR **)text.textArray[line]);

    free(text.textArray);
}



enum FixMode {SILENT, /* convert silently */
              WARN,   /* output message to stderr */
              QUIT    /* abort */ };


static void
fixControlChars(const PM_WCHAR  * const input,
                struct font2    * const fontP,
                const PM_WCHAR ** const outputP,
                enum FixMode      const fixMode) {
/*----------------------------------------------------------------------------
   Return a translation of input[] that can be rendered as glyphs in
   the font 'fontP'.  Return it as newly malloced *outputP.

   Expand tabs to spaces.

   Remove any trailing newline.  (But leave intermediate ones as line
   delimiters).

   Depending on value of fixMode, turn anything that isn't a code point
   in the font to a single space (which isn't guaranteed to be in the
   font either, of course).
-----------------------------------------------------------------------------*/
    /* We don't know in advance how big the output will be because of the
       tab expansions.  So we make sure before processing each input
       character that there is space in the output buffer for a worst
       case tab expansion, plus a terminating NUL, reallocating as
       necessary.  And we originally allocate enough for the entire line
       assuming no tabs.
    */

    unsigned int const tabSize = 8;

    unsigned int inCursor, outCursor;
    PM_WCHAR * output;      /* Output buffer.  Malloced */
    size_t outputSize;  /* Currently allocated size of 'output' */

    outputSize = wcslen(input) + 1 + tabSize;
        /* Leave room for one worst case tab expansion and NUL terminator */
    MALLOCARRAY(output, outputSize);

    if (output == NULL)
        pm_error("Couldn't allocate %u bytes for a line of text.",
                 (unsigned)outputSize);

    for (inCursor = 0, outCursor = 0; input[inCursor] != L'\0'; ++inCursor) {
        PM_WCHAR const currentChar = input[inCursor];
        if (outCursor + 1 + tabSize > outputSize) {
            outputSize = outCursor + 1 + 4 * tabSize;
            REALLOCARRAY(output, outputSize);
            if (output == NULL)
                pm_error("Couldn't allocate %u bytes for a line of text.",
                         (unsigned)outputSize);
        }
        if (currentChar == L'\n' && input[inCursor+1] == L'\0') {
            /* This is a terminating newline.  We don't do those. */
        } else if (currentChar == L'\t') {
            /* Expand this tab into the right number of spaces. */
            unsigned int const nextTabStop =
                (outCursor + tabSize) / tabSize * tabSize;

            if (fontP->glyph[L' '] == NULL)
                pm_error("space character not defined in font");

            while (outCursor < nextTabStop)
                output[outCursor++] = L' ';
        } else if (currentChar > fontP->maxglyph ||
                   !fontP->glyph[currentChar]) {
            if (currentChar > PM_FONT2_MAXGLYPH)
                pm_message("code point %X is beyond what this program "
                           "can handle.  Max=%X",
                           (unsigned int)currentChar, PM_FONT2_MAXGLYPH);

            /* Turn this unknown char into a single space. */
            if (fontP->glyph[L' '] == NULL)
                pm_error("space character not defined in font");
            else if (fixMode == QUIT)
                pm_error("code point %X not defined in font",
                         (unsigned int) currentChar );
            else {
                if (fixMode == WARN)
                    pm_message("converting code point %X to space",
                               (unsigned int) currentChar );
                output[outCursor++] = ' ';
            }
        } else
            output[outCursor++] = input[inCursor];

        assert(outCursor <= outputSize);
    }
    output[outCursor++] = L'\0';

    assert(outCursor <= outputSize);

    *outputP = output;
}



static void
clearBackground(bit ** const bits,
                int    const cols,
                int    const rows) {

    unsigned int row;

    for (row = 0; row < rows; ++row) {
        unsigned int colChar;
        for (colChar = 0; colChar < pbm_packed_bytes(cols); ++colChar)
            bits[row][colChar] = 0x00;
    }
}



static void
getEdges(double               const currentPosition,
         PM_WCHAR             const currentChar,
         const struct glyph * const glyphP,
         int                  const currLeftEdge,
         double               const currRightEdge,
         int                * const newLeftEdgeP,
         double             * const newRightEdgeP) {

    int leftEdge;
    double rightEdge;

    if (glyphP == NULL)
        pm_error("Unrenderable char: %04X", (unsigned int) currentChar);
    else {
        leftEdge  =  (int) MIN(currentPosition + glyphP->x, currLeftEdge);
        rightEdge =  MAX(currentPosition + glyphP->x + glyphP->width,
                         currRightEdge);
    }
    *newLeftEdgeP  = leftEdge;
    *newRightEdgeP = rightEdge;
}



static void
advancePosition(double               const currentPosition,
                PM_WCHAR             const currentChar,
                const struct glyph * const glyphP,
                float                const space,
                double               const accumulatedSpace,
                double             * const newPositionP,
                double             * const newAccumulatedSpaceP) {
/*----------------------------------------------------------------------------
  Advance position according to value for glyph.
  Add extra intercharacter space if -space option was used.

  The advance value must be zero or positive.
----------------------------------------------------------------------------*/

    /* Start position of next character */
    /* Must not move left from current position */
    int const fullPixels = (int) (accumulatedSpace + space);
        /* round toward 0 */
    int const advance    = (int) glyphP->xadd + fullPixels;

    if (advance < 0) {
        if (space < 0)
            pm_error("Negative -space value too large");
        else
            pm_error("Abnormal horizontal advance value %d "
                     "for code point 0x%lx.",
                     glyphP->xadd, (unsigned long int) currentChar);
    }
    else if (currentPosition + advance > INT_MAX)
        pm_error("Image is too wide");
    else {
        *newPositionP = currentPosition + advance;
        *newAccumulatedSpaceP = accumulatedSpace + space
            - (double) fullPixels;
    }
}



static void
getLineDimensions(PM_WCHAR             const line[],
                  const struct font2 * const fontP,
                  float                const intercharacterSpace,
                  double *             const rightEdgeP,
                  int    *             const leftEdgeP) {
/*----------------------------------------------------------------------------
   Determine the left edge and right edge in pixels of the line of text
   line[] in the font *fontP, and return them as *leftEdgeP and *rightEdgeP.
   *leftEdgeP will be negative if the leftmost character in the line has a
   "backup" distance.

   Note that the right (left) edge may not belong to the last (first)
   character in the text line.  This happens when the font is slanted
   (xadd is smaller than width) and/or intercharacter space is negative.
   This is illustrated by the following:

     pbmtext -nomargin "ART." | pnmshear -30 -noantialias

   Also note that there may be no black pixels on what is reported as an edge.
   This often happens with fixed-width font in which the white areas on the
   sides are not trimmed.
-----------------------------------------------------------------------------*/
    unsigned int cursor;  /* cursor into the line of text */
    double currentPosition;
        /* sum of xadd values and intercharacter space so far in line.  this
           is never negative.
        */
    double accumulatedIcs;
        /* accumulated intercharacter space so far in the line we are stepping
           through.  Because the intercharacter space might not be an integer,
           we accumulate it here and realize full pixels whenever we have more
           than one pixel.  Note that this can be negative (which means were
           crowding, rather than spreading, text).
        */
    int leftEdge;
    double rightEdge;

    currentPosition = 0;  /* initial value */
    accumulatedIcs  = 0.0;  /* initial value */

    leftEdge  = INT_MAX;  /* initial value */
    rightEdge = INT_MIN;  /* initial value */

    for (cursor = 0; line[cursor] != L'\0'; ++cursor) {
        PM_WCHAR          const currentChar = line[cursor];
        unsigned long int const glyphIndex  = (unsigned long int) currentChar;
        struct glyph *    const glyphP      = fontP->glyph[glyphIndex];

        getEdges(currentPosition, currentChar, glyphP, leftEdge, rightEdge,
                 &leftEdge, &rightEdge);

        advancePosition(currentPosition, currentChar, glyphP,
                        intercharacterSpace, accumulatedIcs,
                        &currentPosition, &accumulatedIcs);
    }

    if (line[0] == L'\0') {     /* Empty line */
        leftEdge  = 0;
        rightEdge = 0.0;
    }

    *leftEdgeP  = leftEdge;
    *rightEdgeP = rightEdge;
}



static void
getCharsWithinWidth(PM_WCHAR             const line[],
                    const struct font2 * const fontP,
                    float                const intercharacter_space,
                    unsigned int         const targetWidth,
                    unsigned int       * const charCountP,
                    int                * const leftEdgeP) {
/*----------------------------------------------------------------------------
   Determine how many characters of text line[] fit into an image of target
   width targetWidth.

   *leftEdgeP will be negative if the leftmost character in the line has a
   "backup" distance and zero if it does not.
-----------------------------------------------------------------------------*/
    if (line[0] == L'\0') {
        /* Empty line */
        *leftEdgeP = 0;
        *charCountP = 0;
    } else {
        unsigned int cursor;  /* cursor into the line of text */
        double currentPosition;
        double accumulatedIcs;
        int leftEdge;
        double rightEdge;
        unsigned int currentWidth;

        currentPosition = 0;    /* initial value */
        accumulatedIcs  = 0.0;  /* initial value */

        leftEdge     = INT_MAX;  /* initial value */
        rightEdge    = INT_MIN;  /* initial value */

        for (cursor = 0, currentWidth = 0;
             currentWidth <= targetWidth && line[cursor] != L'\0';
             ++cursor) {
            PM_WCHAR const currentChar = line[cursor];
            unsigned long int const glyphIndex =
              (unsigned long int) currentChar;
            struct glyph * const glyphP = fontP->glyph[glyphIndex];

            getEdges(currentPosition, currentChar, glyphP, leftEdge, rightEdge,
                     &leftEdge, &rightEdge);

            advancePosition(currentPosition, currentChar, glyphP,
                            intercharacter_space, accumulatedIcs,
                            &currentPosition, &accumulatedIcs);

            currentWidth = rightEdge - ((leftEdge > 0 ) ? 0 : leftEdge);
        }

        if (currentWidth > targetWidth) {
            if (cursor == 1)
                pm_error("-width value too small "
                         "to accomodate single character");
            else
                *charCountP = cursor - 1;
        } else
            *charCountP = cursor;

        *leftEdgeP  = leftEdge;
    }
}



static void
insertCharacter(const struct glyph * const glyphP,
                int                  const toprow,
                int                  const leftcol,
                unsigned int         const cols,
                unsigned int         const rows,
                bit **               const bits) {
/*----------------------------------------------------------------------------
   Insert one character (whose glyph is 'glyph') into the image bits[].
   Its top left corner shall be row 'toprow', column 'leftcol'.
-----------------------------------------------------------------------------*/
    if (glyphP->width == 0 && glyphP->height == 0) {
        /* No bitmap data.  Some BDF files code space this way */
    } else {
        unsigned int glyph_y;  /* Y position within the glyph */

        if (leftcol + glyphP->x < 0 ||
            leftcol + glyphP->x + glyphP->width > cols ||
            toprow < 0 ||
            toprow + glyphP->height >rows )
            pm_error("internal error.  Rendering out of bounds");

        for (glyph_y = 0; glyph_y < glyphP->height; ++glyph_y) {
            unsigned int glyph_x;  /* position within the glyph */

            for (glyph_x = 0; glyph_x < glyphP->width; ++glyph_x) {
                if (glyphP->bmap[glyph_y * glyphP->width + glyph_x]) {
                    unsigned int const col = leftcol + glyphP->x + glyph_x;
                    bits[toprow+glyph_y][col/8] |= PBM_BLACK << (7-col%8);
                }
            }
        }
    }
}



static void
insertCharacters(bit **         const bits,
                 struct Text    const lp,
                 struct font2 * const fontP,
                 int            const topmargin,
                 int            const leftmargin,
                 float          const intercharacter_space,
                 unsigned int   const cols,
                 unsigned int   const rows,
                 int            const lspace,
                 bool           const fixedAdvance) {
/*----------------------------------------------------------------------------
   Render the text 'lp' into the image 'bits' using font *fontP and
   putting 'intercharacter_space' pixels between characters and
   'lspace' pixels between the lines.
-----------------------------------------------------------------------------*/
    unsigned int line;  /* Line number in input text */

    for (line = 0; line < lp.lineCount; ++line) {
        unsigned int row;  /* row in image of top of current typeline */
        double leftcol;  /* Column in image of left edge of current glyph */
        unsigned int cursor;  /* cursor into a line of input text */
        double accumulatedIcs;
            /* accumulated intercharacter space so far in the line we
               are building.  Because the intercharacter space might
               not be an integer, we accumulate it here and realize
               full pixels whenever we have more than one pixel.
            */

        row = topmargin + line * (fontP->maxheight + lspace);
        leftcol = leftmargin;
        accumulatedIcs = 0.0;  /* initial value */

        for (cursor = 0; lp.textArray[line][cursor] != '\0'; ++cursor) {
            PM_WCHAR const currentChar = lp.textArray[line][cursor];
            unsigned long int const glyphIndex =
                (unsigned long int)currentChar;
            struct glyph * const glyphP = fontP->glyph[glyphIndex];
            int const toprow =
                row + fontP->maxheight + fontP->y - glyphP->height - glyphP->y;
                /* row number in image of top row in glyph */

            assert(glyphP != NULL);

            insertCharacter(glyphP, toprow, leftcol, cols, rows, bits);

        if (fixedAdvance)
            leftcol += fontP->maxwidth;
        else
            advancePosition(leftcol, currentChar, glyphP,
                            intercharacter_space, accumulatedIcs,
                            &leftcol, &accumulatedIcs);
        }
    }
}



static void
flowText(struct Text    const inputText,
         int            const targetWidth,
         struct font2 * const fontP,
         float          const intercharacterSpace,
         struct Text  * const outputTextP,
         unsigned int * const maxleftbP) {

    unsigned int outputLineNum;
    unsigned int incursor;   /* cursor into the line we are reading */
    unsigned int const maxLineCount = 50; /* max output lines */
    int leftEdge;
    int leftExtreme = 0;
    unsigned int charCount;

    allocTextArray(outputTextP, maxLineCount, 0);

    for (incursor = 0, outputLineNum = 0;
         inputText.textArray[0][incursor] != L'\0'; ) {

        unsigned int outcursor;

        getCharsWithinWidth(&inputText.textArray[0][incursor], fontP,
                            intercharacterSpace, targetWidth,
                            &charCount, &leftEdge);

        MALLOCARRAY(outputTextP->textArray[outputLineNum], charCount+1);

        if (!outputTextP->textArray[outputLineNum])
            pm_error("Unable to allocate memory for the text of line %u, "
                     "%u characters long", outputLineNum, charCount);

        ++outputTextP->allocatedLineCount;

        for (outcursor = 0; outcursor < charCount; ++outcursor, ++incursor)
            outputTextP->textArray[outputLineNum][outcursor] =
                inputText.textArray[0][incursor];

        outputTextP->textArray[outputLineNum][charCount] = L'\0';
        ++outputLineNum;
        if (outputLineNum >= maxLineCount)
            pm_error("-width too small.  too many output lines");

        leftExtreme = MIN(leftEdge, leftExtreme);
    }
    outputTextP->lineCount = outputLineNum;
    *maxleftbP = (unsigned int) -leftExtreme;
}



static void
truncateText(struct Text    const inputText,
             unsigned int   const targetWidth,
             struct font2 * const fontP,
             float          const intercharacterSpace,
             unsigned int * const maxleftbP) {

    unsigned int lineNum;  /* Line number on which we are currently working */
    int leftEdge;
    int leftExtreme = 0;

    for (lineNum = 0; lineNum < inputText.lineCount; ++lineNum) {
        PM_WCHAR * const currentLine = inputText.textArray[lineNum];

        unsigned int charCount;

        getCharsWithinWidth(currentLine, fontP,
                            intercharacterSpace, targetWidth,
                            &charCount, &leftEdge);

        if (currentLine[charCount] != L'\0') {
            pm_message("truncating line %u from %u to %u characters",
                       lineNum, (unsigned) wcslen(currentLine), charCount);
            currentLine[charCount] = L'\0';
        }

        leftExtreme = MIN(leftEdge, leftExtreme);
    }
    *maxleftbP = (unsigned int) - leftExtreme;
}



static void
fgetWideString(PM_WCHAR *    const widestring,
               unsigned int  const size,
               FILE *        const ifP,
               bool *        const eofP,
               const char ** const errorP) {

    wchar_t * rc;

    assert(widestring);
    assert(size > 1);

    rc = fgetws(widestring, size, ifP);

    if (rc == NULL) {
        if (feof(ifP)) {
            *eofP   = true;
            *errorP = NULL;
        } else if (ferror(ifP) && errno == EILSEQ)
            pm_asprintf(errorP,
                        "fgetws(): conversion error: sequence is "
                        "invalid for locale '%s'",
                        setlocale(LC_CTYPE, NULL));
        else
            pm_asprintf(errorP,
                        "fgetws() of max %u bytes failed",
                        size);
    } else {
        *eofP   = false;
        *errorP = NULL;
    }
}



static void
fgetNarrowString(PM_WCHAR *    const widestring,
                 unsigned int  const size,
                 FILE *        const ifP,
                 bool *        const eofP,
                 const char ** const errorP) {

    char * bufNarrow;
    char * rc;

    assert(widestring);
    assert(size > 0);

    MALLOCARRAY_NOFAIL(bufNarrow, LINEBUFSIZE);

    rc = fgets(bufNarrow, size, ifP);

    if (rc == NULL) {
        if (feof(ifP)) {
            *eofP   = true;
            *errorP = NULL;
        } else
            pm_asprintf(errorP, "Error reading file");
    } else {
        size_t cnt;

        for (cnt = 0; cnt < size && bufNarrow[cnt] != '\0'; ++cnt)
            widestring[cnt] = (PM_WCHAR)(unsigned char) bufNarrow[cnt];

        widestring[cnt] = L'\0';

        *eofP   = false;
        *errorP = NULL;
    }
    free(bufNarrow);
}



static void
fgetNarrowWideString(PM_WCHAR *    const widestring,
                     unsigned int  const size,
                     FILE *        const ifP,
                     bool *        const eofP,
                     const char ** const errorP) {
/*----------------------------------------------------------------------------
  Return the next line from file *ifP, as *widestring, a buffer 'size'
  characters long.

  Lines are delimited by newline characters and EOF.

  'size' is the size in characters of the buffer at *widestring.  If the line
  to which the file is positioned is longer than that minus 1, we consider it
  to be only that long and consider the next character of the actual line to
  be the first character of the next line.  We leave the file positioned
  to that character.

  Return *eofP == true iff we encounter end of file (and therefore don't read
  a line).

  If we can't read the file (or sense EOF), return as *errorP a text
  explanation of why; otherwise, return *errorP = NULL.

  The line we return is null-terminated.  But it also includes any embedded
  null characters that are within the line in the file.  It is not strictly
  possible for Caller to tell whether a null character in *widestring comes
  from the file or is the one we put there, so Caller should just ignore any
  null character and anything after it.  It is also not possible for Caller to
  tell if we trunctaed the actual line because of 'size' if there is a null
  character in the line.  This means there just isn't any way to get
  reasonable behavior from this function if the input file contains null
  characters (but at least the damage is limited to presenting arbitrary text
  as the contents of the file - the program won't crash).

  Null characters never appear within normal text (including wide-character
  text).  If there is one in the input file, it is probably because the input
  is corrupted.

  The line we return may or may not end in a newline character.  It ends in a
  newline character unless it doesn't fit in 'size' characters or it is the
  last line in the file and doesn't end in newline.
-----------------------------------------------------------------------------*/
    /* The limitations described above with respect to null characters in
       *ifP are derived from the same limitations in POSIX 'fgets' and
       'fgetws'.  To avoid them, we would have to read *ifP one character
       at a time with 'fgetc' and 'fgetwc'.
    */

    int const wideCode = fwide(ifP, 0);
        /* Width orientation for *ifP: positive means wide, negative means
           byte, zero means undecided.
        */

    assert(widestring);
    assert(size > 0);

    if (wideCode > 0)
        /* *ifP is wide-oriented */
        fgetWideString(widestring, size, ifP, eofP, errorP);
    else
        fgetNarrowString(widestring, size, ifP, eofP, errorP);
}




static void
getText(PM_WCHAR       const cmdlineText[],
        struct font2 * const fontP,
        struct Text  * const inputTextP,
        enum FixMode   const fixMode) {
/*----------------------------------------------------------------------------
   Get as *inputTextP the text to format, given that the text on the
   command line (one word per command line argument, separated by spaces),
   is 'cmdlineText'.

   If 'cmdlineText' is null, that means to get the text from Standard Input.
   Otherwise, 'cmdlineText' is that text.

   But we return text as only renderable characters - characters in *fontP -
   with control characters interpreted or otherwise fixed, according to
   'fixMode'.

   If *inputTextP indicates Standard Input and Standard Input contains null
   characters, we will truncate lines or consider a single line to be multiple
   lines.
-----------------------------------------------------------------------------*/
    struct Text inputText;

    if (cmdlineText) {
        MALLOCARRAY_NOFAIL(inputText.textArray, 1);
        inputText.allocatedLineCount = 1;
        inputText.lineCount = 1;
        fixControlChars(cmdlineText, fontP,
                        (const PM_WCHAR**)&inputText.textArray[0], fixMode);
        free((void *) cmdlineText);
    } else {
        /* Read text from stdin. */

        unsigned int const lineBufTerm = LINEBUFSIZE - 1;

        unsigned int maxlines;
            /* Maximum number of lines for which we presently have space in
               the text array
            */
        PM_WCHAR *   buf;
        PM_WCHAR **  textArray;
        unsigned int lineCount;
        bool         eof;

        MALLOCARRAY(buf, LINEBUFSIZE);

        if (!buf)
            pm_error("Unable to allocate memory for up to %u characters of "
                     "text", MAXLINECHARS);
        buf[lineBufTerm] = L'\1';  /* Initalize to non-zero value */
                                   /* to detect input overrun */

        maxlines = 50;  /* initial value */
        MALLOCARRAY(textArray, maxlines);

        if (!textArray)
            pm_error("Unable to allocate memory for a buffer for up to %u "
                     "lines of text", maxlines);

        for (lineCount = 0, eof = false; !eof; ) {
            const char * error;
            fgetNarrowWideString(buf, LINEBUFSIZE, stdin, &eof, &error);
            if (error)
                pm_error("Unable to read line %u from file.  %s",
                         lineCount, error);
            else {
                if (!eof) {
                    if (buf[lineBufTerm] == L'\0') /* overrun */
                        pm_error(
                            "Line %u (starting at zero) of input text "
                            "is longer than %u characters. "
                            "Cannot process",
                            lineCount, (unsigned int) MAXLINECHARS);
                    if (lineCount >= maxlines) {
                        maxlines *= 2;
                        REALLOCARRAY(textArray, maxlines);
                        if (textArray == NULL)
                            pm_error("out of memory");
                    }
                    fixControlChars(buf, fontP,
                                    (const PM_WCHAR **)&textArray[lineCount],
                                    fixMode);
                    if (textArray[lineCount] == NULL)
                        pm_error("out of memory");
                    ++lineCount;
                }
            }
        }
        inputText.textArray = textArray;
        inputText.lineCount = lineCount;
        inputText.allocatedLineCount = lineCount;
        free(buf);
    }
    *inputTextP = inputText;
}



static void
computeMargins(struct CmdlineInfo const cmdline,
               struct Text        const inputText,
               struct font2 *     const fontP,
               unsigned int *     const vmarginP,
               unsigned int *     const hmarginP) {

    if (cmdline.nomargins) {
        *vmarginP = 0;
        *hmarginP = 0;
    } else {
        if (inputText.lineCount == 1) {
            *vmarginP = fontP->maxheight / 2;
            *hmarginP = fontP->maxwidth;
        } else {
            *vmarginP = fontP->maxheight;
            *hmarginP = 2 * fontP->maxwidth;
        }
    }
}



static void
formatText(struct CmdlineInfo const cmdline,
           struct Text        const inputText,
           struct font2 *     const fontP,
           unsigned int       const hmargin,
           struct Text *      const formattedTextP,
           unsigned int *     const maxleftb0P) {
/*----------------------------------------------------------------------------
  Flow or truncate lines to meet user's width request.
-----------------------------------------------------------------------------*/
    if (cmdline.width > 0) {
        unsigned int const fontMargin = fontP->x < 0 ? -fontP->x : 0;

        if (cmdline.width > INT_MAX -10)
            pm_error("-width value too large: %u", cmdline.width);
        else if (cmdline.width < 2 * hmargin)
            pm_error("-width value too small: %u", cmdline.width);
        else if (inputText.lineCount == 1) {
            flowText(inputText, cmdline.width - fontMargin,
                     fontP, cmdline.space, formattedTextP, maxleftb0P);
            freeTextArray(inputText);
        } else {
            truncateText(inputText, cmdline.width - fontMargin,
                         fontP, cmdline.space, maxleftb0P);
            *formattedTextP = inputText;
        }
    } else
        *formattedTextP = inputText;
}



static void
computeImageHeight(struct Text          const formattedText,
                   const struct font2 * const fontP,
                   int                  const interlineSpace,
                   unsigned int         const vmargin,
                   unsigned int       * const rowsP) {

    if (interlineSpace < 0 && fontP->maxheight < -interlineSpace)
        pm_error("-lspace value (%d) negative and exceeds font height.",
                 interlineSpace);
    else {
        double const rowsD = 2 * (double) vmargin +
            (double) formattedText.lineCount * fontP->maxheight +
            (double) (formattedText.lineCount-1) * interlineSpace;

        if (rowsD > INT_MAX-10)
            pm_error("Image height too large.");
        else
            *rowsP = (unsigned int) rowsD;
    }
}



static void
computeImageWidth(struct Text          const formattedText,
                  const struct font2 * const fontP,
                  float                const intercharacterSpace,
                  unsigned int         const hmargin,
                  unsigned int *       const colsP,
                  unsigned int *       const maxleftbP) {

    if (intercharacterSpace < 0 && fontP->maxwidth < -intercharacterSpace)
        pm_error("negative -space value %.2f exceeds font width",
                 intercharacterSpace);
    else {
        /* Find the widest line, and the one that backs up the most past
           the nominal start of the line.
        */

        unsigned int lineNum;
        double rightExtreme;
        int leftExtreme;
        double colsD;

        rightExtreme = 0.0;  /* initial value */
        leftExtreme = 0;     /* initial value */

        for (lineNum = 0; lineNum < formattedText.lineCount;  ++lineNum) {
            double rightEdge;
            int leftEdge;

            getLineDimensions(formattedText.textArray[lineNum], fontP,
                              intercharacterSpace,
                              &rightEdge, &leftEdge);
            rightExtreme = MAX(rightExtreme, rightEdge);
            leftExtreme  = MIN(leftExtreme,  leftEdge);
        }
        leftExtreme = MIN(leftExtreme, 0);

        colsD = (double) (-leftExtreme) + rightExtreme + 2 * hmargin;

        if (colsD > INT_MAX-10)
            pm_error("Image width too large.");
        else
            *colsP = (unsigned int) colsD;

        *maxleftbP = (unsigned int) - leftExtreme;
    }
}



static void
renderText(unsigned int   const cols,
           unsigned int   const rows,
           struct font2 * const fontP,
           unsigned int   const hmargin,
           unsigned int   const vmargin,
           struct Text    const formattedText,
           unsigned int   const maxleftb,
           float          const space,
           int            const lspace,
           bool           const fixedAdvance,
           FILE *         const ofP) {

    bit ** const bits = pbm_allocarray(pbm_packed_bytes(cols), rows);

    /* Fill background with white */
    clearBackground(bits, cols, rows);

    /* Put the text in  */
    insertCharacters(bits, formattedText, fontP, vmargin, hmargin + maxleftb,
                     space, cols, rows, lspace, fixedAdvance);

    /* Free all font data */
    pbm_destroybdffont2(fontP); 

    {
        unsigned int row;

        pbm_writepbminit(ofP, cols, rows, 0);

        for (row = 0; row < rows; ++row)
            pbm_writepbmrow_packed(ofP, bits[row], cols, 0);
    }

    pbm_freearray(bits, rows);
}



static PM_WCHAR const * sheetTextArray[] = {
L"M \",/^_[`jpqy| M",
L"                ",
L"/  !\"#$%&'()*+ /",
L"< ,-./01234567 <",
L"> 89:;<=>?@ABC >",
L"@ DEFGHIJKLMNO @",
L"_ PQRSTUVWXYZ[ _",
L"{ \\]^_`abcdefg {",
L"} hijklmnopqrs }",
L"~ tuvwxyz{|}~  ~",
L"                ",
L"M \",/^_[`jpqy| M" };



static void
validateText(const PM_WCHAR ** const textArray,
             struct font2    * const fontP) {
/*----------------------------------------------------------------------------
   Abort the program if there are characters in 'textArray' which cannot be
   rendered in font *fontP.
-----------------------------------------------------------------------------*/
    const PM_WCHAR * output;
    unsigned int textRow;

    for (textRow = 0; textRow < 12; ++textRow)
        fixControlChars(textArray[textRow], fontP, &output, QUIT);

    free((PM_WCHAR *)output);
}



static void
renderSheet(struct font2 * const fontP,
            FILE *         const ofP) {

    int const cols  = fontP->maxwidth  * 16;
    int const rows  = fontP->maxheight * 12;
    struct Text const sheetText =
        { (PM_WCHAR ** const) sheetTextArray, 12, 12};

    validateText(sheetTextArray, fontP);

    renderText(cols, rows, fontP, 0, 0, sheetText, MAX(-(fontP->x),0),
               0.0, 0, TRUE, ofP);
}



static void
dryrunOutput(unsigned int const cols,
             unsigned int const rows,
             FILE *       const ofP) {

    fprintf(ofP, "%u %u\n", cols, rows);
}



static void
textDumpOutput(struct Text   const lp,
               FILE *        const ofP) {
/*----------------------------------------------------------------------------
   Output the text 'lp' as characters.  (Do not render.)

   Note that the output stream is wide-oriented; it cannot be mixed with
   narrow-oriented output.  The libnetpbm library functions are
   narrow-oriented.  Thus, when this output is specified, it must not be mixed
   with any output from the library; it should be the sole output.
-----------------------------------------------------------------------------*/
    int rc;

    rc = fwide(ofP, 1);
    if (rc != 1) {
        /* This occurs when narrow-oriented output to ofP happens before we
           get here.
        */
        pm_error("Failed to set output stream to wide "
                 "(fwide() returned %d.  Maybe the output file "
                 "was written in narrow mode before this program was invoked?",
                 rc);
    } else {
        unsigned int line;  /* Line number in input text */

        for (line = 0; line < lp.lineCount; ++line) {
            fputws(lp.textArray[line], ofP);
            fputwc(L'\n', ofP);
        }
    }
}



static void
pbmtext(struct CmdlineInfo const cmdline,
        struct font2 *     const fontP,
        FILE *             const ofP) {

    unsigned int rows, cols;
        /* Dimensions in pixels of the output image */
    unsigned int cols0;
    unsigned int vmargin, hmargin;
        /* Margins in pixels we add to the output image */
    unsigned int hmargin0;
    struct Text inputText;
    struct Text formattedText;
    unsigned int maxleftb, maxleftb0;

    getText(cmdline.text, fontP, &inputText,
            cmdline.verbose ? WARN : SILENT);

    computeMargins(cmdline, inputText, fontP, &vmargin, &hmargin0);

    formatText(cmdline, inputText, fontP, hmargin0,
               &formattedText, &maxleftb0);

    if (formattedText.lineCount == 0)
        pm_error("No input text");

    computeImageHeight(formattedText, fontP, cmdline.lspace, vmargin, &rows);

    computeImageWidth(formattedText, fontP, cmdline.space,
                      cmdline.width > 0 ? 0 : hmargin0, &cols0, &maxleftb);

    if (cols0 == 0 || rows == 0)
        pm_error("Input is all whitespace and/or non-renderable characters.");

    if (cmdline.width == 0) {
        cols    = cols0;
        hmargin = hmargin0;
    } else {
        if (cmdline.width < cols0)
            pm_error("internal error: calculated image width (%u) exceeds "
                     "specified -width value: %u",
                     cols0, cmdline.width);
        else if (maxleftb0 != maxleftb)
            pm_error("internal error: contradicting backup values");
        else {
            hmargin = MIN(hmargin0, (cmdline.width - cols0) / 2);
            cols = cmdline.width;
        }
    }

    if (cmdline.dryrun)
        dryrunOutput(cols, rows, ofP);
    else if (cmdline.textdump)
        textDumpOutput(formattedText, ofP);
    else
        renderText(cols, rows, fontP, hmargin, vmargin, formattedText,
                   maxleftb, cmdline.space, cmdline.lspace, FALSE, ofP);

    freeTextArray(formattedText);
}



int
main(int argc, const char *argv[]) {

    struct CmdlineInfo cmdline;
    struct font2 * fontP;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    if (cmdline.wchar) {
        char * newLocale;
        newLocale = setlocale(LC_ALL, "");
        if (!newLocale)
            pm_error("Failed to set locale (LC_ALL) from environment");

        /* Orient standard input stream to wide */
        fwide(stdin,  1);
    } else
        fwide(stdin, -1);

    if (cmdline.verbose)
        pm_message("LC_CTYPE is set to '%s'", setlocale(LC_CTYPE, NULL) );

    computeFont(cmdline, &fontP);

    if (cmdline.dumpsheet)
        renderSheet(fontP, stdout);
    else
        pbmtext(cmdline, fontP, stdout);

    pm_close(stdout);

    return 0;
}



