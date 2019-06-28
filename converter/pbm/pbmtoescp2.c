 /* pbmtoescp2.c - read a portable bitmap and produce Epson ESC/P2 raster
**                 graphics output data for Epson Stylus printers
**
** Copyright (C) 2003 by Ulrich Walcher (u.walcher@gmx.de)
**                       and Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
** Major changes were made in July 2015 by Akira Urushibata.
** Added 720 DPI capability.
** Added -formfeed, -raw and -stripeheight.
** Replaced Packbits run length encoding function.  (Use library function.)
*
*  ESC/P Reference Manual (1997)
*  ftp://download.epson-europe.com/pub/download/182/epson18162eu.zip
*/

#include <string.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "runlength.h"
#include "pbm.h"



static char const esc = 033;

struct CmdlineInfo {
    const char * inputFileName;
    unsigned int resolution;
    unsigned int compress;
    unsigned int stripeHeight;
    bool raw;
    bool formfeed;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo *cmdlineP) {

    optStruct3 opt;
    unsigned int option_def_index = 0;
    optEntry * option_def = malloc(100*sizeof(optEntry));

    unsigned int compressSpec, resolutionSpec, stripeHeightSpec,
                 rawSpec, formfeedSpec;

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;
    opt.allowNegNum = FALSE;
    OPTENT3(0, "compress",     OPT_UINT,    &cmdlineP->compress,    
            &compressSpec,    0);
    OPTENT3(0, "resolution",   OPT_UINT,    &cmdlineP->resolution,  
            &resolutionSpec,  0);
    OPTENT3(0, "stripeheight", OPT_UINT,    &cmdlineP->stripeHeight,  
            &stripeHeightSpec, 0);
    OPTENT3(0, "raw",          OPT_FLAG,    NULL,  
            &rawSpec,    0);
    OPTENT3(0, "formfeed",     OPT_FLAG,    NULL,  
            &formfeedSpec,    0);
    
    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
    
    if (argc-1 > 1)
        pm_error("Too many arguments: %d.  "
                 "Only argument is the filename", argc-1);

    if (compressSpec) {
        if (cmdlineP->compress != 0 && cmdlineP->compress != 1)
            pm_error("Invalid -compress value: %u.  Only 0 and 1 are valid.",
                     cmdlineP->compress);
    } else
        cmdlineP->compress = 1;

    if (resolutionSpec) {
        if (cmdlineP->resolution != 720 && cmdlineP->resolution != 360 &&
            cmdlineP->resolution != 180)
            pm_error("Invalid -resolution value: %u.  "
                     "Only 180, 360 and 720 are valid.", cmdlineP->resolution);
    } else
        cmdlineP->resolution = 360;

    if (stripeHeightSpec) {
        if (cmdlineP->stripeHeight == 0 ||
            cmdlineP->stripeHeight > 255)
            pm_error("Invalid -stripeheight value: %u. "
                     "Should be 24, 8, or 1, and must be in the range 1-255",
                     cmdlineP->stripeHeight);
        else if (cmdlineP->stripeHeight != 24 &&
                 cmdlineP->stripeHeight != 8  &&
                 cmdlineP->stripeHeight != 1)
            pm_message("Proceeding with irregular -stripeheight value: %u. "
                       "Should be 24, 8, or 1.", cmdlineP->stripeHeight);
        else if (cmdlineP->resolution == 720 &&
                 cmdlineP->stripeHeight != 1)
            /* The official Epson manual mandates single-row stripes for
               720 dpi high-resolution images.
            */
            pm_message("Proceeding with irregular -stripeheight value: %u. "
                       "Because resolution i 720dpi, should be 1.",
                        cmdlineP->stripeHeight);
    } else
        cmdlineP->stripeHeight = cmdlineP->resolution == 720 ? 1 : 24;

    if (rawSpec && formfeedSpec)
        pm_error("You cannot specify both -raw and -formfeed");
    else {
        cmdlineP->raw = rawSpec ? true : false ;
        cmdlineP->formfeed = formfeedSpec ? true : false ;
    }

    if (argc-1 == 1)
        cmdlineP->inputFileName = argv[1];
    else
        cmdlineP->inputFileName = "-";

    free(option_def);
}



static void
writeSetup(unsigned int const hres) {

    /* Set raster graphic mode. */
    printf("%c%c%c%c%c%c", esc, '(', 'G', 1, 0, 1);

    /* Set line spacing in units of 1/360 inches. */
    printf("%c%c%c", esc, '+', 24 * hres / 10);
}



int
main(int argc, const char * argv[]) {

    FILE * ifP;
    int rows, cols;
    int format;
    unsigned int row;
    unsigned int idx;
    unsigned int outColByteCt;
    unsigned int stripeByteCt;
    unsigned int hres, vres;
    unsigned char * inBuff;
    unsigned char * bitrow[256];
    unsigned char * compressedData;
    struct CmdlineInfo cmdline;
    
    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    pbm_readpbminit(ifP, &cols, &rows, &format);

    if (cols / 256 > 127)  /* Limit in official Epson manual */
        pm_error("Image width is too large");

    outColByteCt = pbm_packed_bytes(cols);
    stripeByteCt = cmdline.stripeHeight * outColByteCt;

    MALLOCARRAY(inBuff, stripeByteCt);
    if (inBuff == NULL)
      pm_error("Out of memory trying to create input buffer of %u bytes",
               stripeByteCt);

    if (cmdline.compress != 0)
        pm_rlenc_allocoutbuf(&compressedData, stripeByteCt, PM_RLE_PACKBITS);
    else
        compressedData = NULL;

    for (idx = 0; idx <= cmdline.stripeHeight; ++idx)
        bitrow[idx]= &inBuff[idx * outColByteCt];

    hres = vres = 3600 / cmdline.resolution;
        /* Possible values for hres, vres: 20, 10, 5 */

    if (!cmdline.raw)
        writeSetup(hres);

    /* Write out raster stripes */

    for (row = 0; row < rows; row += cmdline.stripeHeight ) {
        unsigned int const rowsThisStripe =
            MIN(rows - row, cmdline.stripeHeight);
        unsigned int const outCols = outColByteCt * 8;

        if (rowsThisStripe > 0) {
            unsigned int idx;

            printf("%c%c%c%c%c%c%c%c", esc, '.', cmdline.compress, vres, hres,
                   cmdline.stripeHeight, outCols % 256, outCols / 256);

            /* Read pbm rows, each padded to full byte */

            for (idx = 0; idx < rowsThisStripe; ++idx) {
                pbm_readpbmrow_packed (ifP, bitrow[idx], cols, format);
                pbm_cleanrowend_packed(bitrow[idx], cols);
            }

            /* If at bottom pad with empty rows up to stripe height */
            if (rowsThisStripe < cmdline.stripeHeight )
                memset(bitrow[rowsThisStripe], 0,
                       (cmdline.stripeHeight - rowsThisStripe) * outColByteCt);

            /* Write raster data */
            if (cmdline.compress != 0) {  /* compressed */
                size_t compressedDataCt;

                pm_rlenc_compressbyte(inBuff, compressedData, PM_RLE_PACKBITS,
                                      stripeByteCt, &compressedDataCt);
                fwrite(compressedData, compressedDataCt, 1, stdout);
            } else                        /* uncompressed */
                fwrite(inBuff, stripeByteCt, 1, stdout);

            /* Emit newline to print the stripe */
            putchar('\n');
        }
    }

    free(inBuff); 
    free(compressedData);
    pm_close(ifP);

    /* Form feed */
    if (cmdline.formfeed)
        putchar('\f');

    if (!cmdline.raw) {
        /* Reset printer. a*/
        printf("%c%c", esc, '@');
    }

    return 0;
}
