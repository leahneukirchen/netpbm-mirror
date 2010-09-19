/*=============================================================================
                               pamtopdbimg
===============================================================================

  Convert Netpbm image to Palm Pilot PDB Image format (for viewing by
  Pilot Image Viewer).

  Bryan Henderson derived this from Eric Howe's programs named
  'pgmtoimgv' and 'pbmtoimgv'.
=============================================================================*/
/*
 * Copyright (C) 1997 Eric A. Howe
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *   Authors:  Eric A. Howe (mu@trends.net)
 *             Bryan Henderson, September 2010.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pam.h"

#include "ipdb.h"

enum CompMode {COMPRESSED, MAYBE, UNCOMPRESSED};

struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char *  inputFileName;  /* '-' if stdin */
    const char * title;
    const char * notefile;  /* NULL if not specified */
    enum CompMode compMode;
    unsigned int depth4;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct cmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.  

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry *option_def;
        /* Instructions to optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int titleSpec, notefileSpec;
    unsigned int compressed, maybeCompressed, uncompressed;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "title",               OPT_STRING,    &cmdlineP->title,
            &titleSpec,               0);
    OPTENT3(0, "notefile",            OPT_STRING,    &cmdlineP->notefile,
            &notefileSpec,            0);
    OPTENT3(0, "compressed",          OPT_FLAG,      NULL,
            &compressed,              0);
    OPTENT3(0, "maybecompressed",     OPT_FLAG,      NULL,
            &maybeCompressed,         0);
    OPTENT3(0, "uncompressed",        OPT_FLAG,      NULL,
            &uncompressed,            0);
    OPTENT3(0, "4depth",              OPT_FLAG,      NULL,
            &cmdlineP->depth4,        0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */


    if (!titleSpec)
        cmdlineP->title = "unnamed";

    if (!notefileSpec)
        cmdlineP->notefile = NULL;
    
    if (compressed + uncompressed + maybeCompressed > 1)
        pm_error("You may specify only one of -compressed, -uncompressed, "
                 "-maybecompressed");
    if (compressed)
        cmdlineP->compMode = COMPRESSED;
    else if (uncompressed)
        cmdlineP->compMode = UNCOMPRESSED;
    else if (maybeCompressed)
        cmdlineP->compMode = MAYBE;
    else
        cmdlineP->compMode = MAYBE;

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileName = argv[1];
    else
        pm_error("Program takes at most one argument:  input file name");
}



static void
readimg(IPDB * const pdbP,
        FILE * const ifP,
        bool   const depth4) {

    struct pam inpam;
    tuple * tuplerow;
    int status;
    uint8_t * imgRaster;
    unsigned int row;

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    if (strneq(inpam.tuple_type, "RGB", 3))
        pm_error("Input image is color.  Cannot make a Palm color image.");

    MALLOCARRAY(imgRaster, inpam.width * inpam.height);

    tuplerow = pnm_allocpamrow(&inpam);

    for (row = 0; row < inpam.height; ++row) {
        unsigned int col;

        pnm_readpamrow(&inpam, tuplerow);

        for (col = 0; col < inpam.width; ++col)
            imgRaster[row * inpam.height + col] = tuplerow[col][0];
    }

    if (inpam.maxval == 1)
        status = ipdb_insert_mimage(pdbP, inpam.width, inpam.height,
                                    imgRaster);
    else if (depth4)
        status = ipdb_insert_g16image(pdbP, inpam.width, inpam.height,
                                      imgRaster);
    else
        status = ipdb_insert_gimage(pdbP, inpam.width, inpam.height,
                                    imgRaster);

    if (status != 0)
        pm_error("ipdb_insert failed.  Error %d (%s)",
                 status, ipdb_err(status));

    pnm_freepamrow(tuplerow);
    free(imgRaster);
}



static void
readtxt(IPDB *       const pdbP,
        const char * const noteFileName) {

    struct stat st;
    char * fileContent;
    FILE * fP;
    int n;
    int rc;
    size_t bytesRead;

    rc = stat(noteFileName, &st);

    if (rc != 0)
        pm_error("stat of '%s' failed, errno = %d (%s)",
                 noteFileName, errno, strerror(errno));

    fP = pm_openr(noteFileName);

    MALLOCARRAY(fileContent, st.st_size + 1);

    if (fileContent == NULL)
        pm_error("Couldn't get %lu bytes of storage to read in note file",
                 (unsigned long) st.st_size);

    bytesRead = fread(fileContent, 1, st.st_size, fP);

    if (bytesRead != st.st_size)
        pm_error("Failed to read note file '%s'.  Errno = %d (%s)",
                 noteFileName, errno, strerror(errno));

    pm_close(fP);

    /* Chop of trailing newlines */
    for (n = strlen(fileContent) - 1; n >= 0 && fileContent[n] == '\n'; --n)
        fileContent[n] = '\0';

    ipdb_insert_text(pdbP, fileContent);
}



int
main(int argc, const char **argv) {

    struct cmdlineInfo cmdline;
    IPDB * pdbP;
    FILE * ifP;
    int comp;
    int status;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    switch (cmdline.compMode) {
    case COMPRESSED:   comp = IPDB_COMPRESS;   break;
    case UNCOMPRESSED: comp = IPDB_NOCOMPRESS; break;
    case MAYBE:        comp = IPDB_COMPMAYBE;  break;
    }

    pdbP = ipdb_alloc(cmdline.title);

    if (pdbP == NULL)
        pm_error("Failed to allocate IPDB structure");

    readimg(pdbP, ifP, cmdline.depth4);

    if (cmdline.notefile)
        readtxt(pdbP, cmdline.notefile);

    status = ipdb_write(pdbP, comp, stdout);

    if (status != 0)
        pm_error("Failed to write PDB.  %s.", ipdb_err(status));

    if (comp == IPDB_COMPMAYBE && !ipdb_compressed(pdbP))
        pm_message("Image too complex to be compressed.");

    ipdb_free(pdbP);

    pm_close(ifP);

    return EXIT_SUCCESS;
}
