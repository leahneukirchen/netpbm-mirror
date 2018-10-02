/*
**
** Font routines.
**
** Wide character stuff written by Akira Urushibata in 2018 and contributed
** to the public domain.
**
** Also copyright (C) 1991 by Jef Poskanzer and licensed to the public as
** follows.
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
#include "pbmfontdata.h"


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



struct font2 *
pbm_defaultfont2(const char * const requestedFontName) {

    struct font2 * font2P;
    struct font2 * retval = NULL; /* initial value */
    unsigned int i;

    for (i = 0; retval == NULL; ++i) {
        const char * longName;
        const char * shortName;
        font2P = (struct font2 * ) pbm_builtinFonts[i];
        if (font2P == NULL)
            break;

        longName = font2P->name;
        shortName = &longName[strlen("builtin ")];

        if (streq(shortName, requestedFontName))
             retval = font2P;
    }

    if (retval == NULL)
        pm_error("No builtin font named %s", requestedFontName);

    return retval;
}



static void
selectFontType(const    char * const filename,
               PM_WCHAR        const maxmaxglyph,
               unsigned int    const isWide,
               struct font  ** const fontPP,
               struct font2 ** const font2PP) {

    FILE * fileP;
    struct font  * fontP  = NULL; /* initial value */
    struct font2 * font2P = NULL; /* initial value */
    char line[10] = "\0\0\0\0\0\0\0\0\0\0";
        /* Initialize to suppress Valgrind error which is reported when file
           is empty or very small.
        */

    fileP = pm_openr(filename);
    fgets(line, 10, fileP);
    pm_close(fileP);

    if (line[0] == PBM_MAGIC1 &&
        (line[1] == PBM_MAGIC2 || line[1] == RPBM_MAGIC2)) {
        if (isWide == TRUE)
            font2P = pbm_loadpbmfont2(filename);
        else
            fontP  = pbm_loadpbmfont(filename);
        if (fontP == NULL && font2P == NULL)
            pm_error("could not load PBM font file");

    } else if (!strncmp(line, "STARTFONT", 9)) {
        if (isWide == TRUE)
            font2P = pbm_loadbdffont2(filename, maxmaxglyph);
        else
            fontP = pbm_loadbdffont(filename);
        if (fontP == NULL && font2P == NULL)
            pm_error("could not load BDF font file");

    } else {
        pm_error("font file not in a recognized format.  Does not start "
                 "with the signature of a PBM file or BDF font file");
        assert(false);
        fontP = NULL;  /* defeat compiler warning */
    }

    if (isWide)
        *font2PP = font2P;
    else
        *fontPP = fontP;
}



struct font *
pbm_loadfont(const    char * const filename) {

    struct font  * fontP;
    struct font2 * font2P;

    selectFontType(filename, PM_FONT_MAXGLYPH, FALSE, &fontP, &font2P);
    return fontP;
}



struct font2 *
pbm_loadfont2(const    char * const filename,
              PM_WCHAR        const maxmaxglyph) {

    struct font  * fontP;
    struct font2 * font2P;

    selectFontType(filename, maxmaxglyph, TRUE, &fontP, &font2P);
    return font2P;
}



void
pbm_createbdffont2_base(struct font2 ** const font2PP,
                        PM_WCHAR        const maxmaxglyph) {

    struct font2 * font2P;

    MALLOCVAR(font2P);
    if (font2P == NULL)
        pm_error("no memory for font");

    MALLOCARRAY(font2P->glyph, maxmaxglyph + 1);
    if (font2P->glyph == NULL)
        pm_error("no memory for font glyphs");

    /* Initialize */
    font2P->size = sizeof (struct font2);
    font2P->len  = PBM_FONT2_STRUCT_SIZE(charset_string);

    /*  Caller should overwrite following fields as necessary */
    font2P->oldfont = NULL;
    font2P->fcols = font2P->frows = 0;
    font2P->selector = NULL;
    font2P->default_char = 0;
    font2P->default_char_defined = FALSE;
    font2P->total_chars = font2P->chars = 0;
    font2P->name = NULL;
    font2P->charset = ENCODING_UNKNOWN;
    font2P->charset_string = NULL;

    *font2PP = font2P;
}



static void
destroyGlyphData(struct glyph ** const glyph,
                 PM_WCHAR        const maxglyph) {
/*----------------------------------------------------------------------------
  Free glyph objects and bitmap objects.

  This does not work when an object is "shared" through multiple pointers
  referencing an identical address and thus pointing to a common glyph
  or bitmap object.
-----------------------------------------------------------------------------*/

    PM_WCHAR i;

    for(i = 0; i <= maxglyph; ++i) {
        if (glyph[i]!=NULL) {
            free((void *) (glyph[i]->bmap));
            free(glyph[i]);
      }
    }
}


void
pbm_destroybdffont2_base(struct font2 * const font2P) {
/*----------------------------------------------------------------------------
  Free font2 structure, but not the glyph data
---------------------------------------------------------------------------- */

    free(font2P->selector);

    pm_strfree(font2P->name);
    pm_strfree(font2P->charset_string);
    free(font2P->glyph);

    if (font2P->oldfont !=NULL)
       pbm_freearray(font2P->oldfont, font2P->frows);

    free((void *)font2P);

}



void
pbm_destroybdffont2(struct font2 * const font2P) {
/*----------------------------------------------------------------------------
  Free font2 structure and glyph data

  Examines the 'load_fn' field to check whether the object is fixed data.
  Do nothing if 'load_fn' is 'FIXED_DATA'.
---------------------------------------------------------------------------- */

    if (font2P->load_fn != FIXED_DATA) {
        destroyGlyphData(font2P->glyph, font2P->maxglyph);
        pbm_destroybdffont2_base(font2P);
    }
}



void
pbm_destroybdffont(struct font * const fontP) {
/*----------------------------------------------------------------------------
  Free font structure and glyph data.

  For freeing a structure created by pbm_loadbdffont() or pbm_loadpbmfont().
---------------------------------------------------------------------------- */

    destroyGlyphData(fontP->glyph, PM_FONT_MAXGLYPH);

    if (fontP->oldfont !=NULL)
       pbm_freearray(fontP->oldfont, fontP->frows);

    free(fontP);
}



struct font2 *
pbm_expandbdffont(const struct font * const fontP) {
/*----------------------------------------------------------------------------
  Convert a traditional 'font' structure into an expanded 'font2' structure.

  After calling this function *fontP may be freed, but not the individual
  glyph data: fontP->glyph[0...255] .

  Using this function on static data is not recommended.  Rather add
  the extra fields to make a font2 structure.  See file pbmfontdata1.c
  for an example.

  The returned object is in new malloc'ed storage, in many pieces.

  Destroy with pbm_destroybdffont2() if *fontP is read from a file.

  Destroy with pbm_destroybdffont2_base() if *fontP is static data
  and you desire to defy the above-stated recommendation.

  The general function for conversion in the opposite direction
  'font2' => 'font' is font2ToFont() in libpbmfont2.c .  It is currently
  declared as static.
 ---------------------------------------------------------------------------*/
    PM_WCHAR maxglyph, codePoint;
    unsigned int nCharacters;
    struct font2 * font2P;

    pbm_createbdffont2_base(&font2P, PM_FONT_MAXGLYPH);

    font2P->maxwidth  = fontP->maxwidth;
    font2P->maxheight = fontP->maxheight;

    font2P->x = fontP->x;
    font2P->y = fontP->y;

    /* Hunt for max non-NULL entry in glyph table */
    for (codePoint = PM_FONT_MAXGLYPH;
         fontP->glyph[codePoint] == NULL && codePoint > 0; --codePoint)
        ;

    maxglyph = font2P->maxglyph = codePoint;
    assert (0 <= maxglyph && maxglyph <= PM_FONT_MAXGLYPH);

    if (maxglyph == 0 && fontP->glyph[0] == NULL)
        pm_error("no glyphs loaded");

    REALLOCARRAY(font2P->glyph, font2P->maxglyph + 1);

    for (codePoint = 0; codePoint <= maxglyph; ++codePoint) {
        font2P->glyph[codePoint] = fontP->glyph[codePoint];

        if (font2P->glyph[codePoint] != NULL)
           ++nCharacters;
    }

    font2P->oldfont = fontP->oldfont;
    font2P->fcols = fontP->fcols;
    font2P->frows = fontP->frows;

    font2P->bit_format = PBM_FORMAT;
    font2P->total_chars = font2P->chars = nCharacters;
    font2P->load_fn = CONVERTED_TYPE1_FONT;
    /* Caller should be overwrite the above to a more descriptive
       value */
    return font2P;
}



