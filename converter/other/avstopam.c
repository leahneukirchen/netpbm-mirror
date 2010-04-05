/* ----------------------------------------------------------------------
 *
 * Convert an AVS X image to a PAM image
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



static void
producePam(FILE *       const avsFileP,
           struct pam * const pamP) {

    tuple *      tuplerow;
    unsigned int row;

    tuplerow = pnm_allocpamrow(pamP);
    for (row = 0; row < pamP->height; ++row) {
        unsigned int col;
        for (col = 0; col < pamP->width; ++col) {
            tuple const thisTuple = tuplerow[col];
            unsigned char c;
            pm_readcharu(avsFileP, &c); thisTuple[3] = c;
            pm_readcharu(avsFileP, &c); thisTuple[0] = c;
            pm_readcharu(avsFileP, &c); thisTuple[1] = c;
            pm_readcharu(avsFileP, &c); thisTuple[2] = c;
        }
        pnm_writepamrow(pamP, tuplerow);
    }
    pnm_freepamrow(tuplerow);
}



int
main(int argc, const char *argv[]) {

    const char * comment = "Produced by avstopam";  /* constant */

    struct pam   outPam;
    const char * inputFilename;
    FILE       * inFileP;
    long         width;
    long         height;

    pm_proginit(&argc, argv);

    inputFilename = (argc > 1) ? argv[1] : "-";

    inFileP = pm_openr(inputFilename);

    pm_readbiglong(inFileP, &width);
    pm_readbiglong(inFileP, &height);

    outPam.size             = sizeof(struct pam);
    outPam.len              = PAM_STRUCT_SIZE(comment_p);
    outPam.file             = stdout;
    outPam.format           = PAM_FORMAT;
    outPam.plainformat      = 0;
    outPam.width            = width;
    outPam.height           = height;
    outPam.depth            = 4;
    outPam.maxval           = 255;
    outPam.bytes_per_sample = 1;
    sprintf(outPam.tuple_type, "RGB_ALPHA");
    outPam.allocation_depth = 4;
    outPam.comment_p        = &comment;

    /* Produce a PAM output header.  Note that AVS files *always*
       contain four channels with one byte per channel.
    */
    pnm_writepaminit(&outPam);

    producePam(inFileP, &outPam);

    pm_closer(inFileP);

    return 0;
}
