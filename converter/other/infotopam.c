/* infotopam:  A program to convert Amiga Info icon files to PAM files
 * Copyright (C) 2004  Richard Griswold - griswold@acm.org
 *
 * Thanks to the following people on comp.sys.amiga.programmer for tips
 * and pointers on decoding the info file format:
 *
 *   Ben Hutchings
 *   Thomas Richter
 *   Kjetil Svalastog Matheussen
 *   Anders Melchiorsen
 *   Dirk Stoecker
 *   Ronald V.D.
 *
 * The format of the Amiga info file is as follows:
 *
 *   DiskObject header            (78 bytes)
 *   Optional DrawerData header   (56 bytes)
 *   First icon header            (20 bytes)
 *   First icon data
 *   Second icon header           (20 bytes)
 *   Second icon data
 *
 * The DiskObject header contains, among other things, the magic number
 * (0xE310), the object width and height (inside the embedded Gadget header),
 * and the version.
 *
 * Each icon header contains the icon width and height, which can be smaller
 * than the object width and height, and the number of bit-planes.
 *
 * The icon data has the following format:
 *
 *   BIT-PLANE planes, each with HEIGHT rows WIDTH bits long, rounded up to
 *   a multiple of 2 bytes.
 *
 * So if you have a 9x3x2 icon, the icon data looks like this:
 *
 *   aaaa aaaa a000 0000
 *   aaaa aaaa a000 0000
 *   aaaa aaaa a000 0000
 *   bbbb bbbb b000 0000
 *   bbbb bbbb b000 0000
 *   bbbb bbbb b000 0000
 *
 * Where 'a' is a bit for the first bit-plane, 'b' is a bit for the second
 * bit-plane, and '0' is padding.  Thanks again to Ben Hutchings for his
 * very helpful post!
 *
 * This program uses code from "sidplay" and an older "infotoxpm" program I
 * wrote, both of which are released under GPL.
 *
 *-------------------------------------------------------------------------
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "pm_c_util.h"
#include "pam.h"
#include "shhopt.h"
#include "mallocvar.h"


typedef struct CmdlineInfo_ {
    const char  * inputFileNm;
    unsigned int  forcecolor;
    pixel         colors[4];   /* Colors to use for converted icons */
    unsigned int  selected;
    unsigned int  verbose;
} CmdlineInfo;

typedef struct IconInfo_ {
    /* Miscellaneous icon information */
    FILE *          ifP;            /* Input file */
    bool            hasDrawerData;  /* Icon has drawer data */
    unsigned int    version;        /* Icon version */
    unsigned int    width;          /* Width in pixels */
    unsigned int    height;         /* Height in pixels */
    unsigned int    depth;          /* Bits of color per pixel */
    unsigned int    bpwidth;
        /* Bitplane width; Width of each row in icon, including padding */
    unsigned char * icon;           /* Completed icon */

} IconInfo;

typedef struct IconHeader_ { /* 20 bytes */
    /* Text of header for one icon image */
    unsigned char pad0[4];        /* Padding (always seems to be zero) */
    unsigned char iconWidth[2];   /* Width (usually equal to Gadget width) */
    unsigned char iconHeight[2];
        /* Height (usually equal to Gadget height -1) */
    unsigned char bpp[2];         /* Bits per pixel */
    unsigned char pad1[10];       /* ??? */
} IconHeader;

/*
 * Gadget and DiskObject structs come from the libsidplay 1.36.57 info_.h file
 * http://www.geocities.com/SiliconValley/Lakes/5147/sidplay/linux.html
 */
typedef struct DiskObject_ { /* 78 bytes (including Gadget struct) */
    /* Text of Info Disk Object header */
    unsigned char magic[2];         /* Magic number at the start of the file */
    unsigned char version[2];       /* Object version number */
    unsigned char gadget[44];       /* Copy of in memory gadget (44 by */
    unsigned char type;             /* ??? */
    unsigned char pad;              /* Pad it out to the next word boundary */
    unsigned char pDefaultTool[4];  /* Pointer  to default tool */
    unsigned char ppToolTypes[4];   /* Pointer pointer to tool types */
    unsigned char currentX[4];      /* Current X position (?) */
    unsigned char currentY[4];      /* Current Y position (?) */
    unsigned char pDrawerData[4];   /* Pointer to drawer data */
    unsigned char pToolWindow[4];   /* Ptr to tool window - only for tools */
    unsigned char stackSize[4];     /* Stack size - only for tools */
} DiskObject;



static void
parseCommandLine(int                 argc,
                 const char **       argv,
                 CmdlineInfo * const cmdlineP) {

    unsigned int   argIdx;
    optEntry     * option_def;
    optStruct3     opt;
    unsigned int   option_def_index;
    unsigned int   numcolorsSpec;
    unsigned int   numcolors;

    MALLOCARRAY_NOFAIL(option_def, 100);

    /* Set command line options */
    option_def_index = 0;   /* Incremented by OPTENT3 */
    OPTENT3(0, "forcecolor", OPT_FLAG, NULL,       &cmdlineP->forcecolor,
            0);
    OPTENT3(0, "numcolors",  OPT_UINT, &numcolors, &numcolorsSpec,
            0);
    OPTENT3(0, "selected",   OPT_FLAG, NULL,       &cmdlineP->selected,
            0);
    OPTENT3(0, "verbose",    OPT_FLAG, NULL,       &cmdlineP->verbose,
            0);

    opt.opt_table     = option_def;
    opt.short_allowed = false;  /* No short (old-fashioned) options */
    opt.allowNegNum   = false;  /* No negative number parameters */

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);

    {
        const char * const colors[4] = {
            /* Pixel colors based on original Amiga colors */
            "#0055AA",    /*   Blue      0,  85, 170 */
            "#FFFFFF",    /*   White   255, 255, 255 */
            "#000020",    /*   Black     0,   0,  32 */
            "#FF8A00"     /*   Orange  255, 138,   0 */
        };

        unsigned int colorArgCt;
            /* Number of arguments for overriding colors */

        unsigned int colorIdx;

        if (numcolorsSpec) {
            colorArgCt = numcolors * 2;
            if (argc-1 < colorArgCt) {
                pm_error("Insufficient arguments for %u color "
                         "specifications.  Need at least %u arguments",
                         numcolors, colorArgCt);
            }
        } else
            colorArgCt = 0;

        /* Initialize palette to defaults */
        for (colorIdx = 0; colorIdx < 4; ++colorIdx)
            cmdlineP->colors[colorIdx] =
                ppm_parsecolor(colors[colorIdx], 0xFF);

        /* Convert color arguments */
        for (argIdx = 1; argIdx < colorArgCt; argIdx += 2) {
            char *       endptr;        /* End pointer for strtol() */
            unsigned int colorIdx;

            /* Get color index from argument */
            colorIdx = strtoul(argv[argIdx], &endptr, 0);

            if (*endptr != '\0') {
                pm_error("'%s' is not a valid color index", argv[argIdx]);
            }

            if ((colorIdx < 0) || (colorIdx > 3)) {
                pm_error(
                    "%u is not a valid color index (minimum 0, maximum 3)",
                    colorIdx);
            }

            cmdlineP->colors[colorIdx] = ppm_parsecolor(argv[argIdx+1], 0xFF);
        }
    }

    if (argIdx > argc-1)
        cmdlineP->inputFileNm = "-";  /* Read from standard input */
    else
        cmdlineP->inputFileNm = argv[argIdx];
}



static void
readDiskObjectHeader(FILE *         const ifP,
                     unsigned int * const versionP,
                     bool *         const hasDrawerDataP) {
/*---------------------------------------------------------------------------
  Read disk object header from file *ifP; validate it and return its contents.
----------------------------------------------------------------------------*/
    DiskObject  dobj;      /* Disk object structure */
    size_t      bytesReadCt;

    /* Read the disk object header */
    bytesReadCt = fread(&dobj, 1, sizeof(dobj), ifP);
    if (ferror(ifP))
        pm_error("Cannot read disk object header.  "
                 "fread() errno = %d (%s)",
                 errno, strerror(errno));
    else if (bytesReadCt != sizeof(dobj))
        pm_error("Cannot read entire disk object header.  "
                 "Only read 0x%X of 0x%X bytes",
                 (unsigned)bytesReadCt, (unsigned)sizeof(dobj));

    /* Validate magic number */
    if ((dobj.magic[0] != 0xE3) && (dobj.magic[1] != 0x10))
        pm_error("Wrong magic number in icon file.  "
                 "Expected 0xE310, but got 0x%X%X",
                 dobj.magic[0], dobj.magic[1]);

    *versionP = (dobj.version[0] <<  8) + (dobj.version[1]);

    *hasDrawerDataP =
        (dobj.pDrawerData[0] << 24) +
        (dobj.pDrawerData[1] << 16) +
        (dobj.pDrawerData[2] <<  8) +
        (dobj.pDrawerData[3]      )
        > 0;
}



static void
readIconHeader(FILE *         const ifP,
               unsigned int * const widthP,
               unsigned int * const heightP,
               unsigned int * const depthP,
               unsigned int * const bpwidthP) {
/*-------------------------------------------------------------------------
 * Get fields from icon header portion of info file
 *-------------------------------------------------------------------------*/
    IconHeader  ihead;      /* Icon header structure */
    size_t      bytesRead;

    /* Read icon header */
    bytesRead = fread(&ihead, 1, sizeof(ihead), ifP);
    if (ferror(ifP))
        pm_error("Failed to read icon header.  fread() errno = %d (%s)",
                 errno, strerror(errno));
    else if (bytesRead != sizeof(ihead))
        pm_error("Failed to read the entire icon header.  "
                 "Read only %u of %u bytes",
                 (unsigned)bytesRead, (unsigned)sizeof(ihead));

    *widthP  = (ihead.iconWidth[0]  << 8) + ihead.iconWidth[1];
    *heightP = (ihead.iconHeight[0] << 8) + ihead.iconHeight[1];
    *depthP  = (ihead.bpp[0]        << 8) + ihead.bpp[1];

    *bpwidthP = ROUNDUP(*widthP, 16);

    /* Validate number of bit planes */
    if (*depthP > 2 || *depthP < 1)
        pm_error("We don't know how to interpret file with %u bitplanes.  ",
                 *depthP);
}



static void
addBitplane(unsigned char * const icon,
            unsigned int    const bpsize,
            unsigned char * const buff) {
/*----------------------------------------------------------------------------
   Add bitplane to existing icon image
-----------------------------------------------------------------------------*/
    unsigned int i;

    for (i = 0; i < bpsize; ++i) {
        icon[(i*8)+0] = (icon[(i*8)+0] << 1) | ((buff[i] >> 7) & 0x01);
        icon[(i*8)+1] = (icon[(i*8)+1] << 1) | ((buff[i] >> 6) & 0x01);
        icon[(i*8)+2] = (icon[(i*8)+2] << 1) | ((buff[i] >> 5) & 0x01);
        icon[(i*8)+3] = (icon[(i*8)+3] << 1) | ((buff[i] >> 4) & 0x01);
        icon[(i*8)+4] = (icon[(i*8)+4] << 1) | ((buff[i] >> 3) & 0x01);
        icon[(i*8)+5] = (icon[(i*8)+5] << 1) | ((buff[i] >> 2) & 0x01);
        icon[(i*8)+6] = (icon[(i*8)+6] << 1) | ((buff[i] >> 1) & 0x01);
        icon[(i*8)+7] = (icon[(i*8)+7] << 1) | ((buff[i] >> 0) & 0x01);
    }
}



static void
readIconData(FILE *           const fileP,
             unsigned int     const width,
             unsigned int     const height,
             unsigned int     const depth,
             unsigned char ** const iconP) {
/*-------------------------------------------------------------------------
 * Read icon data from file
 *-------------------------------------------------------------------------*/
    int             bitplane; /* Bitplane index */
    unsigned char * buff;     /* Buffer to hold bits for 1 bitplane */
    unsigned char * icon;

    unsigned int const bpsize = height * (((width + 15) / 16) * 2);
        /* Bitplane size in bytes, with padding */


    MALLOCARRAY(buff, bpsize);
    if ( buff == NULL )
        pm_error( "Cannot allocate memory to hold icon pixels" );

    MALLOCARRAY(icon, bpsize * 8);
    if (icon == NULL)
        pm_error( "Cannot allocate memory to hold icon" );

    /* Initialize to zero */
    memset(buff, 0, bpsize);
    memset(icon, 0, bpsize * 8);

    /* Each bitplane is stored independently in the icon file.  This
     * loop reads one bitplane at a time into buff.  Since fread() may
     * not read all of the bitplane on the first call, the inner loop
     * continues until all bytes are read.  The buffer pointer, bp,
     * points to the next byte in buff to fill in.  When the inner
     * loop is done, bp points to the end of buff.
     *
     * After reading in the entire bitplane, the second inner loop splits the
     * eight pixels in each byte of the bitplane into eight separate bytes in
     * the icon buffer.  The existing contents of each byte in icon are left
     * shifted by one to make room for the next bit.
     *
     * Each byte in the completed icon contains a value from 0 to
     * 2^depth (0 to 1 for depth of 1 and 0 to 3 for a depth of 3).
     * This is an index into the colors array in the info struct.  */

    for (bitplane = 0; bitplane < depth; ++bitplane) {
        /* Read bitplane into buffer */
        int toread;   /* Number of bytes left to read */
        unsigned char * buffp;    /* Buffer point for reading data */

        toread = bpsize; buffp = &buff[0];

        while (toread > 0) {
            size_t bytesRead;

            bytesRead = fread(buffp, 1, toread, fileP);
            if (ferror(fileP))
                pm_error("Cannot read from file info file.  "
                         "fread() errno = %d (%s)",
                         errno, strerror(errno));
            else if (bytesRead == 0)
                pm_error("Premature end-of-file.  "
                         "Still have 0x%X bytes to read",
                         toread );

            toread -= bytesRead;
            buffp  += bytesRead;
        }
        addBitplane(icon, bpsize, buff);
    }
    *iconP = icon;

    free(buff);
}



static void
writeRaster(IconInfo *    const infoP,
            struct pam *  const pamP,
            bool          const wantColor,
            const pixel * const colors) {
/*-------------------------------------------------------------------------
  Write out raster of PAM image described by *pamP.

  'wantColor' means the user wants the PAM to be tuple type RGB, regardless
  of the input image type.

  'colors' is the palette.  It has 4 entries, one for each of the possible
  color indices in the input icon raster.
--------------------------------------------------------------------------*/
    unsigned int row;
    tuple * tuplerow;      /* Output row */

    tuplerow = pnm_allocpamrow(pamP);

    for (row = 0; row < infoP->height; ++row) {
        unsigned int col;

        for (col = 0; col < infoP->width; ++col) {
            if (infoP->depth == 1) {
                if (wantColor) {
                    /* 1 is black and 0 is white */
                    unsigned int colorIdx =
                        infoP->icon[row * infoP->bpwidth + col] ? 2 : 1;

                    tuplerow[col][PAM_RED_PLANE] = PPM_GETR(colors[colorIdx]);
                    tuplerow[col][PAM_GRN_PLANE] = PPM_GETG(colors[colorIdx]);
                    tuplerow[col][PAM_BLU_PLANE] = PPM_GETB(colors[colorIdx]);
                } else {
                    /* 1 is black and 0 is white */
                    tuplerow[col][0] =
                        infoP->icon[row * infoP->bpwidth + col] ? 0 : 1;
                }
            } else {
                unsigned int const colorIdx =
                    infoP->icon[row * infoP->bpwidth + col];
                tuplerow[col][PAM_RED_PLANE] = PPM_GETR(colors[colorIdx]);
                tuplerow[col][PAM_GRN_PLANE] = PPM_GETG(colors[colorIdx]);
                tuplerow[col][PAM_BLU_PLANE] = PPM_GETB(colors[colorIdx]);
            }
        }
        pnm_writepamrow(pamP, tuplerow);
    }

    pnm_freepamrow(tuplerow);
}



int
main(int argc, const char **argv) {

    CmdlineInfo  cmdline;
    IconInfo     info;      /* Miscellaneous icon information */
    struct pam   pam;       /* PAM header */

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    info.ifP = pm_openr(cmdline.inputFileNm);

    readDiskObjectHeader(info.ifP, &info.version, &info.hasDrawerData);

    /* Skip drawer data, if any */
    if (info.hasDrawerData) {
        unsigned int const skipCt = 56;   /* Draw data size */

        int rc;

        rc = fseek(info.ifP, skipCt, SEEK_CUR);
        if (rc < 0) {
            pm_error("Failed to skip header information in input file.  "
                     "fseek() errno = %d (%s)",
                     errno, strerror(errno));
        }
    }

    /* Read header of first icon */
    readIconHeader(info.ifP, &info.width, &info.height, &info.depth,
                   &info.bpwidth);

    /* Skip ahead to next header if converting second icon */
    if (cmdline.selected) {
        unsigned int const skipCt =
            info.height * (((info.width + 15) / 16) * 2) * info.depth;

        int rc;

        rc = fseek(info.ifP, skipCt, SEEK_CUR);
        if (rc < 0) {
            pm_error("Failed to skip to next icon input file.  "
                     "fseek() errno = %d (%s)",
                     errno, strerror(errno));
        }
        /* Read header of second icon */
        readIconHeader(info.ifP, &info.width, &info.height, &info.depth,
                       &info.bpwidth);
    }

    readIconData(info.ifP, info.width, info.height, info.depth, &info.icon);

    if (cmdline.verbose) {
        pm_message("Version %u .info file, %s icon: %uW x %uH x %u deep",
                   info.version, cmdline.selected ? "second" : "first",
                   info.width, info.height, info.depth);
    }
    pam.size   = sizeof(pam);
    pam.len    = PAM_STRUCT_SIZE(tuple_type);
    pam.file   = stdout;
    pam.height = info.height;
    pam.width  = info.width;
    pam.format = PAM_FORMAT;

    if ((info.depth == 1) && !cmdline.forcecolor) {
        pam.depth  = 1;
        pam.maxval = 1;
        strcpy(pam.tuple_type, "BLACKANDWHITE");
    } else {
        pam.depth  = 3;
        pam.maxval = 0xFF;
        strcpy(pam.tuple_type, "RGB");
    }
    pnm_writepaminit(&pam);

    writeRaster(&info, &pam, cmdline.forcecolor, cmdline.colors);

    free(info.icon);
    pm_close(pam.file);
    pm_close(info.ifP);

    return 0;
}


