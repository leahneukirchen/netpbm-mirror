/*
**
** Font routines.
**
** Wide character stuff written by Akira Urushibata in 2018 and contributed
** to the public domain.
**
** BDF font code by George Phillips, copyright 1993
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

#include "netpbm/pm_c_util.h"
#include "netpbm/mallocvar.h"
#include "netpbm/nstring.h"

#include "pbmfont.h"
#include "pbm.h"

/*----------------------------------------------------------------------------
  Font selector routines

  The selector is a device consisting of a bitmap, min value, max value and
  count.  It is used here to specify necessary fonts and record what entries
  are valid in the glyph array.

  This device may be used for other purposes.  In that case the code should
  be put into an independent source file in the lib/util subdirectory.
-----------------------------------------------------------------------------*/

static void
allocRecord(struct pm_selector * const selectorP,
            unsigned int         const max) {

    unsigned int const size = (max + 8) / 8;

    MALLOCARRAY(selectorP->localRecord, size);

    if (!selectorP->localRecord)
        pm_error("Failed to allocate %u bytes of memory for font selector "
                 "bitmap", size);

    selectorP->record = selectorP->localRecord;
}



void
pm_selector_create(unsigned int          const max,
                   struct pm_selector ** const selectorPP) {

    struct pm_selector * selectorP;

    MALLOCVAR_NOFAIL(selectorP);

    allocRecord(selectorP, max);

    {
        unsigned int byteIndex;
        for (byteIndex = 0; byteIndex <= max/8; ++byteIndex)
            selectorP->localRecord[byteIndex]= 0x00;
    }

    selectorP->maxmax = selectorP->min = max;
    selectorP->max = 0;
    selectorP->count = 0;

    *selectorPP = selectorP;
}



void
pm_selector_create_fixed(const unsigned char * const record,
                         unsigned int          const min,
                         unsigned int          const max,
                         unsigned int          const count,
                         struct pm_selector ** const selectorPP) {

    struct pm_selector * selectorP;

    MALLOCVAR_NOFAIL(selectorP);

    selectorP->localRecord = NULL;
    selectorP->record      = record;
    selectorP->min         = min;
    selectorP->max         = max;
    selectorP->maxmax      = max;
    selectorP->count       = count;

    *selectorPP = selectorP;
}



void
pm_selector_destroy(struct pm_selector * const selectorP) {

    if (selectorP->localRecord)
        free(selectorP->localRecord);

    free(selectorP);
}



void
pm_selector_copy(unsigned int               const max,
                 const struct pm_selector * const srcSelectorP,
                 struct pm_selector      ** const destSelectorPP) {

    /* Create a new selector and copy into it the content of another */

    struct pm_selector * destSelectorP;

    if (max < srcSelectorP->max)
        pm_error("internal error: attempt to copy a selector as "
                 "another with a smaller max value %u -> %u",
                 srcSelectorP->max, max);

    MALLOCVAR_NOFAIL(destSelectorP);

    destSelectorP->maxmax     = max;
    destSelectorP->max        = srcSelectorP->max;
    destSelectorP->min        = srcSelectorP->min;
    destSelectorP->count      = srcSelectorP->count;

    allocRecord(destSelectorP, max);

    {
        unsigned int const minByteIndex = srcSelectorP->min / 8;
        unsigned int const maxByteIndex = srcSelectorP->max / 8;
        unsigned int const maxmaxByteIndex = max / 8;

        unsigned int byteIndex;

        for (byteIndex = 0 ; byteIndex < minByteIndex; ++byteIndex)
            destSelectorP->localRecord[byteIndex] = 0x00;
        for (byteIndex = maxByteIndex + 1 ; byteIndex <= maxmaxByteIndex;
             ++byteIndex)
            destSelectorP->localRecord[byteIndex] = 0x00;
        for (byteIndex = minByteIndex; byteIndex <= maxByteIndex; ++byteIndex)
            destSelectorP->localRecord[byteIndex] =
                srcSelectorP->record[byteIndex];
    }

    *destSelectorPP = destSelectorP;
}



void
pm_selector_mark(struct pm_selector * const selectorP,
                 unsigned int         const index) {
/*----------------------------------------------------------------------------
   Mark index 'index'.
-----------------------------------------------------------------------------*/
    unsigned int  byteIndex = index / 8;
    unsigned int  bitIndex  = index % 8;
    unsigned char mask = (0x01 <<7) >> bitIndex;

    /* set bit on to indicate presence of this index */

    if (!selectorP->localRecord)
        pm_error("INTERNAL ERROR: attempt to mark in a fixed pm_selector");

    if ((selectorP->localRecord[byteIndex] & mask) == 0x00) {
        /* if bit is not already set */

        selectorP->localRecord[byteIndex] |= mask;
        ++selectorP->count;  /* increment count */

        /* reset min and max */
        if (selectorP->min > index)
            selectorP->min = index;
        if (selectorP->max < index)
            selectorP->max = index;
    }
}

/* There is no function for erasing a marked bit */


int  /* boolean */
pm_selector_is_marked(const struct pm_selector * const selectorP,
                      unsigned int               const index) {
/*----------------------------------------------------------------------------
  Index 'index' is marked.
-----------------------------------------------------------------------------*/
    bool retval;

    if (selectorP) {
        unsigned int  const byteIndex = index / 8;
        unsigned int  const bitIndex  = index % 8;
        unsigned char const mask = (0x01 <<7) >> bitIndex;

        if ( index < selectorP->min || index > selectorP->max)
            retval = false;
        else if ((selectorP->record[byteIndex] & mask) != 0x00)
            retval = true;
        else
            retval = false;
    } else {
        /* All entries are set to "exist" */
        retval = true;
    }
    return retval ? 1 : 0;
}



unsigned int
pm_selector_marked_ct(const struct pm_selector * const selectorP) {
/*----------------------------------------------------------------------------
   Number of indices that are marked.
-----------------------------------------------------------------------------*/
    return selectorP->count;
}



/*----------------------------------------------------------------------------
  Routines for loading a BDF font file
-----------------------------------------------------------------------------*/

/* The following are not recognized in individual glyph data; library
   routines do a pm_error if they see one:

   Vertical writing systems: DWIDTH1, SWIDTH1, VVECTOR, METRICSET,
   CONTENTVERSION.

   The following is not recognized and is thus ignored at the global level:
   DWIDTH
*/


#define MAXBDFLINE 1024

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
    const char * arg[7];
        /* These are the words; each entry is a pointer into line[] (above) */

    unsigned int wordCt;
} Readline;



static void
readline_init(Readline * const readlineP,
              FILE *     const ifP) {

    readlineP->ifP = ifP;

    readlineP->arg[0] = NULL;
    readlineP->wordCt = 0;
}



static void
tokenize(char *         const s,
         const char **  const words,
         unsigned int   const wordsSz,
         unsigned int * const wordCtP) {
/*----------------------------------------------------------------------------
   Chop up 's' into words by changing space characters to NUL.  Return as
   'words' an array of pointers to the beginnings of those words in 's'.
   Terminate the words[] list with a null pointer.

   'wordsSz' is the number of elements of space in 'words'.  If there are more
   words in 's' than will fit in that space (including the terminating null
   pointer), ignore the excess on the right.

   '*wordCtP' is the number elements actually found.
-----------------------------------------------------------------------------*/
    unsigned int n;  /* Number of words in words[] so far */
    char * p;

    p = &s[0];
    n = 0;

    while (*p) {
        if (!ISGRAPH(*p)) {
            if(!ISSPACE(*p)) {
              /* Control chars excluding 09 - 0d (space), 80-ff */
            pm_message("Warning: non-ASCII character '%x' in "
                       "BDF font file", *p);
            }
            *p++ = '\0';
        }
        else {
            words[n++] = p;
            if (n >= wordsSz - 1)
                break;
            while (*p && ISGRAPH(*p))
                ++p;
        }
    }
    assert(n <= wordsSz - 1);
    words[n] = NULL;
    *wordCtP = n;
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
            tokenize(readlineP->line, readlineP->arg,
                     ARRAY_SIZE(readlineP->arg), &readlineP->wordCt);
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
        /* ATTRIBUTES is defined in Glyph Bitmap Distribution Format (BDF)
           Specification Version 2.1, but not in Version 2.2.
        */
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
validateWordCount(Readline *    const readlineP,
                  unsigned int  const nWords) {

    if( readlineP->wordCt != nWords )
        pm_error("Wrong number of arguments in '%s' line in BDF font file",
                 readlineP->arg[0]);

    /* We assume that the first word in line 'arg[0]' is a valid string */

}


static void
readExpectedStatement(Readline *    const readlineP,
                      const char *  const expected,
                      unsigned int  const nWords) {
/*----------------------------------------------------------------------------
  Have the readline object *readlineP read the next line from the file, but
  expect it to be a line of type 'expected' (i.e. the verb token at the
  beginning of the line is that, e.g. "STARTFONT").  Check for the number
  of words: 'nWords'.  If either condition is not met, fail the program.
-----------------------------------------------------------------------------*/

    bool eof;

    readline_read(readlineP, &eof);

    if (eof)
        pm_error("EOF in BDF font file where '%s' expected", expected);
    else if (!streq(readlineP->arg[0], expected))
        pm_error("Statement of type '%s' where '%s' expected in BDF font file",
                 readlineP->arg[0], expected);

    validateWordCount(readlineP, nWords);

}



static void
skipCharacter(Readline * const readlineP) {
/*----------------------------------------------------------------------------
  In the BDF font file being read by readline object *readlineP, skip
  through to the end of the data for the character we are presently in.

  At entry the stream must be positioned at the end of the ENCODING line.
-----------------------------------------------------------------------------*/

    char * rc;
    do {
        rc = fgets(readlineP->line, MAXBDFLINE+1, readlineP->ifP);
        readlineP->line[7] = '\0';

    } while (rc != NULL && !streq(readlineP->line, "ENDCHAR"));

    if (rc == NULL)
        pm_error("End of file in the middle of a character (before "
                 "ENDCHAR) in BDF font file.");

}



static int
wordToInt(const char * const word) {

    unsigned int absValue;

    int retval;

    const char * error;
    const int sign = (word[0] == '-') ? -1 : +1;
    const char * const absString = (sign == -1) ? &word[1] : word;
    /* No leading spaces allowed in 'word' */

    if (!ISDIGIT(absString[0]))
      error = "Non-digit character encountered";

    else {
        pm_string_to_uint(absString, &absValue, &error);
        if (error == NULL && absValue > INT_MAX)
            error = "Out of range";
    }

    if (error != NULL)
        pm_error ("Error reading numerical argument in "
                  "BDF font file: %s %s %s", error, word, absString);

    retval = sign * absValue;
    assert (INT_MIN < retval && retval < INT_MAX);

    return retval;
}



static void
interpEncoding(const char **  const arg,
               unsigned int * const codepointP,
               bool *         const badCodepointP,
               PM_WCHAR       const maxmaxglyph) {
/*----------------------------------------------------------------------------
   With arg[] being the ENCODING statement from the font, return as
   *codepointP the codepoint that it indicates (code point is the character
   code, e.g. in ASCII, 48 is '0').

   But if the statement doesn't give an acceptable codepoint return
   *badCodepointP == TRUE.

   'maxmaxglyph' is the maximum codepoint in the font.
-----------------------------------------------------------------------------*/
    bool gotCodepoint = false;   /* initial value */
    bool badCodepoint;
    unsigned int codepoint;

    if (wordToInt(arg[1]) >= 0) {
        codepoint = wordToInt(arg[1]);
        gotCodepoint = true;
    } else {
        if (wordToInt(arg[1]) == -1 && arg[2] != NULL) {
            int const codepoint0 = wordToInt(arg[2]);
            if (codepoint0 >= 0) {
                codepoint = codepoint0;
                gotCodepoint = true;
            }
        }
    }

    if (gotCodepoint) {
        if (codepoint > maxmaxglyph)
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
             bool *         const badCodepointP,
             PM_WCHAR       const maxmaxglyph) {

    bool eof;
    const char * expected = "ENCODING";

    readline_read(readlineP, &eof);

    if (eof)
        pm_error("EOF in BDF font file where '%s' expected", expected);
    else if (!streq(readlineP->arg[0], expected))
        pm_error("Statement of type '%s' where '%s' expected in BDF font file",
                 readlineP->arg[0], expected);
    else if(readlineP->wordCt != 2 &&  readlineP->wordCt != 3)
        pm_error("Wrong number of arguments in '%s' line in BDF font file",
                 readlineP->arg[0]);

    interpEncoding(readlineP->arg, codepointP, badCodepointP, maxmaxglyph);
}



static void
validateFontLimits(const struct font2 * const font2P) {

    assert(pbm_maxfontheight() > 0 && pbm_maxfontwidth() > 0);

    if (font2P->maxwidth  <= 0 ||
        font2P->maxheight <= 0 ||
        font2P->maxwidth  > pbm_maxfontwidth()  ||
        font2P->maxheight > pbm_maxfontheight() ||
        -font2P->x + 1 > font2P->maxwidth ||
        -font2P->y + 1 > font2P->maxheight ||
        font2P->x > font2P->maxwidth  ||
        font2P->y > font2P->maxheight ||
        font2P->x + font2P->maxwidth  > pbm_maxfontwidth() ||
        font2P->y + font2P->maxheight > pbm_maxfontheight()
        ) {

        pm_error("Global font metric(s) out of bounds.");
    }

    if (font2P->maxglyph > PM_FONT2_MAXGLYPH)
        pm_error("Internal error.  Glyph table too large: %u glyphs; "
                 "Maximum possible in Netpbm is %u",
                 (unsigned int) font2P->maxglyph, PM_FONT2_MAXGLYPH);
}



static void
validateGlyphLimits(const struct font2 * const font2P,
                    const struct glyph * const glyphP,
                    const char *         const charName) {

    /* Some BDF files code space with zero width and height,
       no bitmap data and just the xadd value.
       We allow zero width and height, iff both are zero.

       Some BDF files have individual glyphs with a BBX value which
       exceeds the global maximum stated by FONTBOUNDINGBOX.
       Abort with error when this is encountered.
       It seems some programs including emacs and bdftopcf tolerate
       this violation.
    */

    if (((glyphP->width == 0 || glyphP->height == 0) &&
         !(glyphP->width == 0 && glyphP->height == 0)) ||
        glyphP->width  > font2P->maxwidth  ||
        glyphP->height > font2P->maxheight ||
        glyphP->x < font2P->x ||
        glyphP->y < font2P->y ||
        glyphP->x + (int) glyphP->width  > font2P->x + font2P->maxwidth  ||
        glyphP->y + (int) glyphP->height > font2P->y + font2P->maxheight ||
        glyphP->xadd > pbm_maxfontwidth() ||
        glyphP->xadd + MAX(glyphP->x,0) + (int) glyphP->width >
        pbm_maxfontwidth()
        ) {

        pm_error("Font metric(s) for char '%s' out of bounds.\n", charName);
    }
}



static void
readStartchar(Readline * const readlineP,
              const char ** charNameP) {
        
        const char * charName;
        bool eof;

        readline_read(readlineP, &eof);
        if (eof)
            pm_error("End of file after CHARS reading BDF font file");

        while (streq(readlineP->arg[0], "COMMENT")) {
            readline_read(readlineP, &eof);
            if (eof)
                 pm_error("End of file after CHARS reading BDF font file");
            /* ignore */
        }

        if (!streq(readlineP->arg[0], "STARTCHAR"))
            pm_error("%s detected where \'STARTCHAR\' expected "
                     "in BDF font file", readlineP->arg[0] );
        else if (readlineP->wordCt < 2)
            pm_error("Wrong number of arguments in STARTCHAR line "
                      "in BDF font file");
            /* Character name may contain spaces: there may be more than
               three words in the line.
             */
        else
            charName = pm_strdup(readlineP->arg[1]);

        *charNameP = charName;
}



static void
readGlyph(Readline * const readlineP,
          const char * const charName,
          const struct font2 * const font2P,
          struct glyph ** const glyphPP) {

    struct glyph * glyphP;
    MALLOCVAR(glyphP);
    if (glyphP == NULL)
        pm_error("no memory for font glyph for '%s' character",
             charName);

    readExpectedStatement(readlineP, "SWIDTH", 3);

    readExpectedStatement(readlineP, "DWIDTH", 3);
    glyphP->xadd = wordToInt(readlineP->arg[1]);

    readExpectedStatement(readlineP, "BBX", 5);
    glyphP->width  = wordToInt(readlineP->arg[1]);
    glyphP->height = wordToInt(readlineP->arg[2]);
    glyphP->x      = wordToInt(readlineP->arg[3]);
    glyphP->y      = wordToInt(readlineP->arg[4]);

    validateGlyphLimits(font2P, glyphP, charName);

    createBmap(glyphP->width, glyphP->height, readlineP, charName,
               &glyphP->bmap);

    *glyphPP = glyphP;
}



static void
processChars(Readline *     const readlineP,
             struct font2 * const font2P) {
/*----------------------------------------------------------------------------
   Process the CHARS block in a BDF font file, assuming the file is positioned
   just after the CHARS line.  Read the rest of the block and apply its
   contents to *font2P.
-----------------------------------------------------------------------------*/
    unsigned int const nCharacters = wordToInt(readlineP->arg[1]);
    unsigned int const nCharsWanted = (font2P->selectorP != NULL) ?
        font2P->selectorP->count : nCharacters;

    unsigned int nCharsEncountered;
    unsigned int nCharsLoaded;

    for (nCharsEncountered = 0, nCharsLoaded = 0;
         nCharsEncountered < nCharacters && nCharsLoaded < nCharsWanted;
         ++nCharsEncountered ) {

        const char * charName;
        unsigned int codepoint;
        bool badCodepoint;

        readStartchar(readlineP, &charName);

        readEncoding(readlineP, &codepoint, &badCodepoint,
                         font2P->maxmaxglyph);

        if ( badCodepoint ) {
            skipCharacter(readlineP);
            pm_strfree (charName);
            }
        else if (!pm_selector_is_marked(font2P->selectorP, codepoint) ) {
            skipCharacter(readlineP);
            pm_strfree (charName);
            font2P->maxglyph = codepoint;
            }
        else {
            if (codepoint < font2P->maxglyph) {
                if (font2P->glyph[codepoint] != NULL )
                       pm_error("Multiple definition of code point %u "
                                "in BDF font file", (unsigned int) codepoint);
                else
                       pm_message("Reverse order detected in BDF file. "
                                  "Code point %u defined after %u",
                                   (unsigned int) codepoint,
                                   (unsigned int) font2P->maxglyph);
            }

            {
            struct glyph * glyphP;

            readGlyph(readlineP, charName, font2P, &glyphP);
            readExpectedStatement(readlineP, "ENDCHAR", 1);

            assert(codepoint <= font2P->maxmaxglyph);
            /* Ensured by readEncoding() */

            font2P->glyph[codepoint] = glyphP;
            pm_strfree(charName);

            font2P->maxglyph = codepoint;
            ++nCharsLoaded;
            }
        }
    }

    /* Note that BDF file may have reached end before the largest entry
       in selector was checked.  */
    /*
    if (font2P->selectorP->max > font2P->maxglyph)
       font2P->maxglyph = font2P->selectorP->max;
    */
    font2P->chars       = nCharsLoaded;
    font2P->total_chars = nCharacters;
}



static void
processBdfFontNameLine(Readline     * const readlineP,
                       struct font2 * const font2P) {

    if (font2P->name)  /* We've already processed a FONT line */
        pm_error("Multiple FONT lines in BDF font file");
    else {
        char * buffer;

        MALLOCARRAY(buffer, MAXBDFLINE+1);

        if (!buffer)
            pm_error("Failed to get memory for %u-character font name buffer",
                     MAXBDFLINE+1);

        if (readlineP->wordCt == 1)
            strcpy(buffer, "(no name)");

        else {
            unsigned int tokenCt;

            buffer[0] ='\0';

            for (tokenCt=1;
                 tokenCt < ARRAY_SIZE(readlineP->arg) &&
                     readlineP->arg[tokenCt] != NULL; ++tokenCt) {
                strcat(buffer, " ");
                strcat(buffer, readlineP->arg[tokenCt]);
            }
        }
        font2P->name = buffer;
    }
}



static void
loadCharsetString(const char *  const registry,
                  const char *  const encoding,
                  const char ** const charsetStringP) {

    char * dest;
    unsigned int inCt, outCt;

    dest = malloc(strlen(registry) + strlen(encoding) + 1);

    if (!dest)
        pm_error("no memory to load CHARSET_REGISTRY and CHARSET_ENCODING "
               "from BDF file");

    for (inCt = outCt = 0; inCt < strlen(registry); ++inCt) {
        char const c = registry[inCt];
        if (isgraph(c) && c != '"')
            dest[outCt++] = c;
    }
    dest[outCt++] = '-';

    for (inCt = 0; inCt < strlen(encoding); ++inCt) {
        char const c = encoding[inCt];
        if (isgraph(c) && c != '"')
            dest[outCt++] = c;
    }

    dest[outCt] = '\0';
    *charsetStringP = dest;
}


static unsigned int const maxTokenLen = 60;

static void
doCharsetRegistry(Readline *    const readlineP,
                  bool *        const gotRegistryP,
                  const char ** const registryP) {

    if (*gotRegistryP)
        pm_error("Multiple CHARSET_REGISTRY lines in BDF font file");
    else if (readlineP->arg[2] != NULL)
        pm_message("CHARSET_REGISTRY in BDF font file is not "
                   "a single word.  Ignoring extra element(s) %s ...",
                   readlineP->arg[2]);
    else if (strlen(readlineP->arg[1]) > maxTokenLen)
        pm_message("CHARSET_REGISTRY in BDF font file is too long. "
                   "Truncating");

    *registryP = pm_strdup(readlineP->arg[1]);
    *gotRegistryP = true;
}



static void
doCharsetEncoding(Readline *    const readlineP,
                  bool *        const gotEncodingP,
                  const char ** const encodingP) {

    if (*gotEncodingP)
        pm_error("Multiple CHARSET_ENCODING lines in BDF font file");
    else if (readlineP->arg[2] != NULL)
        pm_message("CHARSET_ENCODING in BDF font file is not "
                   "a single word.  Ignoring extra element(s) %s ...",
                   readlineP->arg[2]);
    else if (strlen(readlineP->arg[1]) > maxTokenLen)
        pm_message("CHARSET_ENCODING in BDF font file is too long. "
                   "Truncating");

    *encodingP = pm_strdup(readlineP->arg[1]);
    *gotEncodingP = true;
}



static void
doDefaultChar(Readline * const readlineP,
              bool *     const gotDefaultCharP,
              PM_WCHAR * const defaultCharP) {

    if (*gotDefaultCharP)
        pm_error("Multiple DEFAULT_CHAR lines in BDF font file");
    else if (readlineP->arg[1] == NULL)
        pm_error("Malformed DEFAULT_CHAR line in BDF font file");
    else {
        *defaultCharP = (PM_WCHAR) wordToInt(readlineP->arg[1]);
        *gotDefaultCharP = true;
    }
}



static void
processBdfPropertyLine(Readline     * const readlineP,
                       struct font2 * const font2P) {

    bool gotRegistry;
    const char * registry;
    bool gotEncoding;
    const char * encoding;
    bool gotDefaultChar;
    PM_WCHAR defaultChar;
    unsigned int propCt;
    unsigned int commentCt;
    unsigned int propTotal;

    validateWordCount(readlineP, 2);   /* STARTPROPERTIES n */

    propTotal = wordToInt(readlineP->arg[1]);

    gotRegistry    = false;  /* initial value */
    gotEncoding    = false;  /* initial value */
    gotDefaultChar = false;  /* initial value */

    propCt    = 0;  /* initial value */
    commentCt = 0;  /* initial value */

    do {
        bool eof;

        readline_read(readlineP, &eof);
        if (eof)
            pm_error("End of file after STARTPROPERTIES in BDF font file");
        else if (streq(readlineP->arg[0], "CHARSET_REGISTRY") &&
                 readlineP->arg[1] != NULL) {
            doCharsetRegistry(readlineP, &gotRegistry, &registry);
        } else if (streq(readlineP->arg[0], "CHARSET_ENCODING") &&
                   readlineP->arg[1] != NULL) {
            doCharsetEncoding(readlineP, &gotEncoding, &encoding);
        } else if (streq(readlineP->arg[0], "DEFAULT_CHAR")) {
            doDefaultChar(readlineP, &gotDefaultChar, &defaultChar);
        } else if (streq(readlineP->arg[0], "COMMENT")) {
            ++commentCt;
        }
        ++propCt;

    } while (!streq(readlineP->arg[0], "ENDPROPERTIES"));

    --propCt; /* Subtract one for ENDPROPERTIES line */

    if (propCt != propTotal && propCt - commentCt != propTotal)
      /* Some BDF files have COMMENTs in the property section and leave
         them out of the count.
         Others just give a wrong count.
       */
        pm_message ("Note: wrong number of property lines in BDF font file. "
                    "STARTPROPERTIES line says %u, actual count: %u. "
                    "Proceeding.",
                    propTotal, propCt);


    if (gotRegistry && gotEncoding)
        loadCharsetString(registry, encoding, &font2P->charset_string);
    else if (gotRegistry != gotEncoding) {
        pm_message ("CHARSET_%s absent or incomplete in BDF font file. "
                    "Ignoring CHARSET_%s.",
                    gotEncoding ? "REGISTRY" : "ENCODING",
                    gotEncoding ? "ENCODING" : "REGISTRY");
    }
    if (gotRegistry)
        pm_strfree(registry);
    if (gotEncoding)
        pm_strfree(encoding);

    if (gotDefaultChar) {
        font2P->default_char         = defaultChar;
        font2P->default_char_defined = true;
    }

}


static void
processBdfFontLine(Readline     * const readlineP,
                   struct font2 * const font2P,
                   bool         * const endOfFontP) {
/*----------------------------------------------------------------------------
   Process a nonblank line just read from a BDF font file.

   This processing may involve reading more lines.
-----------------------------------------------------------------------------*/
    *endOfFontP = FALSE;  /* initial assumption */

    assert(readlineP->arg[0] != NULL);  /* Entry condition */

    if (streq(readlineP->arg[0], "FONT")) {
        processBdfFontNameLine(readlineP, font2P);
    } else if (streq(readlineP->arg[0], "COMMENT")) {
        /* ignore */
    } else if (streq(readlineP->arg[0], "SIZE")) {
        /* ignore */
    } else if (streq(readlineP->arg[0], "STARTPROPERTIES")) {
      if (font2P->maxwidth == 0)
      pm_error("Encountered STARTROPERTIES before FONTBOUNDINGBOX "
               "in BDF font file");
      else
        processBdfPropertyLine(readlineP, font2P);
    } else if (streq(readlineP->arg[0], "FONTBOUNDINGBOX")) {
        validateWordCount(readlineP,5);

        font2P->maxwidth  = wordToInt(readlineP->arg[1]);
        font2P->maxheight = wordToInt(readlineP->arg[2]);
        font2P->x = wordToInt(readlineP->arg[3]);
        font2P->y = wordToInt(readlineP->arg[4]);
        validateFontLimits(font2P);
    } else if (streq(readlineP->arg[0], "ENDFONT")) {
        *endOfFontP = true;
    } else if (streq(readlineP->arg[0], "CHARS")) {
      if (font2P->maxwidth == 0)
      pm_error("Encountered CHARS before FONTBOUNDINGBOX "
                   "in BDF font file");
      else {
        validateWordCount(readlineP, 2);  /* CHARS n */
        processChars(readlineP, font2P);
        if (font2P->selectorP != NULL &&
            font2P->selectorP->count == font2P->chars)
               *endOfFontP = true;
      }
    } else {
        /* ignore */
    }

}



static void
initializeGlyphArray(struct font2 * const font2P,
                     unsigned int   const maxmaxglyph) {
/*----------------------------------------------------------------------------
  Initialize glyph array based on entries in selector.
  Note that only valid codepoints are set to NULL.
  Entries for unused glyphs are left untouched.
-----------------------------------------------------------------------------*/
    const struct pm_selector * const selectorP = font2P->selectorP;
    unsigned int const min = (selectorP == NULL) ? 0 : selectorP->min;
    unsigned int const max =
        (selectorP == NULL) ? font2P->maxglyph : selectorP->max;

    unsigned int codepoint;

    for (codepoint = min; codepoint <= max ; ++codepoint)
        if (pm_selector_is_marked(selectorP, codepoint) == true)
             font2P->glyph[codepoint] = NULL;

    font2P->glyph[L' '] = NULL;
        /* Clear the slot for space character.
           It may not be defined in the font, but the program may try
           to use space as a substitute char
        */

}



struct font2 *
pbm_loadbdffont2select(const char *               const filename,
                       PM_WCHAR                   const maxmaxglyph,
                       const struct pm_selector * const selectorP) {
/*----------------------------------------------------------------------------
   Read a BDF font file "filename" as a 'font2' structure.  A 'font2'
   structure is more expressive than a 'font' structure, most notably in that
   it can handle wide code points and many more glyphs.

   Codepoints up to maxmaxglyph inclusive are valid in the file.

   The returned object is in new malloc'ed storage, in many pieces.
   When done with, destroy with pbm_destroybdffont2().

   The returned object refers to *selectorP, so that must continue to exist
   until you call pbm_destroybdffont2().
-----------------------------------------------------------------------------*/
    FILE *         ifP;
    Readline       readline;
    struct font2 * font2P;
    bool           endOfFont;

    ifP = fopen(filename, "rb");
    if (!ifP)
        pm_error("Unable to open BDF font file name '%s'.  errno=%d (%s)",
                 filename, errno, strerror(errno));

    readline_init(&readline, ifP);

    pbm_createbdffont2_base(&font2P, maxmaxglyph);

    font2P->maxglyph = 0;
        /* Initial value.  Increases as new characters are loaded */

    if (font2P->selectorP == NULL) {
        PM_WCHAR i;

        for(i = 0; i <= maxmaxglyph; ++i)
            font2P->glyph[i] = NULL;
            /* Initial value.  Overwrite later if codepoint i is defined. */
    }

    font2P->maxmaxglyph = maxmaxglyph;

    /* Initialize some values - to be overwritten if actual values are
       stated in BDF file */
    font2P->maxwidth = font2P->maxheight = font2P->x = font2P->y = 0;
    font2P->name = font2P->charset_string = NULL;
    font2P->chars = font2P->total_chars = 0;
    font2P->default_char = 0;
    font2P->default_char_defined = FALSE;
    font2P->selectorP = (struct pm_selector * const) selectorP;

    initializeGlyphArray(font2P, maxmaxglyph);

    readExpectedStatement(&readline, "STARTFONT", 2);

    endOfFont = FALSE;

    while (!endOfFont) {
        bool eof;
        readline_read(&readline, &eof);
        if (eof)
            pm_error("End of file before ENDFONT statement in BDF font file");

        processBdfFontLine(&readline, font2P, &endOfFont);
    }
    fclose(ifP);

    if(font2P->total_chars == 0)
        pm_error("No glyphs found in BDF font file "
                 "in codepoint range 0 - %u", (unsigned int) maxmaxglyph);
    if(font2P->chars == 0)
        pm_error("Not any requested glyphs found in BDF font file "
                 "in codepoint range 0 - %u", (unsigned int) maxmaxglyph);


    REALLOCARRAY(font2P->glyph, font2P->maxglyph + 1);

    font2P->bit_format = PBM_FORMAT;
    font2P->load_fn = LOAD_BDFFILE;
    font2P->charset = ENCODING_UNKNOWN;
    font2P->oldfont = NULL;  /* Legacy field */
    font2P->fcols = font2P->frows = 0;  /* Legacy fields */

    return font2P;
}


struct font2 *
pbm_loadbdffont2(const char * const filename,
                 PM_WCHAR     const maxmaxglyph) {

  return (pbm_loadbdffont2select(filename, maxmaxglyph, NULL));

  return(0);
}




static struct font *
font2ToFont(const struct font2 * const font2P) {

            struct font  * fontP;
            unsigned int   codePoint;

    MALLOCVAR(fontP);
    if (fontP == NULL)
        pm_error("no memory for font");

    fontP->maxwidth  = font2P->maxwidth;
    fontP->maxheight = font2P->maxheight;

    fontP->x = font2P->x;
    fontP->y = font2P->y;

    for (codePoint = 0; codePoint <= PM_FONT_MAXGLYPH; ++codePoint)
        fontP->glyph[codePoint] =
             pm_selector_is_marked(font2P->selectorP, codePoint) ?
             font2P->glyph[codePoint] : NULL;

    /* Give values to legacy fields */
    fontP->oldfont = font2P->oldfont;
    fontP->fcols = font2P->fcols;
    fontP->frows = font2P->frows;

    return fontP;
}



struct font *
pbm_loadbdffont(const char * const filename) {
/*----------------------------------------------------------------------------
   Read a BDF font file "filename" into a traditional font structure.

   Codepoints up to 255 (PM_FONT_MAXGLYPH) are valid.

   Can handle ASCII, ISO-8859-1, ISO-8859-2, ISO-8859-15, etc.

   The returned object is in new malloc'ed storage, in many pieces.
   Destroy with pbm_destroybdffont().
-----------------------------------------------------------------------------*/
    struct font  * fontP;
    struct font2 * const font2P = pbm_loadbdffont2(filename, PM_FONT_MAXGLYPH);

    fontP = font2ToFont(font2P);

    /* Free the base structure which was created by pbm_loadbdffont2() */
    pbm_destroybdffont2_base(font2P);

    return fontP;
}



