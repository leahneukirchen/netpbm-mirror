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

#define _XOPEN_SOURCE 500
  /* Make sure popen() is in stdio.h, strdup() is in string.h */

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

enum InputFmt {INPUT_LITERAL, INPUT_ASCIIHEX, INPUT_ASCII85};

static void
validateFontName(const char * const name) {
/*-----------------------------------------------------------------------------
  Validate font name string.

  Abort with error message if it contains anything other than the printable
  characters in the ASCII 7-bit range, or any character with a special meaning
  in PostScript.
-----------------------------------------------------------------------------*/
    unsigned int idx;

    if (name[0] == '\0')
        pm_error("Font name is empty string");

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
asciiHexEncode(const char * const inbuff,
               char *       const outbuff) {
/*-----------------------------------------------------------------------------
  Convert the input text string to ASCII-Hex encoding.

  Examples: "ABC abc 123" -> <4142432061626320313233>
            "FOO(BAR)FOO" -> <464f4f2842415229464f4f>
-----------------------------------------------------------------------------*/
    char const hexits[16] = "0123456789abcdef";

    unsigned int idx;

    outbuff[0] = '<';

    for (idx = 0; inbuff[idx] != '\0'; ++idx) {
        unsigned int const item = (unsigned char) inbuff[idx];

        outbuff[1 + idx * 2 + 0] = hexits[item >> 4];
        outbuff[1 + idx * 2 + 1] = hexits[item & 0xF];
    }

    if (idx == 0)
        pm_message("Empty input string");

    outbuff[1 + idx * 2] = '>';
    outbuff[1 + idx * 2 + 1] = '\0';
}



static void
failForInvalidChar(char         const c,
                   const char * const type) {

    if (c >= 0x32 || c < 0x7F)
        pm_error("Invalid character '%c' in '%s' input string",   c, type);
    else
        pm_error("Invalid character 0x%02x in '%s' input string", c, type);
}



static void
formatNonemptyAsciiHex(const char * const inbuff,
                       char       * const outbuff,
                       unsigned int const inLen) {

    unsigned int inIdx, outIdx;
        /* Cursors in input and output buffers, respectively */
    unsigned int validCharCt;
        /* Number of valid hex characters we've processed so far in input */
    unsigned int startIdx, endIdx;
        /* Limits of input buffer cursor ('inIdx') */

     if (inbuff[0] == '<') {
         startIdx = 1;
         endIdx = inLen - 2;
     } else {
         startIdx = 0;
         endIdx = inLen - 1;
     }

     validCharCt = 0;  /* No valid characters seen yet */

     outIdx = 0;  /* Start at beginning of output buffer */

     outbuff[outIdx++] = '<';

     for (inIdx = startIdx; inIdx <= endIdx; ++inIdx) {
         switch (inbuff[inIdx]) {
         case '<':
         case '>':
             pm_error("Misplaced character '%c' in Ascii Hex input string",
                      inbuff[inIdx]);
           break;
         case '\f':
         case '\n':
         case '\r':
         case ' ':
         case '\t':
             /* ignore whitespace chars */
             break;
         default:
             if ((inbuff [inIdx] >='0' && inbuff [inIdx] <='9') ||
                 (inbuff [inIdx] >='A' && inbuff [inIdx] <='F') ||
                 (inbuff [inIdx] >='a' && inbuff [inIdx] <='f')) {

                 outbuff[outIdx++] = inbuff [inIdx];
                 ++validCharCt;
             } else
                 failForInvalidChar(inbuff[inIdx], "Ascii Hex");
             break;
         }
     }

     if (validCharCt == 0)
         pm_message("Empty Ascii Hex input string");
     else if (validCharCt % 2 != 0)
         pm_error("Number of characters in Ascii Hex input string "
                  "is not even");

     outbuff[outIdx++] = '>';
     outbuff[outIdx++] = '\0';
}



static void
formatAsciiHexString(const char * const inbuff,
                     char       * const outbuff,
                     unsigned int const inLen) {
/*----------------------------------------------------------------------------
  Format the ASCII Hex input 'inbuff' as a Postscript ASCII Hex string,
  e.g. "<313233>".  Input can be just the ASCII Hex (e.g. "313233") or already
  formatted (e.g. "<313233>").  Input may also contain white space, which we
  ignore -- our output never contains white space.  Though in Postscript, an
  ASCII NUL character counts as white space, we consider it the end of the
  input.

  We consider white space outside of the <> delimiters to be an error.

  Abort with error message if there is anything other than valid hex digits in
  the ASCII hex string proper.  This is necessary to prevent code injection.
----------------------------------------------------------------------------*/
    if (inLen == 0 ||
        (inLen == 2 && inbuff[0] == '<' && inbuff[inLen-1] == '>' )) {
        pm_message("Empty Ascii Hex input string");
        strncpy(outbuff, "<>", 3);
    } else {
        if (inbuff[0] == '<' && inbuff[inLen-1] != '>' )
            pm_error("Ascii Hex input string starts with '<' "
                     "but does not end with '>'");
        else if (inbuff[0] != '<' && inbuff[inLen-1] == '>' )
            pm_error("Ascii Hex input string ends with '>' "
                     "but does not start with '<'");

        formatNonemptyAsciiHex(inbuff, outbuff, inLen);
    }
}



static void
formatNonemptyAscii85(const char * const inbuff,
                      char       * const outbuff,
                      unsigned int const inLen) {

    unsigned int inIdx, outIdx;
        /* Cursors in input and output buffers, respectively */
    unsigned int seqPos;
        /* Position in 5-character Ascii-85 sequence where 'inIdx' points */
    unsigned int startIdx, endIdx;
        /* Limits of input buffer cursor ('inIdx') */

    if (inLen > 4 && inbuff[0] == '<' && inbuff[1] == '~' &&
        inbuff[inLen-2] == '~' && inbuff[inLen-1] == '>') {
        startIdx = 2;
        endIdx = inLen - 3;
    } else {
        startIdx = 0;
        endIdx = inLen - 1;
    }

    seqPos = 0;  /* No 5-character Ascii-85 sequence encountered yet */
    outIdx = 0;  /* Start filling output buffer from beginning */

    outbuff[outIdx++] = '<';
    outbuff[outIdx++] = '~';

    for (inIdx = startIdx; inIdx <= endIdx; ++inIdx) {
      switch (inbuff[inIdx]) {
        case '<':
      case '~':
      case '>':
          pm_error("Misplaced character '%c' in Ascii 85 input string",
                   inbuff[inIdx]);
          break;
      case '\f':
      case '\n':
      case '\r':
      case ' ':
      case '\t':
          break;
      case 'z':
          /* z extension */
          if (seqPos > 0)
              pm_error("Special 'z' character appears in the middle of a "
                       "5-character Ascii-85 sequence, which is invalid");
          else
              outbuff[outIdx++] = inbuff[inIdx];
          break;
        default:
          /* valid Ascii 85 char */
          if (inbuff [inIdx] >='!' && inbuff [inIdx] <='u') {
              outbuff[outIdx++] = inbuff[inIdx];
              seqPos = (seqPos + 1) % 5;
          } else
              failForInvalidChar(inbuff[inIdx], "Ascii 85");
          break;
      }
    }

    if (outIdx == 2) {
        pm_message("Empty Ascii 85 input string");
    }

    outbuff[outIdx++]   = '~';
    outbuff[outIdx++] = '>';
    outbuff[outIdx++] = '\0';
}



static void
formatAscii85String(const char * const inbuff,
                    char       * const outbuff,
                    unsigned int const inLen) {
/*----------------------------------------------------------------------------
  Format the Ascii-85 input 'inbuff' as a Postscript Ascii-85 string,
  e.g. "<~313233~>".  Input can be just the Ascii-85 (e.g. "313233") or
  already formatted (e.g. "<~313233~>").  Input may also contain white space,
  which we ignore -- our output never contains white space.  Though in
  Postscript, an ASCII NUL character counts as white space, we consider it the
  end of the input.

  We consider white space outside of the <~~> delimiters to be an error.

  Abort with error message if we encounter anything other than valid Ascii-85
  encoding characters in the string proper.  Note that the Adobe variant
  does not support the "y" extention.
----------------------------------------------------------------------------*/
    if (inLen == 0 || (inLen == 4 && strncmp (inbuff, "<~~>", 4) == 0)) {
        pm_message("Empty Ascii 85 input string");
        strncpy(outbuff,"<~~>", 5);
    } else {
        if (inLen >= 2) {
            if (inbuff[0] == '<' && inbuff[1] == '~' &&
                (inLen < 4 || inbuff[inLen-2] != '~'
                 || inbuff[inLen-1] != '>' ))
                pm_error("Ascii 85 input string starts with '<~' "
                         "but does not end with '~>'");
            else if (inbuff[inLen-2] == '~' && inbuff[inLen-1] == '>' &&
                     (inLen < 4 || inbuff[0] != '<' || inbuff[1] != '~'))
                pm_error("Ascii 85 input string ends with '~>' "
                         "but does not start with '<~'");
        }
        formatNonemptyAscii85(inbuff, outbuff, inLen);
    }
}



static void
combineArgs(int            const argc,
            const char **  const argv,
            const char **  const textP,
            unsigned int * const textSizeP) {

    unsigned int totalTextSize;
    char * text;
    size_t * argSize;
    unsigned int i;
    size_t idx;

    MALLOCARRAY_NOFAIL(argSize, argc);
        /* argv[0] not accessed; argSize[0] not used */

    for (i = 1, totalTextSize = 0; i < argc; ++i) {
        argSize[i] = strlen(argv[i]);
        totalTextSize += argSize[i];
    }

    totalTextSize = totalTextSize + (argc - 2);  /* adjust for spaces */

    MALLOCARRAY(text, totalTextSize + 1); /* add one for \0 at end */
    if (text == NULL)
        pm_error("out of memory allocating buffer for "
                 "%u characters of text", totalTextSize);

    strncpy(text, argv[1], argSize[1]);
    for (i = 2, idx = argSize[1]; i < argc; ++i) {
        text[idx++] = ' ';
        strncpy(&text[idx], argv[i], argSize[i]);
        idx += argSize[i];
    }

    assert(idx == totalTextSize);

    text[idx++] = '\0';

    assert(strlen(text) == totalTextSize);

    *textP     = text;
    *textSizeP = totalTextSize;

    free(argSize);
}



static void
buildTextFromArgs(int           const argc,
                  const char ** const argv,
                  const char ** const inputTextP,
                  enum InputFmt const inputFmt) {
/*----------------------------------------------------------------------------
   Build the string of text to be included in the Postscript program to be
   rendered, from the arguments of this program.

   We encode it in either ASCII-Hex or ASCII-85 as opposed to using the plain
   text from the command line because 1) the command line might have
   Postscript control characters in it; and 2) the command line might have
   text in 8-bit or multibyte code, but a Postscript program is supposed to be
   entirely printable ASCII characters.
-----------------------------------------------------------------------------*/
    const char * text;
        /* All the arguments ('argv') concatenated */
    unsigned int textSize;
        /* Length of 'text' */

    if (argc-1 < 1)
        pm_error("No text");

    combineArgs(argc, argv, &text, &textSize);

    switch (inputFmt) {
    case INPUT_LITERAL: {
        char * asciiHexText;

        MALLOCARRAY(asciiHexText, textSize * 2 + 3);
        if (!asciiHexText)
            pm_error("Unable to allocate memory for hex encoding of %u "
                     "characters of text", textSize);

        asciiHexEncode(text, asciiHexText);
        *inputTextP = asciiHexText;
    } break;
    case INPUT_ASCIIHEX: {
        char * asciiHexText;

        MALLOCARRAY(asciiHexText, textSize + 3);
        if (!asciiHexText)
            pm_error("Unable to allocate memory for hex encoding of %u "
                     "characters of text", textSize);

        formatAsciiHexString(text, asciiHexText, textSize);
        *inputTextP = asciiHexText;
    } break;
    case INPUT_ASCII85: {
        char * ascii85Text;

        MALLOCARRAY(ascii85Text, textSize + 5);
        if (!ascii85Text)
            pm_error("Unable to allocate memory for hex encoding of %u "
                     "characters of text", textSize);

        formatAscii85String(text, ascii85Text, textSize);
        *inputTextP = ascii85Text;
    } break;
    }

    pm_strfree(text);
}



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    unsigned int  res;
    float         fontsize;
    const char *  font;
    float         stroke;
    float         ascent;
    float         descent;
    float         leftmargin;
    float         rightmargin;
    float         topmargin;
    float         bottommargin;
    unsigned int  pad;
    unsigned int  verbose;
    unsigned int  dump;
    const char *  text;
        /* Text to render, in Postscript format, either Ascii-hex
           (e.g. <313233>) or Ascii-85 (e.g. <~aBc-~>)
        */
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
    unsigned int asciihexSpec, ascii85Spec;

    MALLOCARRAY_NOFAIL(option_def, 100);

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
    OPTENT3(0, "asciihex",      OPT_FLAG,
            NULL,                    &asciihexSpec,             0);
    OPTENT3(0, "ascii85",       OPT_FLAG,
            NULL,                    &ascii85Spec,              0);
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

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);

    validateFontName(cmdlineP->font);

    if (cmdlineP->res <= 0)
        pm_error("-resolution must be positive");
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

    if (cropSpec) {
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

    {
        enum InputFmt inputFmt;

        if (asciihexSpec) {
            if (ascii85Spec)
                pm_error("You cannot specify both -asciihex and -ascii85");
            else
                inputFmt = INPUT_ASCIIHEX;
        } else if (ascii85Spec)
            inputFmt = INPUT_ASCII85;
        else
            inputFmt = INPUT_LITERAL;

        if (argc-1 < 1)
            pm_error("No text");

        buildTextFromArgs(argc, argv, &cmdlineP->text, inputFmt);
    }
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
        "/textstring %s def\n"
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

    assert(cmdline.res > 0);
    assert(cmdline.fontsize > 0);

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



