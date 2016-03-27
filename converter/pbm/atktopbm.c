/* atktopbm.c - convert Andrew Toolkit raster object to portable bitmap
**
** Copyright (C) 1991 by Bill Janssen
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "nstring.h"
#include "pbm.h"
#include "mallocvar.h"


/* readatkraster
**
** Routine for reading rasters in .raster form.  (BE2 rasters version 2.)
*/

/* codes for data stream */
#define WHITEZERO   'f'
#define WHITETWENTY 'z'
#define BLACKZERO   'F'
#define BLACKTWENTY 'Z'
#define OTHERZERO   0x1F

#define WHITEBYTE   0x00
#define BLACKBYTE   0xFF

/* error codes (copied from $ANDREW/atk/basics/common/dataobj.ch) */
/* return values from Read */
#define dataobject_NOREADERROR  0
#define dataobject_PREMATUREEOF 1
#define dataobject_NOTBE2DATASTREAM 2 /* backward compatibility */
#define dataobject_NOTATKDATASTREAM 2 /* preferred version */
#define dataobject_MISSINGENDDATAMARKER 3
#define dataobject_OBJECTCREATIONFAILED 4
#define dataobject_BADFORMAT 5

/* ReadRow(file, row, length) 
** Reads from 'file' the encoding of bytes to fill in 'row'.  Row will be
** truncated or padded (with WHITE) to exactly 'length' bytes.
**
** Returns the code that terminated the row.  This may be
**      '|'     correct end of line
**      '\0'    if the length was satisfied (before a terminator)
**      EOF     if the file ended
**      '\'  '{'    other recognized ends. 
** The '|' is the expected end and pads the row with WHITE.
** The '\' and '{' are error conditions and may indicate the
** beginning of some other portion of the data stream.
** If the terminator is '\' or '{', it is left at the front of the input.
** '|' is gobbled up.
*/

/* macros to generate case entries for switch statement */
#define CASE1(v) case v
#define CASE4(v) case v: case (v)+1: case (v)+2: case(v)+3
#define CASE6(v) CASE4(v): case ((v)+4): case ((v)+5)
#define CASE8(v) CASE4(v): CASE4((v)+4)



static long
ReadRow(FILE *          const file,
        unsigned char * const row,
        long            const length) {
/*----------------------------------------------------------------------------
  'file' is where to get them from.
  'row' is where to put bytes.
  'length' is how many bytes in row must be filled.
  
  Return the delimiter that marks the end of the row, or EOF if EOF marks
  the end of the row, or NUL in some cases.
-----------------------------------------------------------------------------*/
    /* Each input character is processed by the central loop.  There are 
    ** some input codes which require two or three characters for
    ** completion; these are handled by advancing the state machine.
    ** Errors are not processed; instead the state machine is reset
    ** to the Ready state whenever a character unacceptable to the
    ** current state is read.
    */
    enum StateCode {
        Ready,
            /* any input code is allowed */
        HexDigitPending,
            /* have seen the first of a hex digit pair */
        RepeatPending,
            /* repeat code has been seen: must be followed by two hex digits
             */
        RepeatAndDigit
            /* have seen repeat code and its first following digit */
    };
    
    enum StateCode InputState;  /* current state */
    int c;     /* the current input character */
    long repeatcount;  /* current repeat value */
    long hexval;   /* current hex value */
    long pendinghex;    /* the first of a pair of hex characters */
    int lengthRemaining;
    unsigned char * cursor;
    
    /* We cannot exit when length becomes zero because we need to check 
    ** to see if a row ending character follows.  Thus length is checked
    ** only when we get a data generating byte.  If length then is
    ** zero, we ungetc the byte.
    */

    repeatcount = 0;  /* initial value */
    pendinghex = 0;  /* initial value */

    lengthRemaining = length;  /* initial value */
    cursor = row;  /* initial value */
    InputState = Ready;  /* initial value */

    while ((c=getc(file)) != EOF) switch (c) {

        CASE8(0x0):
        CASE8(0x8):
        CASE8(0x10):
        CASE8(0x18):
        CASE1(' '):
            /* control characters and space are legal and ignored */
            break;
        CASE1(0x40):    /* '@' */
        CASE1(0x5B):    /* '[' */
        CASE4(0x5D):    /*  ']'  '^'  '_'  '`' */
        CASE4(0x7D):    /* '}'  '~'  DEL  0x80 */
        default:        /* all above 0x80 */
            /* error code:  Ignored at present.  Reset InputState. */
            InputState = Ready;
            break;

        CASE1(0x7B):    /* '{' */
        CASE1(0x5C):    /* '\\' */
            /* illegal end of line:  exit anyway */
            ungetc(c, file);    /* retain terminator in stream */
            /* DROP THROUGH */
        CASE1(0x7C):    /* '|' */
            /* legal end of row: may have to pad  */
            while (lengthRemaining-- > 0)
                *cursor++ = WHITEBYTE;
            return c;
    
        CASE1(0x21):
        CASE6(0x22):
        CASE8(0x28):
            /* punctuation characters: repeat byte given by two
            ** succeeding hex chars
            */
            if (lengthRemaining <= 0) {
                ungetc(c, file);
                return('\0');
            }
            repeatcount = c - OTHERZERO;
            InputState = RepeatPending;
            break;

        CASE8(0x30):
        CASE8(0x38):
            /* digit (or following punctuation)  -  hex digit */
            hexval = c - 0x30;
            goto hexdigit;
        CASE6(0x41):
            /* A ... F    -  hex digit */
            hexval = c - (0x41 - 0xA);
            goto hexdigit;
        CASE6(0x61):
            /* a ... f  - hex digit */
            hexval = c - (0x61 - 0xA);
            goto hexdigit;

        CASE8(0x67):
        CASE8(0x6F):
        CASE4(0x77):
            /* g ... z   -   multiple WHITE bytes */
            if (lengthRemaining <= 0) {
                ungetc(c, file);
                return('\0');
            }
            repeatcount = c - WHITEZERO;
            hexval = WHITEBYTE;
            goto store;
        CASE8(0x47):
        CASE8(0x4F):
        CASE4(0x57):
            /* G ... Z   -   multiple BLACK bytes */
            if (lengthRemaining <= 0) {
                ungetc(c, file);
                return('\0');
            }
            repeatcount = c - BLACKZERO;
            hexval = BLACKBYTE;
            goto store;

        hexdigit:
            /* process a hex digit.  Use InputState to determine
               what to do with it. */
            if (lengthRemaining <= 0) {
                ungetc(c, file);
                return('\0');
            }
            switch(InputState) {
            case Ready:
                InputState = HexDigitPending;
                pendinghex = hexval << 4;
                break;
            case HexDigitPending:
                hexval |= pendinghex;
                repeatcount = 1;
                goto store;
            case RepeatPending:
                InputState = RepeatAndDigit;
                pendinghex = hexval << 4;
                break;
            case RepeatAndDigit:
                hexval |= pendinghex;
                goto store;
            }
            break;

        store:
            /* generate byte(s) into the output row 
               Use repeatcount, depending on state.  */
            if (lengthRemaining < repeatcount) 
                /* reduce repeat count if it would exceed
                   available space */
                repeatcount = lengthRemaining;
            lengthRemaining -= repeatcount;  /* do this before repeatcount-- */
            while (repeatcount-- > 0)
                *cursor++ = hexval;
            InputState = Ready;
            break;

        } /* end of while( - )switch( - ) */
    return EOF;
}



#undef CASE1
#undef CASE4
#undef CASE6
#undef CASE8



static void
ReadATKRaster(FILE * const ifP) {

    int row;  /* count rows;  byte length of row */
    int version;
    char keyword[6];
    int discardid;
    int objectid;     /* id read for the incoming pixel image */
    long tc;            /* temp */
    int width, height;      /* dimensions of image */
    bit * bitrow;

    if (fscanf(ifP, "\\begindata{raster,%d", &discardid) != 1
        || getc(ifP) != '}' || getc(ifP) != '\n')
        pm_error ("input file not Andrew raster object");

    fscanf(ifP, " %d ", &version);
    if (version < 2) 
        pm_error ("version too old to parse");

    {
        unsigned int options;
        long xscale, yscale;
        long xoffset, yoffset, subwidth, subheight;
        /* ignore all these features: */
        fscanf(ifP, " %u %ld %ld %ld %ld %ld %ld",  
               &options, &xscale, &yscale, &xoffset, 
               &yoffset, &subwidth, &subheight);
    }
    /* scan to end of line in case this is actually something beyond V2 */
    while (((tc=getc(ifP)) != '\n') && (tc != '\\') && (tc != EOF)) {}

    /* read the keyword */
    fscanf(ifP, " %5s", keyword);
    if (!streq(keyword, "bits"))
        pm_error ("keyword is not 'bits'!");

    fscanf(ifP, " %d %d %d ", &objectid, &width, &height);

    if (width < 1 || height < 1 || width > 1000000 || height > 1000000) 
        pm_error("bad width or height");

    pbm_writepbminit(stdout, width, height, 0);
    bitrow = pbm_allocrow_packed(width);

    for (row = 0;   row < height; ++row) {
        unsigned int const rowlen = (width + 7) / 8;
        long const nextChar = ReadRow(ifP, bitrow, rowlen);

        switch (nextChar) {
        case '|': 
            pbm_writepbmrow_packed(stdout, bitrow, width, 0);
            break;
        case EOF:
            pm_error("premature EOF");
            break;
        default:
            pm_error("bad format");
        }
    }

    pbm_freerow_packed(bitrow);

    while (! feof(ifP) && getc(ifP) != '\\') {};  /* scan for \enddata */

    if (fscanf(ifP, "enddata{raster,%d", &discardid) != 1
        || getc(ifP) != '}' || getc(ifP) != '\n')
        pm_error("missing end-of-object marker");
}



int
main(int argc, const char ** argv) {

    FILE * ifP;

    pm_proginit(&argc, argv);

    if (argc-1 < 1)
        ifP = stdin;
    else {
        ifP = pm_openr(argv[1]);

        if (argc-1 > 1)
            pm_error("Too many arguments.  The only possible argument is "
                     "the input file name");
    }

    ReadATKRaster(ifP);

    pm_close(ifP);

    pm_close(stdout);

    return 0;
}
