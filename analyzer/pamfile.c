/* pamfile.c - describe a Netpbm image
**
** Copyright (C) 1991 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdbool.h>
#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pam.h"

enum ReportFormat {RF_HUMAN, RF_COUNT, RF_MACHINE, RF_SIZE};

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    int inputFileCount;  /* Number of input files */
    const char ** inputFilespec;  /* Filespecs of input files */
    unsigned int allimages;
    unsigned int comments;
    enum ReportFormat reportFormat;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to as as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;
    unsigned int countSpec, machineSpec, sizeSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "allimages", OPT_FLAG,  NULL, &cmdlineP->allimages,   0);
    OPTENT3(0,   "count",     OPT_FLAG,  NULL, &countSpec,             0);
    OPTENT3(0,   "comments",  OPT_FLAG,  NULL, &cmdlineP->comments,    0);
    OPTENT3(0,   "machine",   OPT_FLAG,  NULL, &machineSpec,           0);
    OPTENT3(0,   "size",      OPT_FLAG,  NULL, &sizeSpec,              0);

    opt.opt_table     = option_def;
    opt.short_allowed = false; /* We have no short (old-fashioned) options */
    opt.allowNegNum   = false; /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others */

    cmdlineP->inputFilespec = (const char **)&argv[1];
    cmdlineP->inputFileCount = argc - 1;

    if (machineSpec + sizeSpec + countSpec > 1)
        pm_error("You can specify only one of -machine, -size, and -count");
    else if (machineSpec)
        cmdlineP->reportFormat = RF_MACHINE;
    else if (sizeSpec)
        cmdlineP->reportFormat = RF_SIZE;
    else if (countSpec)
        cmdlineP->reportFormat = RF_COUNT;
    else
        cmdlineP->reportFormat = RF_HUMAN;

    free(option_def);
}



static void
dumpHeaderHuman(struct pam const pam) {

    switch (pam.format) {
    case PAM_FORMAT:
        printf("PAM, %d by %d by %d maxval %ld\n",
               pam.width, pam.height, pam.depth, pam.maxval);
        printf("    Tuple type: %s\n", pam.tuple_type);
        break;

        case PBM_FORMAT:
        printf("PBM plain, %d by %d\n", pam.width, pam.height );
        break;

        case RPBM_FORMAT:
        printf("PBM raw, %d by %d\n", pam.width, pam.height);
        break;

        case PGM_FORMAT:
        printf("PGM plain, %d by %d  maxval %ld\n",
               pam.width, pam.height, pam.maxval);
        break;

        case RPGM_FORMAT:
        printf("PGM raw, %d by %d  maxval %ld\n",
               pam.width, pam.height, pam.maxval);
        break;

        case PPM_FORMAT:
        printf("PPM plain, %d by %d  maxval %ld\n",
               pam.width, pam.height, pam.maxval);
        break;

        case RPPM_FORMAT:
        printf("PPM raw, %d by %d  maxval %ld\n",
               pam.width, pam.height, pam.maxval);
        break;
    }
}



static void
dumpHeaderMachine(struct pam const pam) {

    const char * formatString;
    bool plain;

    switch (pam.format) {
    case PAM_FORMAT:
        formatString = "PAM";
        plain = false;
        break;

        case PBM_FORMAT:
        formatString = "PBM";
        plain = TRUE;
        break;

        case RPBM_FORMAT:
        formatString = "PBM";
        plain = false;
        break;

        case PGM_FORMAT:
        formatString = "PGM";
        plain = TRUE;
        break;

        case RPGM_FORMAT:
        formatString = "PGM";
        plain = false;
        break;

        case PPM_FORMAT:
        formatString = "PPM";
        plain = TRUE;
        break;

        case RPPM_FORMAT:
        formatString = "PPM";
        plain = false;        break;
    }

    printf("%s %s %d %d %d %ld %s\n", formatString,
           plain ? "PLAIN" : "RAW", pam.width, pam.height,
           pam.depth, pam.maxval, pam.tuple_type);

}


static void
dumpHeaderSize(struct pam const pam) {

    printf("%d %d\n", pam.width, pam.height);
}


static void
dumpComments(const char * const comments) {

    const char * p;
    bool startOfLine;

    printf("Comments:\n");

    for (p = &comments[0], startOfLine = TRUE; *p; ++p) {
        if (startOfLine)
            printf("  #");

        fputc(*p, stdout);

        if (*p == '\n')
            startOfLine = TRUE;
        else
            startOfLine = false;
    }
    if (!startOfLine)
        fputc('\n', stdout);
}



static void
readToNextImage(const struct pam * const pamP,
                bool *             const eofP) {

    tuple * tuplerow;
    unsigned int row;

    tuplerow = pnm_allocpamrow(pamP);

    for (row = 0; row < pamP->height; ++row)
        pnm_readpamrow(pamP, tuplerow);

    pnm_freepamrow(tuplerow);

    {
        int eof;
        pnm_nextimage(pamP->file, &eof);
        *eofP = eof;
    }
}



static void
doOneImage(const char *      const name,
           unsigned int      const imageDoneCt,
           FILE *            const fileP,
           enum ReportFormat const reportFormat,
           bool              const allimages,
           bool              const wantComments,
           bool *            const eofP) {

    struct pam pam;
    const char * comments;
    enum pm_check_code checkRetval;

    pam.comment_p = &comments;

    pnm_readpaminit(fileP, &pam, PAM_STRUCT_SIZE(comment_p));

    switch (reportFormat) {
    case RF_COUNT:
        break;
    case RF_SIZE:
        dumpHeaderSize(pam);
        break;
    case RF_MACHINE:
        printf("%s: ", name);
        dumpHeaderMachine(pam);
        break;
    case RF_HUMAN:
        if (allimages)
            printf("%s:\tImage %d:\t", name, imageDoneCt);
        else
            printf("%s:\t", name);

        dumpHeaderHuman(pam);

        if (wantComments)
            dumpComments(comments);
    }
    pm_strfree(comments);

    pnm_checkpam(&pam, PM_CHECK_BASIC, &checkRetval);

    if (allimages)
        readToNextImage(&pam, eofP);
}



static void
describeOneFile(const char *      const name,
                FILE *            const fileP,
                enum ReportFormat const reportFormat,
                bool              const allimages,
                bool              const wantComments) {
/*----------------------------------------------------------------------------
   Describe one image stream (file).  Its name, for purposes of display,
   is 'name'.  The file is open as *fileP and positioned to the beginning.

   'reportFormat' tells which of the various sets of information we provide.

   'allimages' means report on every image in the stream and read all of
   every image from it, as opposed to reading just the header of the first
   image and reporting just on that.

   'wantComments' means to show the comments from the image header.
   Meaningful only when 'reportFormat' is RF_HUMAN.
-----------------------------------------------------------------------------*/
    unsigned int imageDoneCt;
        /* Number of images we've processed so far */
    bool eof;

    for (eof = false, imageDoneCt = 0;
         !eof && (imageDoneCt < 1 || allimages);
        ++imageDoneCt
        ) {
        doOneImage(name, imageDoneCt, fileP,
                   reportFormat, allimages, wantComments,
                   &eof);
    }
    if (reportFormat == RF_COUNT)
        printf("%s:\t%u images\n", name, imageDoneCt);
}



int
main(int argc, const char *argv[]) {

    struct CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    if (cmdline.inputFileCount == 0)
        describeOneFile("stdin", stdin, cmdline.reportFormat,
                        cmdline.allimages ||
                            cmdline.reportFormat == RF_COUNT,
                        cmdline.comments);
    else {
        unsigned int i;
        for (i = 0; i < cmdline.inputFileCount; ++i) {
            FILE * ifP;

            ifP = pm_openr(cmdline.inputFilespec[i]);

            describeOneFile(cmdline.inputFilespec[i], ifP,
                            cmdline.reportFormat,
                            cmdline.allimages ||
                                cmdline.reportFormat == RF_COUNT,
                            cmdline.comments);

            pm_close(ifP);
            }
        }

    return 0;
}


