/* sgitopnm.c - read an SGI image and and produce a portable anymap
**
** Copyright (C) 1994 by Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
**
** Based on the SGI image description v0.9 by Paul Haeberli (paul@sgi.comp)
** Available via ftp from sgi.com:graphics/SGIIMAGESPEC
**
** The definitive document describing the SGI image file format,
** SGI Image File Format Version 1.00 is available from
** ftp://ftp.sgi.com/graphics/grafica/sgiimage.html
**
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
*/


#include <unistd.h>
#include <limits.h>
#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pnm.h"
#include "sgi.h"

#define MAX_ZSIZE 256


struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* '-' if stdin */
    unsigned int verbose;
    unsigned int channelSpec;
    unsigned int channel;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "channel",      OPT_UINT,
            &cmdlineP->channel,
            &cmdlineP->channelSpec,            0);
    OPTENT3(0, "verbose",             OPT_FLAG,      NULL,
            &cmdlineP->verbose,       0);
    OPTENT3(0, "noverbose",           OPT_FLAG,      NULL,
            NULL,       0);  /* backward compatibility */

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    free(option_def);

    if (!cmdlineP->channelSpec)
        cmdlineP->channel = MAX_ZSIZE + 1;
            /* Invalid value; to suppress Valgrind error */

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileName = argv[1];
    else
        pm_error("Program takes at most one argument:  input file name");
}



/* basic I/O functions, taken from ilbmtoppm.c */

static void
readerr(FILE * const f) {

    if (ferror(f))
        pm_error("read error");
    else
        pm_error("premature EOF");
}



static short
getBigShort(FILE * const ifP) {

    short s;

    if (pm_readbigshort(ifP, &s) == -1)
        readerr(ifP);

    return s;
}



static long
getBigLong(FILE * const ifP) {

    long l;

    if (pm_readbiglong(ifP, &l) == -1)
        readerr(ifP);

    return l;
}



static unsigned char
getByte(FILE * const ifP) {

    int i;

    i = getc(ifP);
    if (i == EOF)
        readerr(ifP);

    return (unsigned char) i;
}



static void
readBytes(FILE * const ifP,
          int    const n,
          char * const buf) {

    int r;

    r = fread((void *)buf, 1, n, ifP);

    if (r != n)
        readerr(ifP);
}



static short
getByteAsShort(FILE * const ifP) {

    return (short)getByte(ifP);
}



static const char *
compressionName(unsigned char const storageCode) {

    switch (storageCode) {
    case STORAGE_VERBATIM:
        return "none";
    case STORAGE_RLE:
        return "RLE";
    default:
        return "unknown";
    }
}



/* entry in RLE offset table */
typedef struct {
    long start;     /* offset in file */
    long length;    /* length of compressed scanline */
} TabEntry;

typedef short       ScanElem;
typedef ScanElem *  ScanLine;

#define WORSTCOMPR(x)   (2*(x) + 2)



static Header *
readHeader(FILE *       const ifP,
           bool         const outChannelSpec,
           bool         const verbose) {

    Header * headP;

    MALLOCVAR_NOFAIL(headP);

    headP->magic     = getBigShort(ifP);
    headP->storage   = getByte(ifP);
    headP->bpc       = getByte(ifP);
    headP->dimension = getBigShort(ifP);
    headP->xsize     = getBigShort(ifP);
    headP->ysize     = getBigShort(ifP);
    headP->zsize     = getBigShort(ifP);
    if (headP->zsize > MAX_ZSIZE)
        pm_error("Too many channels in input image: %u",
                 (unsigned int) headP->zsize );
    headP->pixmin    = getBigLong(ifP);
    headP->pixmax    = getBigLong(ifP);
    if (headP->pixmin >= headP->pixmax)
        pm_error("Invalid sgi image header: pixmin larger than pixmax");
    readBytes(ifP, 4, headP->dummy1);
    readBytes(ifP, 80, headP->name);
    headP->colormap  = getBigLong(ifP);
    readBytes(ifP, 404, headP->dummy2);

    if (headP->magic != SGI_MAGIC)
        pm_error("bad magic number - not an SGI image");
    if (headP->storage != STORAGE_VERBATIM && headP->storage != STORAGE_RLE)
        pm_error("unknown compression type");
    if (headP->bpc < 1 || headP->bpc > 2)
        pm_error("illegal precision value %d (only 1-2 allowed)", headP->bpc);
    if (headP->colormap != CMAP_NORMAL)
        pm_error("non-normal pixel data of a form we don't recognize");

    /* adjust ysize/zsize to dimension, just to be sure */
    switch (headP->dimension) {
    case 1:
        headP->ysize = 1;
        break;
    case 2:
        headP->zsize = 1;
        break;
    case 3:
        switch (headP->zsize) {
        case 1:
            headP->dimension = 2;
            break;
        case 2:
            if (!outChannelSpec)
                pm_message("2-channel image, using only first channel.  "
                           "Extract alpha channel with -channel=1");
            break;
        case 3:
            break;
        default:
            if (!outChannelSpec)
                pm_message("%u-channel image, using only first 3 channels  "
                           "Extract %s with -channel=%c",
                            headP->zsize,
                            headP->zsize==4 ?
                                "alpha channel" : "additional channels",
                            headP->zsize==4 ? '3' : 'N');
            break;
        }
        break;
    default:
        pm_error("illegal dimension value %u (only 1-3 allowed)",
                 headP->dimension);
    }

    if (verbose) {
        pm_message("raster size %ux%u, %u channels",
                   headP->xsize, headP->ysize, headP->zsize);
        pm_message("compression: 0x%02x = %s",
                   headP->storage, compressionName(headP->storage));
        headP->name[79] = '\0';  /* just to be safe */
        pm_message("Image name: '%s'", headP->name);
    }

    return headP;
}



static TabEntry *
readTable(FILE * const ifP,
          int    const tablen) {

    TabEntry * table;
    unsigned int i;

    MALLOCARRAY_NOFAIL(table, tablen);

    for (i = 0; i < tablen; ++i)
        table[i].start = getBigLong(ifP);
    for (i = 0; i < tablen; ++i)
        table[i].length = getBigLong(ifP);

    return table;
}



static void
rleDecompress(ScanElem * const srcStart,
              int        const srcleftStart,
              ScanElem * const destStart,
              int        const destleftStart) {

    ScanElem * src;
    int srcleft;
    ScanElem * dest;
    int destleft;

    for (src = srcStart,
             srcleft = srcleftStart,
             dest = destStart,
             destleft = destleftStart; srcleft; ) {

        unsigned char const el = (unsigned char)(*src++ & 0xff);
        unsigned int const count = (unsigned int)(el & 0x7f);

        --srcleft;

        if (count == 0)
            return;
        if (destleft < count)
            pm_error("RLE error: too much input data "
                     "(space left %d, need %d)", destleft, count);
        destleft -= count;
        if (el & 0x80) {
            unsigned int i;
            if (srcleft < count)
                pm_error("RLE error: not enough data for literal run "
                         "(data left %d, need %d)", srcleft, count);
            srcleft -= count;
            for (i = 0; i < count; ++i)
                *dest++ = *src++;
        } else {
            unsigned int i;
            if (srcleft == 0)
                pm_error("RLE error: not enough data for replicate run");
            for (i = 0; i < count; ++i)
                *dest++ = *src;
            ++src;
            --srcleft;
        }
    }
    pm_error("RLE error: no terminating 0-byte");
}



static ScanLine *
readChannels(FILE *       const ifP,
             Header *     const head,
             TabEntry *   const table,
             bool         const outChannelSpec,
             unsigned int const outChannel) {

    ScanLine * image;
    ScanElem * temp;
    unsigned int channel;
    unsigned int maxchannel;

    if (outChannelSpec) {
        maxchannel = outChannel + 1;
        MALLOCARRAY_NOFAIL(image, head->ysize);
    } else if (head->zsize <= 2) {
        maxchannel = 1;
        MALLOCARRAY_NOFAIL(image, head->ysize);
    } else {
        maxchannel = 3;
        MALLOCARRAY_NOFAIL(image, head->ysize * maxchannel);
    }
    if (table)
        MALLOCARRAY_NOFAIL(temp, WORSTCOMPR(head->xsize));

    for (channel = 0; channel < maxchannel; ++channel) {
        unsigned int row;
        for (row = 0; row < head->ysize; ++row) {
            int const sgiIndex = channel * head->ysize + row;

            unsigned long int iindex;

            iindex = outChannelSpec ? row : sgiIndex;
            if (!outChannelSpec || outChannel == channel)
                MALLOCARRAY_NOFAIL(image[iindex], head->xsize);

            if (table) {
                if (!outChannelSpec || channel >= outChannel) {
                    pm_filepos const offset = (pm_filepos)
                        table[sgiIndex].start;
                    long const length = head->bpc == 2 ?
                        table[sgiIndex].length / 2 :
                        table[sgiIndex].length;

                    unsigned int i;

                    /* Note: (offset < currentPosition) can happen */

                    pm_seek2(ifP, &offset, sizeof(offset));

                    for (i = 0; i < length; ++i)
                        if (head->bpc == 1)
                            temp[i] = getByteAsShort(ifP);
                        else
                            temp[i] = getBigShort(ifP);
                    rleDecompress(temp, length, image[iindex], head->xsize);
                }
            } else {
                unsigned int i;
                for (i = 0; i < head->xsize; ++i) {
                    ScanElem p;
                    if (head->bpc == 1)
                        p = getByteAsShort(ifP);
                    else
                        p = getBigShort(ifP);

                    if (!outChannelSpec || outChannel == channel)
                        image[iindex][i] = p;
                }
            }
        }
    }
    if (table)
        free(temp);
    return image;
}



static void
imageToPnm(Header   *   const head,
           ScanLine *   const image,
           xelval       const maxval,
           bool         const outChannelSpec,
           unsigned int const outChannel) {

    int const sub = head->pixmin;
    xel * const pnmrow = pnm_allocrow(head->xsize);

    int row;
    int format;

    if (head->zsize <= 2 || outChannelSpec) {
        pm_message("writing PGM image");
        format = PGM_TYPE;
    } else {
        pm_message("writing PPM image");
        format = PPM_TYPE;
    }

    pnm_writepnminit(stdout, head->xsize, head->ysize, maxval, format, 0);
    for (row = head->ysize-1; row >= 0; --row) {
        unsigned int col;
        for (col = 0; col < head->xsize; ++col) {
            if (format == PGM_TYPE)
                PNM_ASSIGN1(pnmrow[col], image[row][col] - sub);
            else {
                pixval r, g, b;
                r = image[row][col] - sub;
                g = image[head->ysize + row][col] - sub;
                b = image[2* head->ysize + row][col] - sub;
                PPM_ASSIGN(pnmrow[col], r, g, b);
            }
        }
        pnm_writepnmrow(stdout, pnmrow, head->xsize, maxval, format, 0);
    }
    pnm_freerow(pnmrow);
}



int
main(int argc, const char * argv[]) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    TabEntry * table;
    ScanLine * image;
    Header * headP;
    xelval maxval;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr_seekable(cmdline.inputFileName);

    headP = readHeader(ifP, cmdline.channelSpec, cmdline.verbose);

    maxval = headP->pixmax - headP->pixmin;
    if (maxval > PNM_OVERALLMAXVAL)
        pm_error("Maximum sample value in input image (%d) is too large.  "
                 "This program's limit is %d.",
                 maxval, PNM_OVERALLMAXVAL);

    if (cmdline.channelSpec && cmdline.channel >= headP->zsize)
        pm_error("channel out of range - only %d channels in image",
                 headP->zsize);

    if (headP->storage != STORAGE_VERBATIM)
        table = readTable(ifP, headP->ysize * headP->zsize);
    else
        table = NULL;

    image = readChannels(ifP, headP, table,
                         cmdline.channelSpec, cmdline.channel);

    imageToPnm(headP, image, maxval, cmdline.channelSpec, cmdline.channel);

    pm_close(ifP);

    return 0;
}


