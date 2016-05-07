/*
 * pbmtextps.c -  render text into a bitmap using a postscript interpreter
 *
 * Copyright (C) 2002 by James McCann.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  This software is provided "as is" without express or
 * implied warranty.
 *
 * PostScript is a registered trademark of Adobe Systems International.
 *
 * Additions by Bryan Henderson contributed to public domain by author.
 *
 */
#define _XOPEN_SOURCE   /* Make sure popen() is in stdio.h */
#define _BSD_SOURCE     /* Make sure stdrup() is in string.h */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pm_system.h"
#include "pbm.h"


#define BUFFER_SIZE 2048

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    unsigned int res;         /* resolution, DPI */
    unsigned int fontsize;    /* Size of font in points */
    const char * font;      /* Name of postscript font */
    float        stroke;
        /* Width of stroke in points (only for outline font) */
    unsigned int verbose;
    const char * text;
};



static void
writeFileToStdout(const char * const fileName){
    /* simple pbmtopbm */

    FILE * ifP;
    int format;
    int cols, rows, row ;
    unsigned char * bitrow; 
    
    ifP = pm_openr(fileName);
    pbm_readpbminit(ifP, &cols, &rows, &format);

    if (cols==0 || rows==0 || cols>INT_MAX-10 || rows>INT_MAX-10)
      pm_error("Abnormal output from gs program.  "
               "width x height = %u x %u", cols, rows);
               
    pbm_writepbminit(stdout, cols, rows, 0);           
               
    bitrow = pbm_allocrow_packed(cols);
    
    for (row = 0; row < rows; ++row) {
        pbm_readpbmrow_packed(ifP, bitrow, cols, format);
        pbm_writepbmrow_packed(stdout, bitrow, cols, 0);
    }
    pbm_freerow_packed(bitrow);
}



static void
buildTextFromArgs(int           const argc,
                  const char ** const argv,
                  const char ** const textP) {

    char * text;
    unsigned int totalTextSize;
    unsigned int i;

    text = strdup("");
    totalTextSize = 1;

    for (i = 1; i < argc; ++i) {
        if (i > 1) {
            totalTextSize += 1;
            text = realloc(text, totalTextSize);
            if (text == NULL)
                pm_error("out of memory");
            strcat(text, " ");
        } 
        totalTextSize += strlen(argv[i]);
        text = realloc(text, totalTextSize);
        if (text == NULL)
            pm_error("out of memory");
        strcat(text, argv[i]);
    }
    *textP = text;
}



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*---------------------------------------------------------------------------
  Note that the file spec array we return is stored in the storage that
  was passed to us as the argv array.
---------------------------------------------------------------------------*/
    optEntry * option_def;
    /* Instructions to OptParseOptions2 on how to parse our options.
   */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "resolution", OPT_UINT,   &cmdlineP->res,            NULL,  0);
    OPTENT3(0, "font",       OPT_STRING, &cmdlineP->font,           NULL,  0);
    OPTENT3(0, "fontsize",   OPT_UINT,   &cmdlineP->fontsize,       NULL,  0);
    OPTENT3(0, "stroke",     OPT_FLOAT,  &cmdlineP->stroke,         NULL,  0);
    OPTENT3(0, "verbose",    OPT_FLAG,   NULL, &cmdlineP->verbose,         0);

    /* Set the defaults */
    cmdlineP->res = 150;
    cmdlineP->fontsize = 24;
    cmdlineP->font = "Times-Roman";
    cmdlineP->stroke = -1;

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);

    buildTextFromArgs(argc, argv, &cmdlineP->text);
}



static const char *
construct_postscript(struct CmdlineInfo const cmdline) {

    const char * retval;
    const char * template;

    if (cmdline.stroke < 0) 
        template =
            "/%s findfont\n"
            "%d scalefont\n"
            "setfont\n"
            "12 36 moveto\n"
            "(%s) show\n"
            "showpage\n";
    else 
        template =
            "/%s findfont\n"
            "%d scalefont\n"
            "setfont\n"
            "12 36 moveto\n"
            "%f setlinewidth\n"
            "0 setgray\n"
            "(%s) true charpath\n"
            "stroke\n"
            "showpage\n";

    if (cmdline.stroke < 0)
        pm_asprintf(&retval, template, cmdline.font, cmdline.fontsize, 
                    cmdline.text);
    else
        pm_asprintf(&retval, template, cmdline.font, cmdline.fontsize, 
                    cmdline.stroke, cmdline.text);

    return retval;
}



static const char *
gsExecutableName() {

    const char * const which = "which gs";

    static char buffer[BUFFER_SIZE];

    FILE * f;

    memset(buffer, 0, BUFFER_SIZE);

    f = popen(which, "r");
    if (!f)
        pm_error("Can't find ghostscript");

    fread(buffer, 1, BUFFER_SIZE, f);
    if (buffer[strlen(buffer) - 1] == '\n')
        buffer[strlen(buffer) - 1] = '\0';
    pclose(f);
    
    if (buffer[0] != '/' && buffer[0] != '.')
        pm_error("Can't find ghostscript");

    return buffer;
}



static const char *
cropExecutableName(void) {

    const char * const which = "which pnmcrop";

    static char buffer[BUFFER_SIZE];
    const char * retval;

    FILE * f;

    memset(buffer, 0, BUFFER_SIZE);

    f = popen(which, "r");
    if (!f)
        retval = NULL;
    else {
        fread(buffer, 1, BUFFER_SIZE, f);
        if (buffer[strlen(buffer) - 1] == '\n')
            buffer[strlen(buffer) - 1] = 0;
        pclose(f);
            
        if (buffer[0] != '/' && buffer[0] != '.') {
            retval = NULL;
            pm_message("Can't find pnmcrop");
        } else
            retval = buffer;
    }
    return retval;
}



static const char **
gsArgList(const char *       const psFname,
          const char *       const outputFilename, 
          struct CmdlineInfo const cmdline) {

    unsigned int const maxArgCt = 50;
    double const x = (double) cmdline.res * 11;
    double const y = (double) cmdline.res * 
                     ((double) cmdline.fontsize * 2 + 72)  / 72;
    
    const char ** retval;
    unsigned int argCt;  /* Number of arguments in 'retval' so far */

    if (cmdline.res <= 0)
         pm_error("Resolution (dpi) must be positive.");
    
    if (cmdline.fontsize <= 0)
         pm_error("Font size must be positive.");
    
    /* The following checks are for guarding against overflows in this 
       function.  Huge x,y values that pass these checks may be
       rejected by the 'gs' program.
    */
    
    if (x > (double) INT_MAX-10)
         pm_error("Absurdly fine resolution: %u. Output width too large.",
                   cmdline.res );
    if (y > (double) INT_MAX-10)
         pm_error("Absurdly fine resolution (%u) and/or huge font size (%u). "
                  "Output height too large.", cmdline.res, cmdline.fontsize);
    
    MALLOCARRAY_NOFAIL(retval, maxArgCt+2);

    argCt = 0;  /* initial value */

    pm_asprintf(&retval[argCt++], "ghostscript");

    pm_asprintf(&retval[argCt++], "-g%dx%d", (int) x, (int) y);
    pm_asprintf(&retval[argCt++], "-r%d", cmdline.res);
    pm_asprintf(&retval[argCt++], "-sDEVICE=pbm");
    pm_asprintf(&retval[argCt++], "-sOutputFile=%s", outputFilename);
    pm_asprintf(&retval[argCt++], "-q");
    pm_asprintf(&retval[argCt++], "-dBATCH");
    pm_asprintf(&retval[argCt++], "-dNOPAUSE");
    pm_asprintf(&retval[argCt++], "%s", psFname);

    retval[argCt++] = NULL;

    return retval;
}



static const char **
cropArgList(const char * const inputFileName) {

    unsigned int const maxArgCt = 50;

    const char ** retval;
    unsigned int argCt;

    MALLOCARRAY_NOFAIL(retval, maxArgCt+2);

    argCt = 0;  /* initial value */

    pm_asprintf(&retval[argCt++], "pnmcrop");

    pm_asprintf(&retval[argCt++], "-top");
    pm_asprintf(&retval[argCt++], "-right");
    if (pm_plain_output)
        pm_asprintf(&retval[argCt++], "-plain");
    pm_asprintf(&retval[argCt++], "%s", inputFileName);

    retval[argCt++] = NULL;

    return retval;
}



static void
writeProgram(const char *       const psFname,
             struct CmdlineInfo const cmdline) {

    const char * ps;
    FILE * psfile;

    psfile = fopen(psFname, "w");
    if (psfile == NULL)
        pm_error("Can't open temp file '%s'.  Errno=%d (%s)",
                 psFname, errno, strerror(errno));

    ps = construct_postscript(cmdline);

    if (cmdline.verbose)
        pm_message("Postscript program = '%s'", ps);
        
    if (fwrite(ps, 1, strlen(ps), psfile) != strlen(ps))
        pm_error("Can't write postscript to temp file");

    fclose(psfile);

    pm_strfree(ps);
}



static void
reportGhostScript(const char *  const executableNm,
                  const char ** const argList) {

    unsigned int i;

    pm_message("Running Ghostscript interpreter '%s'", executableNm);

    pm_message("Program arguments:");

    for (i = 0; argList[i]; ++i)
        pm_message("  '%s'", argList[i]);
}



static void
freeArgList(const char ** const argList) {

    unsigned int i;

    for (i = 0; argList[i]; ++i)
        pm_strfree(argList[i]);

    free(argList);
}



static void
executeProgram(const char *       const psFname, 
               const char *       const outputFname,
               struct CmdlineInfo const cmdline) {

    const char * const executableNm = gsExecutableName();
    const char ** const argList = gsArgList(psFname, outputFname, cmdline);
    int termStatus;

    if (cmdline.verbose)
        reportGhostScript(executableNm, argList);

    pm_system2_vp(executableNm,
                  argList,
                  pm_feed_null, NULL,
                  pm_accept_null, NULL,
                  &termStatus);

    if (termStatus != 0) {
        const char * const msg = pm_termStatusDesc(termStatus);

        pm_error("Failed to run Ghostscript process.  %s", msg);

        pm_strfree(msg);
    }
    freeArgList(argList);
}



static void
reportCrop(const char *  const executableNm,
           const char ** const argList) {

    unsigned int i;

    pm_message("Running '%s' to crop the output", executableNm);

    pm_message("Program arguments:");

    for (i = 0; argList[i]; ++i)
        pm_message("  '%s'", argList[i]);
}



static void
cropToStdout(const char * const inputFileName,
             bool         const verbose) {

    const char * executableNm = cropExecutableName();

    if (executableNm) {
        const char ** const argList = cropArgList(inputFileName);

        int termStatus;

        if (verbose)
            reportCrop(executableNm, argList);
    
        pm_system2_vp(executableNm,
                      argList,
                      pm_feed_null, NULL,
                      NULL, NULL,
                      &termStatus);
        
        if (termStatus != 0) {
            const char * const msg = pm_termStatusDesc(termStatus);

            pm_error("Failed to run pnmcrop process.  %s", msg);

            pm_strfree(msg);
        }
        freeArgList(argList);
    } else {
        /* No pnmcrop.  So don't crop. */
        pm_message("Can't find pnmcrop program, image will be large");
        writeFileToStdout(inputFileName);
    }
}



static void
createOutputFile(struct CmdlineInfo const cmdline) {

    const char * const template = "./pstextpbm.%d.tmp.%s";
    
    const char * psFname;
    const char * uncroppedPbmFname;

    pm_asprintf(&psFname, template, getpid(), "ps");
    if (psFname == NULL)
        pm_error("Unable to allocate memory");
 
    writeProgram(psFname, cmdline);

    pm_asprintf(&uncroppedPbmFname, template, getpid(), "pbm");
    if (uncroppedPbmFname == NULL)
        pm_error("Unable to allocate memory");
 
    executeProgram(psFname, uncroppedPbmFname, cmdline);

    unlink(psFname);
    pm_strfree(psFname);

    cropToStdout(uncroppedPbmFname, cmdline.verbose);

    unlink(uncroppedPbmFname);
    pm_strfree(uncroppedPbmFname);
}



int 
main(int argc, const char *argv[]) {

    struct CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    createOutputFile(cmdline);

    return 0;
}
