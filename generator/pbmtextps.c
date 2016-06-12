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
 * Postscript Font Naming Issues:
 * https://partners.adobe.com/public/developer/en/font/5088.FontNames.pdf
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

static void
validateFontName(const char * const name) {
/*-----------------------------------------------------------------------------
  Validate font name string.

  Abort with error message if it contains anything other than the printable
  characters in the ASCII 7-bit range, or any character with a special meaning
  in PostScript.
-----------------------------------------------------------------------------*/
    unsigned int idx;

    for (idx = 0; name[idx] != '\0'; ++idx) {
        char const c = name[idx]; 

        if (c < 32 || c > 125)
            pm_error("Invalid character in font name");
        else 
            switch (c) {
              case '[':   case ']':   case '(':   case ')':
              case '{':   case '}':   case '/':   case '\\':
              case '<':   case '>':   case '%':   case ' ':
              case '@':
                pm_error("Invalid character in font name");
            }
    }
}



static void
asciiHexEncode(char *          const inbuff,
               char *          const outbuff) {
/*-----------------------------------------------------------------------------
  Convert the input text string to ASCII-Hex encoding.

  Examples: "ABC abc 123" -> <4142432061626320313233>
            "FOO(BAR)FOO" -> <464f4f2842415229464f4f>
-----------------------------------------------------------------------------*/
    char const hexits[16] = "0123456789abcdef";

    unsigned int idx;

    for (idx = 0; inbuff[idx] != '\0'; ++idx) {
        unsigned int const item = (unsigned char) inbuff[idx]; 

        outbuff[idx*2]   = hexits[item >> 4];
        outbuff[idx*2+1] = hexits[item & 0xF];
    }

    outbuff[idx * 2] = '\0';
}



static void
buildTextFromArgs(int           const argc,
                  const char ** const argv,
                  const char ** const asciiHexTextP) {
/*----------------------------------------------------------------------------
   Build the array of text to be included in the Postscript program to
   be rendered, from the arguments of this program.

   We encode it in ASCII-Hex notation as opposed to using the plain text from
   the command line because 1) the command line might have Postscript control
   characters in it; and 2) the command line might have text in 8-bit or
   multibyte code, but a Postscript program is supposed to be entirely
   printable ASCII characters.
-----------------------------------------------------------------------------*/
    char * text;
    unsigned int totalTextSize;
    unsigned int i;

    text = strdup("");
    totalTextSize = 1;

    if (argc-1 < 1)
        pm_error("No text");

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

    { 
        char * asciiHexText;

        MALLOCARRAY(asciiHexText, totalTextSize * 2);

        if (!asciiHexText)
            pm_error("Unable to allocate memory for hex encoding of %u "
                     "characters of text", totalTextSize);

        asciiHexEncode(text, asciiHexText);
        *asciiHexTextP = asciiHexText;
    }
    pm_strfree(text);
}



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    unsigned int res;
    float        fontsize;
    const char * font;
    float        stroke;
    float        ascent;
    float        descent;
    float        leftmargin;
    float        rightmargin;
    float        topmargin;
    float        bottommargin;
    unsigned int pad;
    unsigned int verbose;
    unsigned int dump;
    const char * text;
};



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
    unsigned int cropSpec, ascentSpec, descentSpec;
    unsigned int leftmarginSpec, rightmarginSpec;
    unsigned int topmarginSpec, bottommarginSpec;

    MALLOCARRAY(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "resolution",    OPT_UINT,
            &cmdlineP->res,          NULL,                      0);
    OPTENT3(0, "font",          OPT_STRING,
            &cmdlineP->font,         NULL,                      0);
    OPTENT3(0, "fontsize",      OPT_FLOAT,
            &cmdlineP->fontsize,     NULL,                      0);
    OPTENT3(0, "stroke",        OPT_FLOAT,
            &cmdlineP->stroke,       NULL,                      0);
    OPTENT3(0, "ascent",        OPT_FLOAT,
            &cmdlineP->ascent,       &ascentSpec,               0);
    OPTENT3(0, "descent",       OPT_FLOAT,
            &cmdlineP->descent,      &descentSpec,              0);
    OPTENT3(0, "leftmargin",    OPT_FLOAT,
            &cmdlineP->leftmargin,   &leftmarginSpec,           0);
    OPTENT3(0, "rightmargin",   OPT_FLOAT,
            &cmdlineP->rightmargin,  &rightmarginSpec,          0);
    OPTENT3(0, "topmargin",     OPT_FLOAT,
            &cmdlineP->topmargin,    &topmarginSpec,            0);
    OPTENT3(0, "bottommargin",  OPT_FLOAT,
            &cmdlineP->bottommargin, &bottommarginSpec,         0);
    OPTENT3(0, "crop",          OPT_FLAG,
            NULL,                    &cropSpec,                 0);
    OPTENT3(0, "pad",           OPT_FLAG,
            NULL,                    &cmdlineP->pad,            0);
    OPTENT3(0, "verbose",       OPT_FLAG,
            NULL,                    &cmdlineP->verbose,        0);
    OPTENT3(0, "dump-ps",       OPT_FLAG,
            NULL,                    &cmdlineP->dump,           0);

    /* Set the defaults */
    cmdlineP->res = 150;
    cmdlineP->fontsize = 24;
    cmdlineP->font = "Times-Roman";
    cmdlineP->stroke  = -1;
    cmdlineP->ascent  = 0;
    cmdlineP->descent = 0;
    cmdlineP->rightmargin = 0;
    cmdlineP->leftmargin  = 0;
    cmdlineP->topmargin   = 0;
    cmdlineP->bottommargin = 0;
    cropSpec       = FALSE;
    cmdlineP->pad  = FALSE;

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);

    validateFontName(cmdlineP->font);

    if (cmdlineP->fontsize <= 0)
        pm_error("-fontsize must be positive");
    if (cmdlineP->ascent < 0)
        pm_error("-ascent must not be negative");
    if (cmdlineP->descent < 0)
        pm_error("-descent must not be negative");
    if (cmdlineP->leftmargin <0)
        pm_error("-leftmargin must not be negative");
    if (cmdlineP->rightmargin <0)
        pm_error("-rightmargin must not be negative");
    if (cmdlineP->topmargin <0)
        pm_error("-topmargin must not be negative");
    if (cmdlineP->bottommargin <0)
        pm_error("-bottommargin must not be negative");

    if (cropSpec == TRUE) {
        if (ascentSpec || descentSpec ||
            leftmarginSpec || rightmarginSpec ||
            topmarginSpec || bottommarginSpec || 
            cmdlineP->pad)
              pm_error("-crop cannot be specified with -ascent, -descent, "
                       "-leftmargin, -rightmargin, "
                       "-topmargin, -bottommargin or -pad");
    } else {
        if (!descentSpec && !bottommarginSpec && !cmdlineP->pad)
            cmdlineP->descent = cmdlineP->fontsize * 1.5;

        if (!leftmarginSpec)
             cmdlineP->leftmargin = cmdlineP->fontsize / 2;
    }

    buildTextFromArgs(argc, argv, &cmdlineP->text);

    free(option_def);
}



static void
termCmdline(struct CmdlineInfo const cmdline) {

    pm_strfree(cmdline.text);
}



static const char *
postscriptProgram(struct CmdlineInfo const cmdline) {
/*-----------------------------------------------------------------------------
  In Postscript, the bottom of the page is row zero.  Postscript allows
  negative values but negative regions are clipped from the output image.
  We make adjustments to ensure that nothing is lost.

  Postscript also allow fonts to have negative values in the bounding box
  coordinates.  The bottom edge of "L" is row zero: this row is called the
  "baseline".  The feet of "g" "p" "y" extend into negative region.  In a
  similar manner the left edge of the bounding box may be negative.  We add
  margins on the left and the bottom with "xorigin" and "yorigin" to
  provide for such characters.

  The sequence "textstring false charpath flattenpath pathbbox" determines
  the bounding box of the entire text when rendered.
-----------------------------------------------------------------------------*/

    /* C89 limits the size of a string constant, so we have to build the
       Postscript command in pieces.

       psVariable, psTemplate: Set variables.
       psFixed1: Scale font.  Calculate pad metrics.
       psFixed2: Determine width, height, xorigin, yorigin.
       psFixed3: Render.
       psFixed4: Verbose mode: Report font name, metrics.

       We could add code to psFixed2 for handling right-to-left writing
       (Hebrew, Arabic) and vertical writing (Chinese, Korean, Japanese).
    */

    const char * const psTemplate =
        "/FindFont {/%s findfont} def\n"
        "/fontsize %f def\n"
        "/pensize %f def\n"
        "/textstring <%s> def\n"
        "/ascent %f def\n"
        "/descent %f def\n"
        "/leftmargin %f def\n"
        "/rightmargin %f def\n"
        "/topmargin %f def\n"
        "/bottommargin %f def\n"
        "/pad %s def\n"
        "/verbose %s def\n";

    const char * const psFixed1 =
        "FindFont fontsize scalefont\n"
        "pad { dup dup\n"
        "  /FontMatrix get 3 get /yscale exch def\n"
        "  /FontBBox get dup\n"
        "  1 get yscale mul neg /padbottom exch def\n"
        "  3 get yscale mul /padtop exch def}\n"
        "  {/padbottom 0 def /padtop 0 def}\n"
        "  ifelse\n"
        "setfont\n";
    
    const char * const psFixed2 =
        "0 0 moveto\n"
        "textstring false charpath flattenpath pathbbox\n"
        "/BBtop    exch def\n"
        "/BBright  exch def\n"
        "/BBbottom exch neg def\n"
        "/BBleft   exch neg def\n"
        "/max { 2 copy lt { exch } if pop } bind def\n"
        "/yorigin descent padbottom max BBbottom max bottommargin add def\n"
        "/xorigin leftmargin BBleft max def\n"
        "/width xorigin BBright add rightmargin add def\n"
        "/height ascent BBtop max padtop max topmargin add yorigin add def\n";
    
    const char * const psFixed3 =
        "<</PageSize [width height]>> setpagedevice\n"
        "xorigin yorigin moveto\n"
        "pensize 0 lt\n"
        "  {textstring show}\n"
        "  {pensize setlinewidth 0 setgray\n"
        "  textstring true charpath stroke}\n"
        "  ifelse\n"
        "showpage\n";
    
    const char * const psFixed4 =
        "verbose\n"
        "  {xorigin yorigin moveto\n"
        "   [(width height) width height] ==\n"
        "   [(ascent descent) height yorigin sub yorigin] ==\n"
        "   [(bounding box) \n"
        "     textstring false charpath flattenpath pathbbox] ==\n"
        "   [(Fontname) FindFont dup /FontName\n"
        "     known\n"
        "       {/FontName get}\n"
        "       {pop (anonymous)}\n"
        "       ifelse]  ==}\n"
        "  if";
    
    const char * retval;
    const char * psVariable;

    pm_asprintf(&psVariable, psTemplate, cmdline.font,
                cmdline.fontsize, cmdline.stroke, cmdline.text,
                cmdline.ascent, cmdline.descent,
                cmdline.leftmargin, cmdline.rightmargin,
                cmdline.topmargin,  cmdline.bottommargin,
                cmdline.pad ? "true" : "false",
                cmdline.verbose ? "true" : "false" );

    pm_asprintf(&retval, "%s%s%s%s%s", psVariable,
                psFixed1, psFixed2, psFixed3, psFixed4);

    pm_strfree(psVariable);
        
    return retval;
}



static const char **
gsArgList(const char *       const outputFilename, 
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
    pm_asprintf(&retval[argCt++], "-");

    retval[argCt++] = NULL;

    assert(argCt < maxArgCt);

    return retval;
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
reportFontName(const char * const fontname) {

    pm_message("Font: '%s'", fontname);

}



static void
reportMetrics(float const  width,
              float const  height,
              float const  ascent,
              float const  descent,
              float const  BBoxleft,
              float const  BBoxbottom,
              float const  BBoxright,
              float const  BBoxtop) {

    pm_message("-- Metrics in points.  Bottom left is (0,0) --");  
    pm_message("Width:   %f", width);
    pm_message("Height:  %f", height);
    pm_message("Ascent:  %f", ascent);
    pm_message("Descent: %f", descent);
    pm_message("BoundingBox_Left:   %f", BBoxleft);
    pm_message("BoundingBox_Right:  %f", BBoxright);
    pm_message("BoundingBox_Top:    %f", BBoxtop);
    pm_message("BoundingBox_Bottom: %f", BBoxbottom);

}



static void
acceptGSoutput(int             const pipetosuckFd,
               void *          const nullParams ) {
/*-----------------------------------------------------------------------------
  Accept text written to stdout by the PostScript program.

  There are two kinds of output:
    (1) Metrics and fontname reported, when verbose is on.
    (2) Error messages from ghostscript.

  We read one line at a time.

  We cannot predict how long one line can be in case (2).  In practice
  the "execute stack" report gets long.  We provide by setting lineBuffSize
  to a large number.
-----------------------------------------------------------------------------*/
    unsigned int const lineBuffSize = 1024*32;
    FILE *       const inFileP = fdopen(pipetosuckFd, "r");

    float width, height, ascent, descent;
    float BBoxleft, BBoxbottom, BBoxright, BBoxtop;
    char * lineBuff;  /* malloc'd */
    char fontname [2048];
    bool fontnameReported, widthHeightReported;
    bool ascentDescentReported, BBoxReported;

    assert(nullParams == NULL);

    fontnameReported      = FALSE; /* Initial value */
    widthHeightReported   = FALSE; /* Initial value */
    ascentDescentReported = FALSE; /* Initial value */
    BBoxReported          = FALSE; /* Initial value */
    
    MALLOCARRAY_NOFAIL(lineBuff, lineBuffSize);

    while (fgets(lineBuff, lineBuffSize, inFileP) != NULL) {
        unsigned int rWidthHeight, rAscentDescent, rBBox, rFontname;

        rWidthHeight = sscanf(lineBuff, "[(width height) %f %f]",
                              &width, &height);

        rAscentDescent = sscanf(lineBuff, "[(ascent descent) %f %f]",
                                &ascent, &descent);

        rBBox =  sscanf(lineBuff, "[(bounding box) %f %f %f %f]",
                        &BBoxleft, &BBoxbottom, &BBoxright, &BBoxtop);

        rFontname = sscanf(lineBuff, "[(Fontname) /%2047s", fontname);

        if (rFontname == 1)
            fontnameReported = TRUE;
        else if (rWidthHeight == 2)
            widthHeightReported = TRUE;
        else if (rAscentDescent == 2)
            ascentDescentReported = TRUE;
        else if (rBBox == 4)
            BBoxReported = TRUE;
        else
            pm_message("[gs] %s", lineBuff);
    }

    if (fontnameReported) {
        fontname[strlen(fontname)-1] = 0; 
        reportFontName(fontname);

        if (widthHeightReported && ascentDescentReported && BBoxReported)
            reportMetrics(width, height, ascent, descent,
                          BBoxleft, BBoxbottom, BBoxright, BBoxtop);
    }
    fclose(inFileP);
    pm_strfree(lineBuff);
}



static void
executeProgram(const char *       const psProgram, 
               const char *       const outputFname,
               struct CmdlineInfo const cmdline) {

    const char *  const executableNm = "gs";
    const char ** const argList = gsArgList(outputFname, cmdline);

    struct bufferDesc feedBuffer;
    int               termStatus;
    unsigned int      bytesFed;

    bytesFed = 0;  /* Initial value */

    feedBuffer.buffer            = (unsigned char *) psProgram;
    feedBuffer.size              = strlen(psProgram);
    feedBuffer.bytesTransferredP = &bytesFed;

    if (cmdline.verbose)
        reportGhostScript(executableNm, argList);

    pm_system2_vp(executableNm,
                  argList,
                  &pm_feed_from_memory, &feedBuffer,
                  cmdline.verbose ? &acceptGSoutput : &pm_accept_null, NULL,
                  &termStatus);

    if (termStatus != 0) {
        const char * const msg = pm_termStatusDesc(termStatus);

        pm_error("Failed to run Ghostscript process.  %s", msg);

        pm_strfree(msg);
    }
    freeArgList(argList);
}



static void
writePbm(const char * const fileName,
         FILE *       const ofP) {
/*----------------------------------------------------------------------------
  Write the PBM image that is in the file named 'fileName" to file *ofP.
  I.e. pbmtopbm.

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
               
    pbm_writepbminit(ofP, cols, rows, 0);           
               
    bitrow = pbm_allocrow_packed(cols);
    
    for (row = 0; row < rows; ++row) {
        pbm_readpbmrow_packed(ifP, bitrow, cols, format);
        pbm_writepbmrow_packed(ofP, bitrow, cols, 0);
    }
    pbm_freerow_packed(bitrow);
    pm_close(ifP);
}



static void
generatePbm(struct CmdlineInfo const cmdline,
            FILE *             const ofP) {

    const char * const psProgram = postscriptProgram(cmdline);

    const char * tempPbmFname;
    FILE * pbmFileP;

    pm_make_tmpfile(&pbmFileP, &tempPbmFname);
    assert(pbmFileP != NULL && tempPbmFname != NULL);
    fclose(pbmFileP);

    executeProgram(psProgram, tempPbmFname, cmdline);

    /* Although Ghostscript created a legal PBM file, it uses a different
       implementation of the format from libnetpbm's canonical output format,
       so instead of copying the content of 'tempPbmFname' to *ofP byte for
       byte, we copy it as a PBM image.
    */
    writePbm(tempPbmFname, ofP);

    unlink(tempPbmFname);
    pm_strfree(tempPbmFname);
    pm_strfree(psProgram);
}



static void
dumpPsProgram(struct CmdlineInfo const cmdline) {

    const char * psProgram;

    psProgram = postscriptProgram(cmdline);

    puts(psProgram);

    pm_strfree(psProgram);
}



int 
main(int argc, const char *argv[]) {

    struct CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    if (cmdline.dump)
        dumpPsProgram(cmdline);
    else
        generatePbm(cmdline, stdout);

    termCmdline(cmdline);

    return 0;
}



