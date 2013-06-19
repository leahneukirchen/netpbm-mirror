/* ----------------------------------------------------------------------
 *
 * Convert a Netpbm file to the GNU Octave image format
 * by Scott Pakin <scott+pbm@pakin.org>
 *
 * ----------------------------------------------------------------------
 *
 * Copyright information is at end of file.
 * ----------------------------------------------------------------------
 */

#include <assert.h>
#include <stdio.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "pam.h"
#include "pammap.h"

typedef struct {
    double comp[3];
        /* comp[0] is red; comp[1] is green; comp[2] is blue */
} octaveColor;

typedef struct {
    struct pam pam;
    unsigned int nColors;
    tuplehash hash;
    unsigned int paletteAlloc;
        /* 'palette' array has this many slots allocated.  Only the first
           'nColors' are meaningful.
        */
    octaveColor * palette;
    double normalizer;
        /* 1/maxval */
} cmap;



static void
initCmap(cmap * const cmapP,
         sample const maxval) {

    cmapP->pam.size             = sizeof(cmapP->pam.size);
    cmapP->pam.len              = PAM_STRUCT_SIZE(tuple_type);
    cmapP->pam.depth            = 3;
    cmapP->pam.maxval           = maxval;
    cmapP->pam.bytes_per_sample = pnm_bytespersample(maxval);

    cmapP->normalizer   = 1.0/maxval;
    cmapP->nColors      = 0;
    cmapP->paletteAlloc = 0;
    cmapP->palette      = NULL;
    cmapP->hash         = pnm_createtuplehash();
}



static void
termCmap(cmap * const cmapP) {
    pnm_destroytuplehash(cmapP->hash);

    free(cmapP->palette);
}



static void
findOrAddColor(tuple          const color,
               cmap *         const cmapP,
               unsigned int * const colorIndexP) {
/*----------------------------------------------------------------------------
  Return as *colorIndexP the colormap index of color 'color' in
  colormap *cmapP.  If the color isn't in the map, give it a new
  colormap index, put it in the colormap, and return that.
-----------------------------------------------------------------------------*/
    int found;
    int colorIndex;

    pnm_lookuptuple(&cmapP->pam, cmapP->hash, color, &found, &colorIndex);

    if (!found) {
        int fits;
        unsigned int plane;

        colorIndex = cmapP->nColors++;

        if (cmapP->nColors > cmapP->paletteAlloc) {
            cmapP->paletteAlloc *= 2;
            REALLOCARRAY(cmapP->palette, cmapP->nColors);
        }
        for (plane = 0; plane < 3; ++plane)
            cmapP->palette[colorIndex].comp[plane] =
                color[plane] * cmapP->normalizer;

        pnm_addtotuplehash(&cmapP->pam, cmapP->hash, color, colorIndex, &fits);

        if (!fits)
            pm_error("Out of memory constructing color map, on %uth color",
                     cmapP->nColors);
    }
    *colorIndexP = colorIndex;
}



static void
outputColormap(FILE * const ofP,
               cmap   const cmap) {
/*----------------------------------------------------------------------------
  Output the colormap as a GNU Octave matrix.
-----------------------------------------------------------------------------*/
    unsigned int colorIndex;

    fprintf(ofP, "# name: map\n");
    fprintf(ofP, "# type: matrix\n");
    fprintf(ofP, "# rows: %u\n", cmap.nColors);
    fprintf(ofP, "# columns: 3\n");

    for (colorIndex = 0; colorIndex < cmap.nColors; ++colorIndex) {
        unsigned int plane;

        assert(cmap.pam.depth == 3);

        for (plane = 0; plane < 3; ++plane)
            fprintf(ofP, " %.10f", cmap.palette[colorIndex].comp[plane]);

        fprintf(ofP, "\n");
    }
}



static void
convertToOctave(FILE * const ifP,
                FILE * const ofP) {

    struct pam inpam;
    tuple * inRow;
    unsigned int row;
    cmap cmap;

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(allocation_depth));

    pnm_setminallocationdepth(&inpam, 3);
    
    /* Output the image as a GNU Octave matrix.  For each row of the
     * input file we immediately output indexes into the colormap then,
     * when we're finished, we output the colormap as a second
     * matrix. */
    fprintf(ofP, "# name: img\n");
    fprintf(ofP, "# type: matrix\n");
    fprintf(ofP, "# rows: %u\n", inpam.height);
    fprintf(ofP, "# columns: %u\n", inpam.width);

    initCmap(&cmap, inpam.maxval);

    inRow = pnm_allocpamrow(&inpam);
    for (row = 0; row < inpam.height; ++row) {
        unsigned int col;
        pnm_readpamrow(&inpam, inRow);

        pnm_makerowrgb(&inpam, inRow);

        for (col = 0; col < inpam.width; ++col) {
            unsigned int colorIndex;
            findOrAddColor(inRow[col], &cmap, &colorIndex);
            fprintf(ofP, " %u", colorIndex + 1);
        }
        fprintf(ofP, "\n");
    }
    pm_message("%u colors in palette", cmap.nColors);

    pnm_freepamrow(inRow);
    outputColormap(ofP, cmap);

    termCmap(&cmap);
}



int
main(int argc, char *argv[]) {

    FILE * ifP;
    const char * inputName;

    pnm_init(&argc, argv);

    inputName = argc-1 > 0 ? argv[1] : "-";

    ifP = pm_openr(inputName);
    
    if (streq(inputName, "-"))
        fprintf(stdout, "# Created by pamtooctave\n");
    else
        fprintf(stdout, "# Created from '%s' by pamtooctave\n", inputName);

    convertToOctave(ifP, stdout);
    
    pm_close(ifP);

    return 0;
}



/*
 * Copyright (C) 2007 Scott Pakin <scott+pbm@pakin.org>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ----------------------------------------------------------------------
 */
