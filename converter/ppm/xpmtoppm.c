/* xpmtoppm.c - read an X11 pixmap file and produce a portable pixmap
**
** Copyright (C) 1991 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
** Upgraded to handle XPM version 3 by
**   Arnaud Le Hors (lehors@mirsa.inria.fr)
**   Tue Apr 9 1991
**
** Rainer Sinkwitz sinkwitz@ifi.unizh.ch - 21 Nov 91:
**  - Bug fix, no advance of read ptr, would not read 
**    colors like "ac c black" because it would find 
**    the "c" of "ac" and then had problems with "c"
**    as color.
**    
**  - Now understands multiword X11 color names
**  
**  - Now reads multiple color keys. Takes the color
**    of the hightest available key. Lines no longer need
**    to begin with key 'c'.
**    
**  - expanded line buffer to from 500 to 2048 for bigger files
*/

#define _BSD_SOURCE   /* Make sure strdup() is in string.h */
#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */

#include <assert.h>
#include <string.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "nstring.h"
#include "ppm.h"

#define MAX_LINE (8 * 1024)
  /* The maximum size XPM input line we can handle. */

/* number of xpmColorKeys */
#define NKEYS 5

const char *xpmColorKeys[] =
{
 "s",					/* key #1: symbol */
 "m",					/* key #2: mono visual */
 "g4",					/* key #3: 4 grays visual */
 "g",					/* key #4: gray visual */
 "c",					/* key #5: color visual */
};

struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * input_filespec;  /* Filespecs of input files */
    const char * alpha_filename;
    int alpha_stdout;
    int verbose;
};


static bool verbose;


typedef struct {
    bool none;
        /* No color is transparent */
    unsigned int index;
        /* Color index of the transparent color.
           Meaningless if 'none'
        */
} TransparentColor;



static void
parseCommandLine(int argc, char ** argv,
                 struct cmdlineInfo *cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions2 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "alphaout",   OPT_STRING, &cmdlineP->alpha_filename,
            NULL, 0);
    OPTENT3(0,   "verbose",    OPT_FLAG,   &cmdlineP->verbose,
            NULL, 0);

    cmdlineP->alpha_filename = NULL;
    cmdlineP->verbose = FALSE;

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = TRUE;  /* We may have parms that are negative numbers */

    pm_optParseOptions3(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc - 1 == 0)
        cmdlineP->input_filespec = NULL;  /* he wants stdin */
    else if (argc - 1 == 1)
        cmdlineP->input_filespec = strdup(argv[1]);
    else 
        pm_error("Too many arguments.  The only argument accepted\n"
                 "is the input file specification");

    if (cmdlineP->alpha_filename && 
        streq(cmdlineP->alpha_filename, "-"))
        cmdlineP->alpha_stdout = TRUE;
    else 
        cmdlineP->alpha_stdout = FALSE;

}


static char lastInputLine[MAX_LINE+1];
    /* contents of line most recently read from input */
static bool backup;
    /* TRUE means next read should be a reread of the most recently read
       line, i.e. lastInputLine, instead of a read from the input file.
    */


static void
getLine(char * const line,
        size_t const size,
        FILE * const stream) {
/*----------------------------------------------------------------------------
   Read the next line from the input file 'stream', through the one-line
   buffer lastInputLine[].

   If 'backup' is true, the "next line" is the previously read line, i.e.
   the one in that one-line buffer.  Otherwise, the "next line" is the next
   line from the real file.  After reading the backed up line, we reset 
   'backup' to false.

   Return the line as a null terminated string in *line, which is an
   array of 'size' bytes.

   Exit program if the line doesn't fit in the buffer.
-----------------------------------------------------------------------------*/
    if (size > sizeof(lastInputLine))
        pm_error("INTERNAL ERROR: getLine() received 'size' parameter "
                 "which is out of bounds");

    if (backup) {
        strncpy(line, lastInputLine, size); 
        backup = FALSE;
    } else {
        if (fgets(line, size, stream) == NULL)
            pm_error("EOF or read error on input file");
        if (strlen(line) == size - 1)
            pm_error("Input file has line that is too long (longer than "
                     "%u bytes).", (unsigned)size - 1);
        STRSCPY(lastInputLine, line);
    }
}



static void
getColorNumber(const char *   const pArg,
               unsigned int   const bytesPerPixel,
               unsigned int   const nColors,
               unsigned int * const colorNumberP,
               unsigned int * const bytesReadP) {
/*----------------------------------------------------------------------------
   Return the color number at 'pArg'.

   It occupies 'bytesPerPixel' bytes.

   Note that the color number is not an ordinary palette index.  It is a
   number we make up to represent the 0-3 bytes of text that represent a color
   in the XPM image.  In particular, we take the bytes to be a big-endian pure
   binary number code.  Note that this number is typically larger than the
   number of colors in the palette and sometimes too large to be used as an
   array index at all.

   Abort program if the number is too large for the format described
   by 'bytesPerPixel' and 'nColors'.
-----------------------------------------------------------------------------*/
    const unsigned char * const p = (const unsigned char *)pArg;

    unsigned int accum;
    const unsigned char * q;

    assert(bytesPerPixel <= sizeof(unsigned int));
    
    for (q = p, accum = 0; q < p + bytesPerPixel && *q && *q != '"'; ++q) {
        accum <<= 8;
        accum += *q;
    }

    if (bytesPerPixel <= 2 && accum >= nColors)
        pm_error("Color number %u in color map is too large, as the "
                 "header says there are only %u colors in the image",
                 accum, nColors);

    *colorNumberP = accum;
    *bytesReadP   = q - p;
}



static void
getword(char * const output, char ** const cursorP) {

    char *t1;
    char *t2;

    for (t1=*cursorP; ISSPACE(*t1); t1++); /* skip white space */
    for (t2 = t1; !ISSPACE(*t2) && *t2 != '"' && *t2 != '\0'; t2++);
        /* Move to next white space, ", or eol */
    if (t2 > t1)
        strncpy(output, t1, t2 - t1);
    output[t2 - t1] = '\0';
    *cursorP = t2;
}    



static void
addToColorMap(unsigned int       const seqNum,
              unsigned int       const colorNumber, 
              pixel *            const colors,
              unsigned int *     const ptab, 
              char               const colorspec[],
              bool               const isTransparent,
              TransparentColor * const transparentP) {
/*----------------------------------------------------------------------------
   Add the color named by colorspec[] to the colormap contained in
   'colors' and 'ptab', as the color associated with XPM color number
   'colorNumber', which is the seqNum'th color in the XPM color map.

   Iff 'isTransparent', set *transparentP to the colormap index that 
   corresponds to this color.
-----------------------------------------------------------------------------*/
    if (ptab == NULL) {
        /* Index into table. */
        colors[colorNumber] = ppm_parsecolor(colorspec,
                                             (pixval) PPM_MAXMAXVAL);
        if (isTransparent) {
            transparentP->none = false;
            transparentP->index = colorNumber;
        }
    } else {
        /* Set up linear search table. */
        colors[seqNum] = ppm_parsecolor(colorspec,
                                        (pixval) PPM_MAXMAXVAL);
        ptab[seqNum] = colorNumber;
        if (isTransparent) {
            transparentP->none = false;
            transparentP->index = seqNum;
        }
    }
}



static void
interpretXpm3ColorTableLine(char               const line[],
                            unsigned int       const seqNum, 
                            unsigned int       const charsPerPixel,
                            pixel *            const colors,
                            unsigned int *     const ptab,
                            unsigned int       const nColors,
                            TransparentColor * const transparentP) {
/*----------------------------------------------------------------------------
   Interpret one line of the color table in the XPM header.  'line' is the
   line from the XPM file.  It is the seqNum'th color table entry in the file.
   The raster in the file uses 'charsPerPixel' characters per pixel (i.e.
   a color number (palette index) is 'charsPerPixel' characters).

   Add the information from this color table entry to the color table 'colors'
   and, if it isn't NULL, the corresponding lookup shadow table 'ptab'.  Both
   are of size 'nColors'.  (See readV3ColorTable for a description of these
   data structures).

   The line may include values for multiple kinds of color (grayscale,
   color, etc.).  We take the highest of these (e.g. color over grayscale).

   If a color table entry indicates transparency, set *transparentP
   to the colormap index that corresponds to the indicated color.
-----------------------------------------------------------------------------*/
    /* Note: this code seems to allow for multi-word color specifications,
       but I'm not aware that such are legal.  Ultimately, ppm_parsecolor()
       interprets the name, and I believe it takes only single word 
       color specifications.  -Bryan 2001.05.06.
    */
    char str2[MAX_LINE+1];    
    char * t1;
    char * t2;
    int endOfEntry;   /* boolean */
    
    unsigned int curkey, key, highkey;	/* current color key */
    unsigned int lastwaskey;	
        /* The last token we processes was a key, and we have processed
           at least one token.
        */
    char curbuf[BUFSIZ];		/* current buffer */
    bool isTransparent;
    
    unsigned int colorNumber;
        /* The color number that will appear in the raster to refer to the
           color indicated by this color map line.
        */
    unsigned int bytesRead;

    /* read the chars */
    t1 = strchr(line, '"');
    if (t1 == NULL)
        pm_error("A line that is supposed to be an entry in the color "
                 "table does not start with a quote.  The line is '%s'.  "
                 "It is the %uth entry in the color table.", 
                 line, seqNum);
    else
        ++t1;  /* Points now to first color number character */
    
    getColorNumber(t1, charsPerPixel, nColors, &colorNumber, &bytesRead);
    if (bytesRead < charsPerPixel)
        pm_error("A color map entry ends in the middle of the colormap index");

    t1 += bytesRead;

    /*
     * read color keys and values 
     */
    curkey = 0; 
    highkey = 1;
    lastwaskey = FALSE;
    t2 = t1;
    endOfEntry = FALSE;
    while ( !endOfEntry ) {
        int isKey;   /* boolean */
        getword(str2, &t2);
        if (strlen(str2) == 0)
            endOfEntry = TRUE;
        else {
            /* See if the word we got is a valid key (and get its key
               number if so)
            */
            for (key = 1; 
                 key <= NKEYS && !streq(xpmColorKeys[key - 1], str2); 
                 key++);
            isKey = (key <= NKEYS);

            if (lastwaskey || !isKey) {
                /* This word is a color specification (or "none" for
                   transparent).
                */
                if (!curkey) 
                    pm_error("Missing color key token in color table line "
                             "'%s' before '%s'.", line, str2);
                if (!lastwaskey) 
                    strcat(curbuf, " ");		/* append space */
                if ( (strneq(str2, "None", 4)) 
                     || (strneq(str2, "none", 4)) ) {
                    /* This entry identifies the transparent color number */
                    strcat(curbuf, "#000000");  /* Make it black */
                    isTransparent = TRUE;
                } else 
                    strcat(curbuf, str2);		/* append buf */
                lastwaskey = 0;
            } else { 
                /* This word is a key.  So we've seen the last of the 
                   info for the previous key, and we must either put it
                   in the color map or ignore it if we already have a higher
                   color form in the colormap for this colormap entry.
                */
                if (curkey > highkey) {	/* flush string */
                    addToColorMap(seqNum, colorNumber, colors, ptab, curbuf,
                                  isTransparent, transparentP);
                    highkey = curkey;
                }
                curkey = key;			/* set new key  */
                curbuf[0] = '\0';		/* reset curbuf */
                isTransparent = FALSE;
                lastwaskey = 1;
            }
            if (*t2 == '"') break;
        }
    }
    /* Put the info for the last key in the line into the colormap (or
       ignore it if there's already a higher color form for this colormap
       entry in it)
    */
    if (curkey > highkey) {
        addToColorMap(seqNum, colorNumber, colors, ptab, curbuf,
                      isTransparent, transparentP);
        highkey = curkey;
    }
    if (highkey == 1) 
        pm_error("C error scanning color table");
}



static void
readV3ColorTable(FILE *             const ifP,
                 pixel **           const colorsP,
                 unsigned int       const nColors,
                 unsigned int       const charsPerPixel,
                 unsigned int **    const ptabP,
                 TransparentColor * const transparentP) {
/*----------------------------------------------------------------------------
   Read the color table from the XPM Version 3 header.

   Assume *ifP is positioned to the color table; leave it positioned after.
-----------------------------------------------------------------------------*/
    unsigned int colormapSize;
    pixel * colors;  /* malloc'ed array */
    unsigned int * ptab; /* malloc'ed array */

    if (charsPerPixel <= 2) {
        /* Set up direct index (see above) */
        colormapSize =
            charsPerPixel == 0 ?   1 :
            charsPerPixel == 1 ? 256 :
            256*256;
        ptab = NULL;
    } else {
        /* Set up lookup table (see above) */
        colormapSize = nColors;
        MALLOCARRAY(ptab, nColors);
        if (ptab == NULL)
            pm_error("Unable to allocate memory for %u colors", nColors);
    }
    colors = ppm_allocrow(colormapSize);
    
    { 
        /* Read the color table */
        unsigned int seqNum;
            /* Sequence number of entry within color table in XPM header */

        transparentP->none = true;  /* initial value */

        for (seqNum = 0; seqNum < nColors; ++seqNum) {
            char line[MAX_LINE+1];
            getLine(line, sizeof(line), ifP);
            /* skip the comment line if any */
            if (strneq(line, "/*", 2))
                getLine(line, sizeof(line), ifP);
            
            interpretXpm3ColorTableLine(line, seqNum, charsPerPixel,
                                        colors, ptab, nColors, transparentP);
        }
    }
    *colorsP = colors;
    *ptabP   = ptab;
}



static void
readXpm3Header(FILE *             const ifP,
               unsigned int *     const widthP,
               unsigned int *     const heightP, 
               unsigned int *     const charsPerPixelP,
               unsigned int *     const nColorsP,
               pixel **           const colorsP,
               unsigned int **    const ptabP,
               TransparentColor * const transparentP) {
/*----------------------------------------------------------------------------
  Read the header of the XPM file on stream *ifP.  Assume the
  getLine() stream is presently positioned to the beginning of the
  file and it is a Version 3 XPM file.  Leave the stream positioned
  after the header.

  We have two ways to return the colormap, depending on the number of
  characters per pixel in the XPM:  
  
  If it is 1 or 2 characters per pixel, we return the colormap as a
  Netpbm 'pixel' array *colorsP (in newly malloc'ed storage), such
  that if a color in the raster is identified by index N, then
  (*colorsP)[N] is that color.  So this array is either 256 or 64K
  pixels.  In this case, we return *ptabP = NULL.

  If it is more than 2 characters per pixel, we return the colormap as
  both a Netpbm 'pixel' array *colorsP and a lookup table *ptabP (both
  in newly malloc'ed storage).

  If a color in the raster is identified by index N, then for some I,
  (*ptabP)[I] is N and (*colorsP)[I] is the color in question.  So 
  you iterate through *ptabP looking for N and then look at the 
  corresponding entry in *colorsP to get the color.

  Return as *nColorsP the number of colors in the color map (which if
  there are less than 3 characters per pixel, is quite a bit smaller than
  the *colorsP array).

  Return as *transColorNumberP the value of the XPM color number that
  represents a transparent pixel, or -1 if no color number does.
-----------------------------------------------------------------------------*/
    char line[MAX_LINE+1];
    const char * xpm3_signature = "/* XPM */";
    
    unsigned int width, height;
    unsigned int nColors;
    unsigned int charsPerPixel;

    /* Read the XPM signature comment */
    getLine(line, sizeof(line), ifP);
    if (!strneq(line, xpm3_signature, strlen(xpm3_signature))) 
        pm_error("Apparent XPM 3 file does not start with '/* XPM */'.  "
                 "First line is '%s'", xpm3_signature);

    /* Read the assignment line */
    getLine(line, sizeof(line), ifP);
    if (!strneq(line, "static char", 11))
        pm_error("Cannot find data structure declaration.  Expected a "
                 "line starting with 'static char', but found the line "
                 "'%s'.", line);

    getLine(line, sizeof(line), ifP);

    /* Skip the comment block, if one starts here */
    if (strneq(line, "/*", 2)) {
        while (!strstr(line, "*/"))
            getLine(line, sizeof(line), ifP);
        getLine(line, sizeof(line), ifP);
    }

    /* Parse the hints line */
    if (sscanf(line, "\"%u %u %u %u\",", &width, &height,
               &nColors, &charsPerPixel) != 4)
        pm_error("error scanning hints line");

    if (verbose) {
        pm_message("Width x Height:  %u x %u", width, height);
        pm_message("no. of colors:  %u", nColors);
        pm_message("chars per pixel:  %u", charsPerPixel);
    }

    readV3ColorTable(ifP, colorsP, nColors, charsPerPixel, ptabP,
                     transparentP);

    *widthP         = width;
    *heightP        = height;
    *charsPerPixelP = charsPerPixel;
    *nColorsP       = nColors;
}



static void
readV1ColorTable(FILE *          const ifP,
                 pixel **        const colorsP,
                 unsigned int    const nColors,
                 unsigned int    const charsPerPixel,
                 unsigned int ** const ptabP) {
/*----------------------------------------------------------------------------
   Read the color table from the XPM Version 1 header.

   Assume *ifP is positioned to the color table; leave it positioned after.
-----------------------------------------------------------------------------*/
    pixel * colors;  /* malloc'ed array */
    unsigned int * ptab; /* malloc'ed array */
    unsigned int i;

    /* Allocate space for color table. */
    if (charsPerPixel <= 2) {
        /* Up to two chars per pixel, we can use an indexed table. */
        unsigned int v;
        v = 1;
        for (i = 0; i < charsPerPixel; ++i)
            v *= 256;
        colors = ppm_allocrow(v);
        ptab = NULL;
    } else {
        /* Over two chars per pixel, we fall back on linear search. */
        colors = ppm_allocrow(nColors);
        MALLOCARRAY(ptab, nColors);
        if (ptab == NULL)
            pm_error("Unable to allocate memory for %u colors", nColors);
    }

    for (i = 0; i < nColors; ++i) {
        char line[MAX_LINE+1];
        char str1[MAX_LINE+1];
        char str2[MAX_LINE+1];
        char * t1;
        char * t2;

        getLine(line, sizeof(line), ifP);

        if ((t1 = strchr(line, '"')) == NULL)
            pm_error("D error scanning color table");
        if ((t2 = strchr(t1 + 1, '"')) == NULL)
            pm_error("E error scanning color table");
        if (t2 - t1 - 1 != charsPerPixel)
            pm_error("wrong number of chars per pixel in color table");
        strncpy(str1, t1 + 1, t2 - t1 - 1);
        str1[t2 - t1 - 1] = '\0';

        if ((t1 = strchr(t2 + 1, '"')) == NULL)
            pm_error("F error scanning color table");
        if ((t2 = strchr(t1 + 1, '"')) == NULL)
            pm_error("G error scanning color table");
        strncpy(str2, t1 + 1, t2 - t1 - 1);
        str2[t2 - t1 - 1] = '\0';

        {
            unsigned int colorNumber;
            unsigned int bytesRead;

            getColorNumber(str1, charsPerPixel, nColors,
                           &colorNumber, &bytesRead);
            
            if (bytesRead < charsPerPixel)
                pm_error("A color map entry ends in the middle "
                         "of the colormap index");

            if (charsPerPixel <= 2)
                /* Index into table. */
                colors[colorNumber] = ppm_parsecolor(str2, PPM_MAXMAXVAL);
            else {
                /* Set up linear search table. */
                colors[i] = ppm_parsecolor(str2, PPM_MAXMAXVAL);
                ptab[i] = colorNumber;
            }
        }
    }
    *colorsP = colors;
    *ptabP   = ptab;
}



static void
readXpm1Header(FILE *          const ifP,
               unsigned int *  const widthP,
               unsigned int *  const heightP, 
               unsigned int *  const charsPerPixelP,
               unsigned int *  const nColorsP, 
               pixel **        const colorsP,
               unsigned int ** const ptabP) {
/*----------------------------------------------------------------------------
  Read the header of the XPM file on stream *ifP.  Assume the
  getLine() stream is presently positioned to the beginning of the
  file and it is a Version 1 XPM file.  Leave the stream positioned
  after the header.
  
  Return the information from the header the same as for readXpm3Header.
-----------------------------------------------------------------------------*/
    int format, v;
    bool processedStaticChar;  
        /* We have read up to and interpreted the "static char..." line */
    bool gotPixel;
    char * t1;

    *widthP = *heightP = *nColorsP = format = -1;
    gotPixel = false;

    /* Read the initial defines. */
    processedStaticChar = FALSE;
    while (!processedStaticChar) {
        char line[MAX_LINE+1];
        char str1[MAX_LINE+1];

        getLine(line, sizeof(line), ifP);

        if (sscanf(line, "#define %s %d", str1, &v) == 2) {
            if ((t1 = strrchr(str1, '_')) == NULL)
                t1 = str1;
            else
                ++t1;
            if (streq(t1, "format"))
                format = v;
            else if (streq(t1, "width"))
                *widthP = v;
            else if (streq(t1, "height"))
                *heightP = v;
            else if (streq(t1, "nColors"))
                *nColorsP = v;
            else if (streq(t1, "pixel")) {
                gotPixel = TRUE;
                *charsPerPixelP = v;
            }
        } else if (strneq(line, "static char", 11)) {
            if ((t1 = strrchr(line, '_')) == NULL)
                t1 = line;
            else
                ++t1;
            processedStaticChar = TRUE;
        }
    }
    /* File is positioned to "static char" line, which is in line[] and
       t1 points to position of last "_" in the line, or the beginning of
       the line if there is no "_"
    */
    if (!gotPixel)
        pm_error("No 'pixel' value (characters per pixel)");
    if (format == -1)
        pm_error("missing or invalid format");
    if (format != 1)
        pm_error("can't handle XPM version %d", format);
    if (*widthP == -1)
        pm_error("missing or invalid width");
    if (*heightP == -1)
        pm_error("missing or invalid height");
    if (*nColorsP == -1)
        pm_error("missing or invalid nColors");

    if (*charsPerPixelP > 2)
        pm_message("WARNING: > 2 characters per pixel uses a lot of memory");

    /* If there's a monochrome color table, skip it. */
    if (strneq(t1, "mono", 4)) {
        for (;;) {
            char line[MAX_LINE+1];
            getLine(line, sizeof(line), ifP);
            if (strneq(line, "static char", 11))
                break;
        }
    }
    readV1ColorTable(ifP, colorsP, *nColorsP, *charsPerPixelP, ptabP);

    /* Position to first line of raster (which is the line after
       "static char ...").
    */
    for (;;) {
        char line[MAX_LINE+1];
        getLine(line, sizeof(line), ifP);
        if (strneq(line, "static char", 11))
            break;
    }
}



static void
getColormapIndex(const char **  const lineCursorP,
                 unsigned int   const charsPerPixel,
                 unsigned int * const ptab, 
                 unsigned int   const nColors,
                 unsigned int * const colormapIndexP) {
/*----------------------------------------------------------------------------
   Read from the line (advancing cursor *lineCursorP) the next
   color number, which is 'charsPerPixel' characters long, and determine
   from it the index into the colormap of the color represented.

   That index is just the color number itself if 'ptab' is NULL.  Otherwise,
   'ptab' shadows the colormap and the index we return is the index into
   'ptab' of the element that contains the color number.
-----------------------------------------------------------------------------*/
    const char * const pixelBytes = *lineCursorP;

    unsigned int colorNumber;
    unsigned int bytesRead;

    getColorNumber(pixelBytes, charsPerPixel, nColors,
                   &colorNumber, &bytesRead);

    if (bytesRead < charsPerPixel) {
        if (pixelBytes[bytesRead] == '\0')
            pm_error("XPM input file ends in the middle of a string "
                     "that represents a raster line");
        else if (pixelBytes[bytesRead] == '"')
            pm_error("A string that represents a raster line in the "
                     "XPM input file is too short to contain all the "
                     "pixels (%u characters each)",
                     charsPerPixel);
        else
            pm_error("INTERNAL ERROR.  Failed to read a raster value "
                     "for unknown reason");
    }
    if (ptab == NULL)
        /* colormap is indexed directly by XPM color number */
        *colormapIndexP = colorNumber;
    else {
        /* colormap shadows ptab[].  Find this color # in ptab[] */
        unsigned int i;
        for (i = 0; i < nColors && ptab[i] != colorNumber; ++i);
        if (i < nColors)
            *colormapIndexP = i;
        else
            pm_error("Color number %u is in raster, but not in colormap",
                     colorNumber);
    }
    *lineCursorP += bytesRead;
}



static void
interpretXpmLine(char            const line[],
                 unsigned int    const width,
                 unsigned int    const charsPerPixel,
                 unsigned int    const nColors,
                 unsigned int *  const ptab, 
                 unsigned int ** const cursorP) {
/*----------------------------------------------------------------------------
   Interpret one line from XPM input which describes one raster line of the
   image.  The XPM line is in 'line', and its format is 'width' pixel,
   'charsPerPixel' characters per pixel.  'ptab' is the color table that
   applies to the line, which table has 'nColors' colors.

   Put the colormap indexes for the pixels represented in 'line' at
   *cursorP, lined up in the order they are in 'line', and return
   *cursorP positioned just after the last one.

   If the line doesn't start with a quote (e.g. it is empty), we issue
   a warning and just treat the line as one that describes no pixels.

   Abort program if there aren't exactly 'width' pixels in the line.
-----------------------------------------------------------------------------*/
    unsigned int pixelCtSoFar;
    const char * lineCursor;

    lineCursor = strchr(line, '"');  /* position to 1st quote in line */
    if (lineCursor == NULL) {
        /* We've seen a purported XPM that had a blank line in it.  Just
           ignoring it was the right thing to do.  05.05.27.
        */
        pm_message("WARNING:  No opening quotation mark in XPM input "
                   "line which is supposed to be a line of raster data: "
                   "'%s'.  Ignoring this line.", line);
    } else {
        ++lineCursor; /* Skip to first character after quote */

        /* Handle pixels until a close quote, eol, or we've returned all
           the pixels Caller wants.
        */
        for (pixelCtSoFar = 0; pixelCtSoFar < width; ++pixelCtSoFar) {
            unsigned int colormapIndex;

            getColormapIndex(&lineCursor, charsPerPixel, ptab, nColors,
                             &colormapIndex);

            *(*cursorP)++ = colormapIndex;
        }
        if (*lineCursor != '"')
            pm_error("A raster line continues past width of image");
    }
}



static void
readXpmFile(FILE *             const ifP,
            unsigned int *     const widthP,
            unsigned int *     const heightP, 
            pixel **           const colorsP,
            unsigned int **    const dataP, 
            TransparentColor * const transparentP) {
/*----------------------------------------------------------------------------
   Read the XPM file from stream *ifP.

   Return the dimensions of the image as *widthP and *heightP.
   Return the color map as *colorsP, which is an array of *nColorsP
   colors.

   Return the raster in newly malloced storage, an array of *widthP by
   *heightP integers, each of which is an index into the colormap
   *colorsP (and therefore less than *nColorsP).  Return the address
   of the array as *dataP.

   In the colormap, put black for the transparent color, if the XPM 
   image contains one.
-----------------------------------------------------------------------------*/
    unsigned int * data;
    char line[MAX_LINE+1], str1[MAX_LINE+1];
    unsigned int totalpixels;
    unsigned int * cursor;
        /* cursor into data{} */
    unsigned int * maxCursor;
        /* value of above cursor for last pixel in image */
    unsigned int * ptab;   /* colormap - malloc'ed */
    int rc;
    unsigned int nColors;
    unsigned int charsPerPixel;
    unsigned int width, height;

    backup = FALSE;

    /* Read the header line */
    getLine(line, sizeof(line), ifP);
    backup = TRUE;  /* back up so next read reads this line again */
    
    rc = sscanf(line, "/* %s */", str1);
    if (rc == 1 && strneq(str1, "XPM", 3)) {
        /* It's an XPM version 3 file */
        readXpm3Header(ifP, &width, &height, &charsPerPixel,
                       &nColors, colorsP, &ptab, transparentP);
    } else {				/* try as an XPM version 1 file */
        /* Assume it's an XPM version 1 file */
        readXpm1Header(ifP, &width, &height, &charsPerPixel, 
                       &nColors, colorsP, &ptab);
        transparentP->none = true;  /* No transparency in version 1 */
    }
    totalpixels = width * height;
    MALLOCARRAY(data, totalpixels);
    if (!data)
        pm_error("Could not get %u bytes of memory for image", totalpixels);
    cursor = &data[0];
    maxCursor = &data[totalpixels - 1];
	getLine(line, sizeof(line), ifP); 
        /* read next line (first line may not always start with comment) */
    while (cursor <= maxCursor) {
        if (strneq(line, "/*", 2)) {
            /* It's a comment.  Ignore it. */
        } else {
            interpretXpmLine(line, width, charsPerPixel, nColors, ptab,
                             &cursor);
        }
        if (cursor <= maxCursor)
            getLine(line, sizeof(line), ifP);
    }
    if (ptab) free(ptab);
    *dataP   = data;
    *widthP  = width;
    *heightP = height;
}
 


static void
writeOutput(FILE *           const imageout_file,
            FILE *           const alpha_file,
            unsigned int     const cols,
            unsigned int     const rows, 
            pixel *          const colors,
            unsigned int *   const data,
            TransparentColor const transparent) {
/*----------------------------------------------------------------------------
   Write the image in 'data' to open PPM file stream 'imageout_file',
   and the alpha mask for it to open PBM file stream 'alpha_file',
   except if either is NULL, skip it.

   'data' is an array of cols * rows integers, each one being an index
   into the colormap 'colors'.

   Where the index 'transparent' occurs in 'data', the pixel is supposed
   to be transparent.  If 'transparent' < 0, no pixels are transparent.
-----------------------------------------------------------------------------*/
    unsigned int row;
    pixel * pixrow;
    bit * alpharow;

    if (imageout_file)
        ppm_writeppminit(imageout_file, cols, rows, PPM_MAXMAXVAL, 0);
    if (alpha_file)
        pbm_writepbminit(alpha_file, cols, rows, 0);

    pixrow = ppm_allocrow(cols);
    alpharow = pbm_allocrow(cols);

    for (row = 0; row < rows; ++row ) {
        unsigned int * const datarow = data+(row*cols);

        unsigned int col;

        for (col = 0; col < cols; ++col) {
            pixrow[col] = colors[datarow[col]];
            if (!transparent.none && datarow[col] == transparent.index)
                alpharow[col] = PBM_BLACK;
            else
                alpharow[col] = PBM_WHITE;
        }
        if (imageout_file)
            ppm_writeppmrow(imageout_file, 
                            pixrow, cols, (pixval) PPM_MAXMAXVAL, 0);
        if (alpha_file)
            pbm_writepbmrow(alpha_file, alpharow, cols, 0);
    }
    ppm_freerow(pixrow);
    pbm_freerow(alpharow);

    if (imageout_file)
        pm_close(imageout_file);
    if (alpha_file)
        pm_close(alpha_file);
}    



int
main(int argc, char *argv[]) {

    FILE * ifP;
    FILE * alpha_file;
    FILE * imageout_file;
    pixel * colormap;
    unsigned int cols, rows;
    TransparentColor transparent;
        /* Pixels of what color, if any, are transparent */
    unsigned int * data;
        /* The image as an array of width * height integers, each one
           being an index int colormap[].
        */

    struct cmdlineInfo cmdline;

    ppm_init(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    verbose = cmdline.verbose;

    if ( cmdline.input_filespec != NULL ) 
        ifP = pm_openr( cmdline.input_filespec);
    else
        ifP = stdin;

    if (cmdline.alpha_stdout)
        alpha_file = stdout;
    else if (cmdline.alpha_filename == NULL) 
        alpha_file = NULL;
    else {
        alpha_file = pm_openw(cmdline.alpha_filename);
    }

    if (cmdline.alpha_stdout) 
        imageout_file = NULL;
    else
        imageout_file = stdout;

    readXpmFile(ifP, &cols, &rows, &colormap, &data, &transparent);
    
    pm_close(ifP);

    writeOutput(imageout_file, alpha_file, cols, rows, colormap, data,
                transparent);

    free(colormap);
    
    return 0;
}



