/*
 *  cistopbm: Convert images in the CompuServe RLE format to PBM
 *  Copyright (C) 2009  John Elliott
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pbm.h"

/* The maximum length of a run. Limit it to 0x5E bytes so that it is always
 * represented by a printable character 0x20-0x7E */
#define MAXRUNLENGTH 0x5E

static void syntax(const char *prog)
{
    pm_usage(" { options } { input } }\n\n"
         "Input file should be in PBM format.\n"
         "Output will be in CompuServe RLE format.\n"
         "Options:\n"
         "-i, --inverse: Reverse black and white.\n"
         "-w, --whitebg: White background.\n"
         "--:            End of options\n\n"
"pbmtocis v1.00, Copyright 2009 John Elliott <jce@seasip.demon.co.uk>\n"
"This program is redistributable under the terms of the GNU General Public\n"
"License, version 2 or later.\n"
         );
}

int main(int argc, const char **argv)
{
    FILE *ofP = stdout;
    FILE *ifP;
    int inoptions = 1;
    int n, x, y;
    int bg = PBM_BLACK; /* Default colouring is white on black */
    int inverse = 0;
    int cell, last, run;
    const char *inpname = NULL;

    int outh, outw;
    int height, width;
    bit **bits;

    pm_proginit(&argc, argv);

    for (n = 1; n < argc; n++)
    {
        if (!strcmp(argv[n], "--"))
        {
            inoptions = 0;
            continue;
        }
        if (inoptions)
        {
            if (pm_keymatch(argv[n], "-h", 2) ||
                pm_keymatch(argv[n], "-H", 2) ||
                pm_keymatch(argv[n], "--help", 6))
            {
                syntax(argv[0]);
                return EXIT_SUCCESS;
            }
            if (pm_keymatch(argv[n], "-i", 2) ||
                pm_keymatch(argv[n], "-I", 2) ||
                pm_keymatch(argv[n], "--inverse", 9))
            {
                inverse = 1;
                continue;
            }
            if (pm_keymatch(argv[n], "-w", 2) ||
                pm_keymatch(argv[n], "-W", 2) ||
                pm_keymatch(argv[n], "--whitebg", 9))
            {
                bg = PBM_WHITE; 
                continue;
            }
            if (argv[n][0] == '-' && argv[n][1] != 0)
            {
                pm_message("Unknown option: %s", argv[n]);
                syntax(argv[0]);
                return EXIT_FAILURE;
            }
        }

        if (inpname == NULL) inpname = argv[n];
        else { syntax(argv[0]); return EXIT_FAILURE; }
    }
    if (inpname == NULL) inpname = "-";
    ifP  = pm_openr(inpname);

    /* Load the PBM */
    bits = pbm_readpbm(ifP, &width, &height);

    if      (width <= 128 && height <= 96)  { outw = 128; outh = 96; }
    else if (width <= 256 && height <= 192) { outw = 256; outh = 192; }
    else
    {
        outw = 256;
        outh = 192;
        pm_message("Warning: Input file is larger than 256x192. "
                "It will be cropped.");
    }
    /* Write the magic number */
    fputc(0x1B, ofP);
    fputc(0x47, ofP);
    fputc((outw == 128) ? 0x4D : 0x48, ofP);

    /* And now start encoding */
    y = x = 0;
    last = PBM_BLACK;
    run = 0;
    while (y < outh)
    {
        if (x < width && y < height)    
        {
            cell = bits[y][x];
            if (inverse) cell ^= (PBM_BLACK ^ PBM_WHITE);
        }
        else cell = bg;

        if (cell == last)   /* Cell is part of current run */
        {
            ++run;
            if (run > MAXRUNLENGTH)
            {       
                fputc(0x20 + MAXRUNLENGTH, ofP);
                fputc(0x20, ofP);
                run -= MAXRUNLENGTH;
            }   
        }
        else    /* change */
        {
            fputc(run + 0x20, ofP);
            last = last ^ (PBM_BLACK ^ PBM_WHITE);
            run = 1;
        }
        ++x;
        if (x >= outw) { x = 0; ++y; }
    }
    if (last == bg) /* Last cell written was background. Write foreground */
    {
        fputc(run + 0x20, ofP);
    }
    else if (run)   /* Write background and foreground */
    {
        fputc(run + 0x20, ofP);
        fputc(0x20, ofP);
    }
    /* Write the end-graphics signature */
    fputc(0x1B, ofP);
    fputc(0x47, ofP);
    fputc(0x4E, ofP);
    pm_close(ifP);
    return 0;   
}
