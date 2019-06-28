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

    for (i = 0, ng = 0; i < PM_FONT_MAXGLYPH +1; ++i) {
        if (fontP->glyph[i])
            ++ng;
    }

    printf("static struct glyph _g[%d] = {\n", ng);

    for (i = 0; i < PM_FONT_MAXGLYPH + 1; ++i) {
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

        for (i = 0; i < PM_FONT_MAXGLYPH + 1; ++i) {
            if (fontP->glyph[i])
                printf(" _g + %d", ng++);
            else
                printf(" NULL");

            if (i != PM_FONT_MAXGLYPH) printf(",");
            printf("\n");
        }
    }

    printf(" }\n};\n");
}



