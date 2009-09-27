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


static void syntax(const char *prog)
{
        pm_usage(" { options } { input }\n\n"
                 "Input file should be in CompuServe RLE format.\n"
                 "Output file will be in PBM format.\n"
                 "Options:\n"
                 "-i, --inverse: Reverse black and white.\n"
                 "--:            End of options\n\n"
"cistopbm v1.01, Copyright 2009 John Elliott <jce@seasip.demon.co.uk>\n"
"This program is redistributable under the terms of the GNU General Public\n"
"License, version 2 or later.\n"
                 );
}

int main(int argc, const char **argv)
{
    FILE *ifP;
    int c[3];
    int inoptions = 1;
    int n, x, y;
    int bw = PBM_BLACK;     /* Default colouring is white on black */
    const char *inpname = NULL;
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
                bw ^= (PBM_WHITE ^ PBM_BLACK);
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

    /* There may be junk before the magic number. If so, skip it. */
    x = 0;
    c[0] = c[1] = c[2] = EOF;

    /* Read until the array c[] holds the magic number. */
    do
    {
        c[0] = c[1];
        c[1] = c[2];
        c[2] = fgetc(ifP);

        /* If character read was EOF, end of file was reached and magic number
         * not found.
         */
        if (c[2] == EOF)
        {
            pm_error("Input file is not in CompuServe RLE format");
        }
        ++x;
    } while (c[0] != 0x1B || c[1] != 0x47);

    /* x = number of bytes read. Should be 3 if signature is at the start */
    if (x > 3)
    {
        pm_message("Warning: %d bytes of junk skipped before image",
                   x - 3);
    }
    /* Parse the resolution */
    switch(c[2])
    {
    case 0x48:      height = 192; width = 256; break;
    case 0x4D:      height =  96; width = 128; break;
    default:        pm_error("Unknown resolution 0x%02x", c[2]);
        break;
    }
    /* Convert the data */
    bits = pbm_allocarray(width, height);
    x = y = 0;
    do
    {
        c[0] = fgetc(ifP);

        /* Stop if we hit EOF or Escape */
        if (c[0] == EOF)  break;        /* EOF */
        if (c[0] == 0x1B) break;        /* End of graphics */
        /* Other non-printing characters are ignored; some files contain a
         * BEL
         */
        if (c[0] < 0x20)  continue;

        /* Each character gives the number of pixels to draw in the appropriate
         * colour.
         */
        for (n = 0x20; n < c[0]; n++)
        {
            if (x < width && y < height) bits[y][x] = bw;
            x++;
            /* Wrap at end of line */
            if (x >= width)
            {
                x = 0;
                y++;
            }
        }
        /* And toggle colours */
        bw ^= (PBM_WHITE ^ PBM_BLACK);
    }
    while (1);

    /* See if the end-graphics signature (ESC G N) is present. */
    c[1] = EOF;
    if (c[0] == 0x1B)
    {
        c[1] = fgetc(ifP);
        c[2] = fgetc(ifP);
    }
    if (c[0] != 0x1B || c[1] != 0x47 || c[2] != 0x4E)
    {
        pm_message("Warning: End-graphics signature not found");
    }
    /* See if we decoded the right number of pixels */
    if (x != 0 || y != height)
    {
        pm_message("Warning: %d pixels found, should be %d",
                   y * width + x, width * height);
    }
    pbm_writepbm(stdout, bits, width, height, 0);
    pm_close(ifP);
    return 0;       
}
