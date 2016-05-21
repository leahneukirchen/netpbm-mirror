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
 * PostScript(R) Language Reference, Third Edition  (a.k.a. "Red Book")
 * http://www.adobe.com/products/postscript/pdfs/PLRM.pdf
 * ISBN 0-201-37922-8
 *
 * Other resources:
 * http://partners.adobe.com/public/developer/ps/index_specs.html
 */

#define _XOPEN_SOURCE   /* Make sure popen() is in stdio.h */
#define _BSD_SOURCE     /* Make sure stdrup() is in string.h */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pm_system.h"
#include "pbm.h"

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

    free(option_def);
}



static const char *
constructPostscript(struct CmdlineInfo const cmdline) {
/*-----------------------------------------------------------------------------
  In Postscript, the bottom of the page is row zero.  Negative values are
  allowed but negative regions are clipped from the output image.  We make
  adjustments to ensure that nothing is lost.

  Postscript fonts also allow negative values in the bounding box coordinates.
  The bottom edge of "L" is row zero.  The feet of "g" "p" "y" extend into
  negative region.  In a similar manner the left edge of the bounding box may
  be negative.  We add margins on the left and the bottom with "xinit" and
  "yinit" to provide for such characters.

  The sequence "textstring false charpath flattenpath pathbbox" determines
  the bounding box of the entire text when rendered.  We make adjustments
  with  "xdelta", "ydelta" whenever "xinit", "yinit" is insufficient.
  This may sound unlikely, but is likely to happen with right-to-left or
  vertical writing, which we haven't tested.
-----------------------------------------------------------------------------*/

    /* C89 limits the size of a string constant, so we have to build the
       Postscript command in pieces.
    */

    const char * const psTemplate =
        "/%s findfont\n"
        "/fontsize %u def\n"
        "/stroke %f def\n"
        "/textstring (%s) def\n";

    const char * const psFixed1 =
        "fontsize scalefont\n"
        "setfont\n"
        "/xinit fontsize 2   div def\n"
        "/yinit fontsize 1.5 mul def\n"
        "xinit yinit moveto\n"
        "textstring false charpath flattenpath pathbbox\n"
        "/top    exch def\n"
        "/right  exch def\n"
        "/bottom exch def\n"
        "/left   exch def\n"
        "/xdelta left   0 lt {left neg}   {0} ifelse def\n"
        "/ydelta bottom 0 lt {bottom neg} {0} ifelse def\n"
        "/width  right xdelta add def\n"
        "/height top   ydelta add def\n";

    const char * const psFixed2 =
        "<</PageSize [width height]>> setpagedevice\n"
        "xinit yinit moveto\n"
        "xdelta ydelta rmoveto\n" 
        "stroke 0 lt\n"
        "  {textstring show}\n"
        "  {stroke setlinewidth 0 setgray\n"
        "  textstring true charpath stroke}\n"
        "  ifelse\n"
        "textstring false charpath flattenpath pathbbox\n"
        "showpage";

    const char * retval;
    const char * psVariable;

    pm_asprintf(&psVariable, psTemplate, cmdline.font, cmdline.fontsize, 
                cmdline.stroke, cmdline.text);

    pm_asprintf(&retval, "%s%s%s", psVariable, psFixed1, psFixed2);

    pm_strfree(psVariable);

    return retval;
}



static const char **
gsArgList(const char *       const psFname,
          const char *       const outputFilename, 
          struct CmdlineInfo const cmdline) {

    unsigned int const maxArgCt = 50;
    
    const char ** retval;
    unsigned int argCt;  /* Number of arguments in 'retval' so far */

    if (cmdline.res <= 0)
         pm_error("Resolution (dpi) must be positive.");
    
    if (cmdline.fontsize <= 0)
         pm_error("Font size must be positive.");
  
    MALLOCARRAY_NOFAIL(retval, maxArgCt+2);

    argCt = 0;  /* initial value */

    pm_asprintf(&retval[argCt++], "ghostscript");

    pm_asprintf(&retval[argCt++], "-r%d", cmdline.res);
    pm_asprintf(&retval[argCt++], "-sDEVICE=pbmraw");
    pm_asprintf(&retval[argCt++], "-sOutputFile=%s", outputFilename);
    pm_asprintf(&retval[argCt++], "-q");
    pm_asprintf(&retval[argCt++], "-dBATCH");
    pm_asprintf(&retval[argCt++], "-dSAFER");
    pm_asprintf(&retval[argCt++], "-dNOPAUSE");
    pm_asprintf(&retval[argCt++], "%s", psFname);

    retval[argCt++] = NULL;

    assert(argCt < maxArgCt);

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

    ps = constructPostscript(cmdline);

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

    const char *  const executableNm = "gs";
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
writePbmToStdout(const char * const fileName){
/*----------------------------------------------------------------------------
  Write the PBM image that is in the file named 'fileName" to Standard
  Output.  I.e. pbmtopbm.

  It's not a byte-for-byte copy because PBM allows the same image to be
  represented many ways (all of which we can accept as our input), but we use
  libnetpbm to write our output in its specific way.
----------------------------------------------------------------------------*/
    FILE * ifP;
    int format;
    int cols, rows, row ;
    unsigned char * bitrow; 
    
    ifP = pm_openr(fileName);
    pbm_readpbminit(ifP, &cols, &rows, &format);

    if (cols == 0 || rows == 0 || cols > INT_MAX - 10 || rows > INT_MAX - 10)
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
createOutputFile(struct CmdlineInfo const cmdline) {

    const char * psFname;
    const char * tempPbmFname;
    FILE * psFileP;
    FILE * pbmFileP;

    pm_make_tmpfile(&psFileP, &psFname);
    assert(psFileP != NULL && psFname != NULL);
    fclose(psFileP);
 
    writeProgram(psFname, cmdline);

    pm_make_tmpfile(&pbmFileP, &tempPbmFname);
    assert(pbmFileP != NULL && tempPbmFname != NULL);
    fclose(pbmFileP);

    executeProgram(psFname, tempPbmFname, cmdline);

    unlink(psFname);
    pm_strfree(psFname);

    /* Although Ghostscript created a legal PBM file, it uses a different
       implementation of the format from libnetpbm's canonical output format,
       so instead of copying the content of 'tempPbmFname' to Standard output
       byte for byte, we copy it as a PBM image.
    */
    writePbmToStdout(tempPbmFname);

    unlink(tempPbmFname);
    pm_strfree(tempPbmFname);
}



int 
main(int argc, const char *argv[]) {

    struct CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    createOutputFile(cmdline);

    return 0;
}
