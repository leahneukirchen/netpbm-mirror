/*
 * Convert a ATARI Degas .pi3 file to a portable bitmap file.
 *
 * Author: David Beckemeyer
 *
 * This code was derived from the original gemtopbm program written
 * by Diomidis D. Spinellis.
 *
 * (C) Copyright 1988 David Beckemeyer and Diomidis D. Spinellis.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appear in all copies and that
 * both that copyright notice and this permission notice appear in
 * supporting documentation.
 *
 * This file is provided AS IS with no warranties of any kind.  The author
 * shall have no liability with respect to the infringement of copyrights,
 * trade secrets or any patents by this file or any part thereof.  In no
 * event will the author be liable for any lost revenue or profits or
 * other special, indirect and consequential damages.
 */

#include <stdio.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pbm.h"



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* Filename of input file */
    unsigned int debug;
};



static void 
parseCommandLine(int argc, 
                 const char ** argv, 
                 struct CmdlineInfo * const cmdlineP) {
/* --------------------------------------------------------------------------
   Parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.  

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
--------------------------------------------------------------------------*/
    optEntry * option_def;
    optStruct3 opt;
        /* Instructions to pm_optParseOptions3 on how to parse our options. */
    unsigned int option_def_index;
  
    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "debug",    OPT_FLAG,    NULL,       &cmdlineP->debug,       0);
  
    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;   /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 < 1) 
        cmdlineP->inputFileName = "-";
    else {
        cmdlineP->inputFileName = argv[1];

        if (argc-1 > 1)
            pm_error("Program takes zero or one argument (filename).  You "
                     "specified %u", argc-1);
    }
}



static void
readAndValidateHeader(FILE * const ifP,
                      bool   const debug,
                      bool * const reverseP) {

    short item;

    pm_readbigshort(ifP, &item);

    if (debug)
        pm_message("resolution is %d", item);

    /* only handles hi-rez 640x400 */
    if (item != 2)
        pm_error("bad resolution %d", item);

    pm_readbigshort(ifP, &item);

    *reverseP = (item == 0);

    {
        unsigned int i;

        for (i = 1; i < 16; ++i)
            pm_readbigshort (ifP, &item);
    }
}



int
main(int argc, const char ** argv) {

    unsigned int const rows = 400;
    unsigned int const cols = 640;

    struct CmdlineInfo cmdline;
    FILE * ifP;
    unsigned int row;
    bit * bitrow;
    bool reverse;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    readAndValidateHeader(ifP, cmdline.debug, &reverse);

    pbm_writepbminit(stdout, cols, rows, 0);

    bitrow = pbm_allocrow_packed(cols);

    for (row = 0; row < rows; ++row) {
        unsigned int const colChars = cols / 8;

        unsigned int bytesReadCt;

        bytesReadCt = fread(bitrow, cols / 8, 1, ifP);
        if (bytesReadCt != 1) {
            if (feof(ifP))
                pm_error( "EOF reached while reading image data" );
            else
                pm_error("read error while reading image data");
        }

        if (reverse) {
            /* flip all pixels */
            unsigned int colChar;
            for (colChar = 0; colChar < colChars; ++colChar)
                bitrow[colChar] = ~bitrow[colChar];
        }
        pbm_writepbmrow_packed(stdout, bitrow, cols, 0);
    }

    pbm_freerow_packed(bitrow);
    pm_close(ifP);
    pm_close(stdout);

    return 0;
}
