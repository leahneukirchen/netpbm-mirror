/* pbmtonokia.c - convert a PBM image to Nokia Smart Messaging
   Formats (NOL, NGG, HEX)

   Copyright information is at end of file.
*/

#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */
#define _BSD_SOURCE    /* Make sure strcaseeq() is in nstring.h */
#include <string.h>
#include <assert.h>

#include "pm_c_util.h"
#include "nstring.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pbm.h"

enum outputFormat {
    FMT_HEX_NOL,
    FMT_HEX_NGG,
    FMT_HEX_NPM,
    FMT_NOL,
    FMT_NGG,
    FMT_NPM
};


struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* Filename of input files */
    int outputFormat;
    const char * networkCode;
    const char * txt;  /* NULL means unspecified */
};



static const char *
uppercase(const char * const subject) {

    char * buffer;

    buffer = malloc(strlen(subject) + 1);

    if (buffer == NULL)
        pm_error("Out of memory allocating buffer for uppercasing a "
                 "%u-character string", (unsigned)strlen(subject));
    else {
        unsigned int i;

        i = 0;
        while (subject[i]) {
            buffer[i] = TOUPPER(subject[i]);
            ++i;
        }
        buffer[i] = '\0';
    }
    return buffer;
}



static void
parseCommandLine(int argc, char ** argv,
                 struct cmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;
    unsigned int fmtSpec, netSpec, txtSpec;
    const char * fmtOpt;
    const char * netOpt;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "fmt",     OPT_STRING, &fmtOpt, 
            &fmtSpec, 0);
    OPTENT3(0, "net",     OPT_STRING, &netOpt,
            &netSpec, 0);
    OPTENT3(0, "txt",     OPT_STRING, &cmdlineP->txt,
            &txtSpec, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (fmtSpec) {
        if (strcaseeq(fmtOpt, "HEX_NOL"))
            cmdlineP->outputFormat = FMT_HEX_NOL;
        else if (strcaseeq(fmtOpt, "HEX_NGG"))
            cmdlineP->outputFormat = FMT_HEX_NGG;
        else if (strcaseeq(fmtOpt, "HEX_NPM"))
            cmdlineP->outputFormat = FMT_HEX_NPM;
        else if (strcaseeq(fmtOpt, "NOL"))
            cmdlineP->outputFormat = FMT_NOL;
        else if (strcaseeq(fmtOpt, "NGG"))
            cmdlineP->outputFormat = FMT_NGG;
        else if (strcaseeq(fmtOpt, "NPM"))
            cmdlineP->outputFormat = FMT_NPM;
        else
            pm_error("-fmt option must be HEX_NGG, HEX_NOL, HEX_NPM, "
                     "NGG, NOL or NPM.  You specified '%s'", fmtOpt);
    } else
        cmdlineP->outputFormat = FMT_HEX_NOL;

    if (netSpec) {
        if (strlen(netOpt) != 6)
            pm_error("-net option must be 6 hex digits long.  "
                     "You specified %u characters", (unsigned)strlen(netOpt));
        else if (!pm_strishex(netOpt))
            pm_error("-net option must be hexadecimal.  You specified '%s'",
                     netOpt);
        else
            cmdlineP->networkCode = uppercase(netOpt);
    } else
        cmdlineP->networkCode = strdup("62F210");  /* German D1 net */

    if (!txtSpec)
        cmdlineP->txt = NULL;
    else if (strlen(cmdlineP->txt) > 120)
        pm_error("Text message is longer (%u characters) than "
                 "the 120 characters allowed by the format.",
                 (unsigned)strlen(cmdlineP->txt));

    if (argc-1 == 0) 
        cmdlineP->inputFileName = "-";
    else if (argc-1 != 1)
        pm_error("Program takes zero or one argument (filename).  You "
                 "specified %u", argc-1);
    else
        cmdlineP->inputFileName = argv[1];
}



static void
freeCmdline(struct cmdlineInfo const cmdline) {

    pm_strfree(cmdline.networkCode);
}



static void
validateSize(unsigned int const cols,
             unsigned int const rows){

    if (cols > 255)
        pm_error("This program cannot handle files with more than 255 "
                 "columns");
    if (rows > 255)
        pm_error("This program cannot handle files with more than 255 "
                 "rows");
}




static void
convertToHexNol(bit **       const image,
                unsigned int const cols,
                unsigned int const rows,
                const char * const networkCode,
                FILE *       const ofP) {

    unsigned int row;

    /* header */
    fprintf(ofP, "06050415820000%s00%02X%02X01", networkCode, cols, rows);
    
    /* image */
    for (row = 0; row < rows; ++row) {
        unsigned int col;
        unsigned int p;
        unsigned int c;

        c = 0;

        for (p = 0, col = 0; col < cols; ++col) {
            if (image[row][col] == PBM_BLACK)
                c |= 0x80 >> p;
            if (++p == 8) {
                fprintf(ofP, "%02X",c);
                p = c = 0;
            }
        }
        if (p > 0)
            fprintf(ofP, "%02X", c);
    }
}



static void
convertToHexNgg(bit **       const image,
                unsigned int const cols,
                unsigned int const rows,
                FILE *       const ofP) {

    unsigned int row;

    /* header */
    fprintf(ofP, "0605041583000000%02X%02X01", cols, rows);

    /* image */
    for (row = 0; row < rows; ++row) {
        unsigned int col;
        unsigned int p;
        unsigned int c;

        for (p = 0, c = 0, col = 0; col < cols; ++col) {
            if (image[row][col] == PBM_BLACK)
                c |= 0x80 >> p;
            if (++p == 8) {
                fprintf(ofP, "%02X", c);
                p = c = 0;
            }
        }
        if (p > 0)
            fprintf(ofP, "%02X", c);
    }
}




static void
convertToHexNpm(bit **       const image,
                unsigned int const cols,
                unsigned int const rows,
                const char * const text,
                FILE *       const ofP) {

    unsigned int row;
    
    /* header */
    fprintf(ofP, "060504158A0000");

    /* text */
    if (text) {
        size_t const len = strlen(text);

        unsigned int it;

        fprintf(ofP, "00%04X", (unsigned)len);

        for (it = 0; it < len; ++it)
            fprintf(ofP, "%02X", text[it]);
    }

    /* image */
    fprintf(ofP, "02%04X00%02X%02X01", (cols * rows) / 8 + 4, cols, rows);

    for (row = 0; row < rows; ++row) {
        unsigned int col;
        unsigned int p;
        unsigned int c;

        for (p = 0, c = 0, col = 0; col < cols; ++col) {
            if (image[row][col] == PBM_BLACK)
                c |= 0x80 >> p;
            if (++p == 8) {
                fprintf(ofP, "%02X", c);
                p = c = 0;
            }
        }
        if (p > 0)
            fprintf(ofP, "%02X", c);
    }
}



static void
convertToNol(bit **       const image,
             unsigned int const cols,
             unsigned int const rows,
             FILE *       const ofP) {

    unsigned int row;
    char header[32];
    unsigned int it;
    
    /* header - this is a hack */

    header[ 0] = 'N';
    header[ 1] = 'O';
    header[ 2] = 'L';
    header[ 3] = 0;
    header[ 4] = 1;
    header[ 5] = 0;
    header[ 6] = 4;
    header[ 7] = 1;
    header[ 8] = 1;
    header[ 9] = 0;
    header[10] = cols;
    header[11] = 0;
    header[12] = rows;
    header[13] = 0;
    header[14] = 1;
    header[15] = 0;
    header[16] = 1;
    header[17] = 0;
    header[18] = 0x53;
    header[19] = 0;

    fwrite(header, 20, 1, ofP);
    
    /* image */
    for (row = 0; row < rows; ++row) {
        unsigned int col;

        for (col = 0; col < cols; ++col) {
            char const output = image[row][col] == PBM_BLACK ? '1' : '0';

            putc(output, ofP);
        }
    }

    /* padding (to keep gnokii happy) */
    for (it = 0; it < 8 - cols * rows % 8; ++it)
        putc('0', ofP);
}




static void
convertToNgg(bit **       const image,
             unsigned int const cols,
             unsigned int const rows,
             FILE *       const ofP) {

    unsigned int row;
    char    header[32];
    unsigned int it;

    /* header - this is a hack */

    header[ 0] = 'N';
    header[ 1] = 'G';
    header[ 2] = 'G';
    header[ 3] = 0;
    header[ 4] = 1;
    header[ 5] = 0;
    header[ 6] = cols;
    header[ 7] = 0;
    header[ 8] = rows;
    header[ 9] = 0;
    header[10] = 1;
    header[11] = 0;
    header[12] = 1;
    header[13] = 0;
    header[14] = 0x4a;
    header[15] = 0;

    fwrite(header, 16, 1, ofP);
    
    /* image */

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        for (col = 0; col < cols; ++col) {
            char const output = image[row][col] == PBM_BLACK ? '1' : '0';

            putc(output, ofP);
        }
    }

    /* padding (to keep gnokii happy) */
    for (it = 0; it < 8 - cols * rows % 8; ++it)
        putc('0', ofP);
}



static void
convertToNpm(bit **       const image,
             unsigned int const cols,
             unsigned int const rows,
             const char * const text,
             FILE *       const ofP) {

    unsigned int row;
    char header[132];
    size_t len;

    if (text) 
        len = strlen(text);
    else
        len = 0;

    /* header and optional text */

    header[       0] = 'N';
    header[       1] = 'P';
    header[       2] = 'M';
    header[       3] = 0;
    header[       4] = len;
    header[       5] = 0;
    memcpy(&header[5], text, len);
    header[ 6 + len] = cols;
    header[ 7 + len] = rows;
    header[ 8 + len] = 1;
    header[ 9 + len] = 1;
    header[10 + len] = 0; /* unknown */

    assert(10 + len < sizeof(header));

    fwrite(header, 11 + len, 1, ofP);
    
    /* image: stream of bits, each row padded to a byte boundary
       inspired by gnokii/common/gsm-filesystems.c
     */
    for (row = 0; row < rows; row++) {
        unsigned int byteNumber;
        int bitNumber;
        char buffer[32];  /* picture messages are (always?) 72 x 28 */
        unsigned int col;

        byteNumber = 0;
        bitNumber = 7;

        memset(buffer, 0, sizeof(buffer));

        for (col = 0; col < cols; ++col) {
            if (image[row][col] == PBM_BLACK)
                buffer[byteNumber] |= (1 << bitNumber);
            --bitNumber;
            if (bitNumber < 0 && col < (cols - 1)) {
                bitNumber = 7;
                ++byteNumber;
            }
        }
        fwrite(buffer, byteNumber + 1, 1, ofP);
    }
}



int 
main(int    argc,
     char * argv[]) {

    struct cmdlineInfo cmdline;
    FILE  * ifP;
    bit ** bits;
    int rows, cols;

    pbm_init(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);
    bits = pbm_readpbm(ifP, &cols, &rows);
    pm_close(ifP);

    validateSize(cols, rows);

    switch (cmdline.outputFormat) {
    case FMT_HEX_NGG:
        convertToHexNgg(bits, cols, rows, stdout);
        break;
    case FMT_HEX_NOL:
        convertToHexNol(bits, cols, rows, cmdline.networkCode, stdout);
        break;
    case FMT_HEX_NPM:
        convertToHexNpm(bits, cols, rows, cmdline.txt, stdout);
        break;
    case FMT_NGG:
        convertToNgg(bits, cols, rows, stdout);
        break;
    case FMT_NOL:
        convertToNol(bits, cols, rows, stdout);
        break;
    case FMT_NPM:
        convertToNpm(bits, cols, rows, cmdline.txt, stdout);
        break;
    }

freeCmdline(cmdline);

    return 0;
}



/* Copyright (C)2001 OMS Open Media System GmbH, Tim Rühsen
** <tim.ruehsen@openmediasystem.de>.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.

  Created 2001.06.07

Notes:
  - limited to rows <= 255 and columns <= 255
  - limited to b/w graphics, not animated

Testing:
  Testing was done with SwissCom SMSC (Switzerland) and IC3S SMSC (Germany).
  The data was send with EMI/UCP protocol over TCP/IP.

  - 7.6.2001: tested with Nokia 3210: 72x14 Operator Logo
  - 7.6.2001: tested with Nokia 6210: 72x14 Operator Logo and 
              72x14 Group Graphic
*/
