/* ----------------------------------------------------------------------
 *
 * Convert a PAM image to an AVS X image
 *
 * By Scott Pakin <scott+pbm@pakin.org>
 *
 * ----------------------------------------------------------------------
 *
 * Copyright (C) 2010 Scott Pakin <scott+pbm@pakin.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 *
 * ----------------------------------------------------------------------
 */

#include <stdio.h>
#include "pm.h"
#include "pam.h"



static char
sample2char(sample const s,
            sample const maxval) {
/* Scale down a sample to a single byte. */

    return maxval==255 ? s : s * 255 / maxval;
}


#define THIS_SAMPLE_CHAR(PLANE) \
  sample2char(tuplerow[col][PLANE], pamP->maxval)

static void
produceAvs(struct pam * const pamP,
           FILE *       const avsFileP) {

    tuple * tuplerow;

    /* Write the AVS header (image width and height as 4-byte
       big-endian integers).
    */
    pm_writebiglong(avsFileP, pamP->width);
    pm_writebiglong(avsFileP, pamP->height);

    /* Write the AVS data (alpha, red, green, blue -- one byte apiece. */
    tuplerow = pnm_allocpamrow(pamP);
    switch (pamP->depth) {
    case 1: {
        /* Black-and-white or grayscale, no alpha */
        unsigned int row;
        for (row = 0; row < pamP->height; ++row) {
            unsigned int col;
            pnm_readpamrow(pamP, tuplerow);
            for (col = 0; col < pamP->width; ++col) {
                pm_writechar(avsFileP, (char)255);
                pm_writechar(avsFileP, THIS_SAMPLE_CHAR(0));
                pm_writechar(avsFileP, THIS_SAMPLE_CHAR(0));
                pm_writechar(avsFileP, THIS_SAMPLE_CHAR(0));
            }
        }
    } break;

    case 2: {
        /* Black-and-white or grayscale plus alpha */
        unsigned int row;
        for (row = 0; row < pamP->height; ++row) {
            unsigned int col;
            pnm_readpamrow(pamP, tuplerow);
            for (col = 0; col < pamP->width; ++col) {
                pm_writechar(avsFileP, THIS_SAMPLE_CHAR(1));
                pm_writechar(avsFileP, THIS_SAMPLE_CHAR(0));
                pm_writechar(avsFileP, THIS_SAMPLE_CHAR(0));
                pm_writechar(avsFileP, THIS_SAMPLE_CHAR(0));
            }
        }
    } break;

    case 3: {
        /* RGB, no alpha */
        unsigned int row;
        for (row = 0; row < pamP->height; ++row) {
            unsigned int col;
            pnm_readpamrow(pamP, tuplerow);
            for (col = 0; col < pamP->width; ++col) {
                pm_writechar(avsFileP, (char)255);
                pm_writechar(avsFileP, THIS_SAMPLE_CHAR(0));
                pm_writechar(avsFileP, THIS_SAMPLE_CHAR(1));
                pm_writechar(avsFileP, THIS_SAMPLE_CHAR(2));
            }
        }
    } break;

    case 4: {
        /* RGB plus alpha */
        unsigned int row;
        for (row = 0; row < pamP->height; ++row) {
            unsigned int col;
            pnm_readpamrow( pamP, tuplerow );
            for (col = 0; col < pamP->width; ++col) {
                pm_writechar(avsFileP, THIS_SAMPLE_CHAR(3));
                pm_writechar(avsFileP, THIS_SAMPLE_CHAR(0));
                pm_writechar(avsFileP, THIS_SAMPLE_CHAR(1));
                pm_writechar(avsFileP, THIS_SAMPLE_CHAR(2));
            }
        }
    } break;

    default:
        pm_error("Unrecognized PAM depth %u.  We understand only "
                 "1, 2, 3, and 4", pamP->depth);
        break;
    }
    pnm_freepamrow(tuplerow);
}



int
main(int argc, const char *argv[]) {
    struct pam   inPam;
    const char * inputFilename;
    FILE       * inFileP;

    pm_proginit(&argc, argv);

    inputFilename = (argc > 1) ? argv[1] : "-";

    inFileP = pm_openr(inputFilename);

    pnm_readpaminit(inFileP, &inPam, PAM_STRUCT_SIZE(tuple_type));

    produceAvs(&inPam, stdout);

    pm_closer(inFileP);

    return 0;
}

