/* colorname.c - colorname routines, not dependent on Netpbm formats
**
** Taken from libppm4.c May 2002.

** Copyright (C) 1989 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#define _DEFAULT_SOURCE 1  /* New name for SVID & BSD source defines */
#define _BSD_SOURCE 1      /* Make sure strdup() is in string.h */
#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */

#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

#include "netpbm/pm_c_util.h"
#include "netpbm/nstring.h"
#include "netpbm/mallocvar.h"

#include "colorname.h"

static int lineNo;



void
pm_canonstr(char * const arg) {
/*----------------------------------------------------------------------------
   Modify string 'arg' to canonical form: lower case, no white space.
-----------------------------------------------------------------------------*/
    const char * srcCursor;
    char * dstCursor;

    for (srcCursor = arg, dstCursor = arg; *srcCursor; ++srcCursor) {
        if (!ISSPACE(*srcCursor)) {
            *dstCursor++ =
                ISUPPER(*srcCursor) ? tolower(*srcCursor) : *srcCursor;
        }
    }
}



static void
openColornameFileSearch(const char * const searchPath,
                        FILE **      const filePP) {
/*----------------------------------------------------------------------------
   Open the color name file, finding it via the search path 'searchPath'.

   Return as *filePP the stream handle for it, but if we don't find it
   (or just can open it) anywhere, return *filePP == NULL.
-----------------------------------------------------------------------------*/
    char * buffer;

    buffer = strdup(searchPath);

    if (buffer) {
        char * cursor;
        bool eol;

        cursor = &buffer[0];
        eol = false;    /* initial value */
        *filePP = NULL;  /* initial value */
        while (!eol && !*filePP) {
            const char * token;
            token = pm_strsep(&cursor, ":");
            if (token) {
                *filePP = fopen(token, "r");
            } else
                eol = true;
        }
        free(buffer);
    } else
        *filePP = NULL;
}



FILE *
pm_openColornameFile(const char * const fileName,
                     int          const mustOpen) {
/*----------------------------------------------------------------------------
   Open the colorname dictionary file.  Its file name is 'fileName', unless
   'fileName' is NULL.  In that case, its file name is the value of the
   environment variable whose name is RGB_ENV (e.g. "RGBDEF").  Except
   if that environment variable is not set, it is the first file found,
   if any, in the search path RGB_DB_PATH.

   'mustOpen' is a logical: we must get the file open or die.  If
   'mustOpen' is true and we can't open the file (e.g. it doesn't
   exist), exit the program with an error message.  If 'mustOpen' is
   false and we can't open the file, just return a null pointer.
-----------------------------------------------------------------------------*/
    FILE * fileP;

    if (fileName == NULL) {
        const char * rgbdef = getenv(RGBENV);
        if (rgbdef) {
            /* The environment variable is set */
            fileP = fopen(rgbdef, "r");
            if (fileP == NULL && mustOpen)
                pm_error("Can't open the color names dictionary file "
                         "named %s, per the %s environment variable.  "
                         "errno = %d (%s)",
                         rgbdef, RGBENV, errno, strerror(errno));
        } else {
            /* The environment variable isn't set, so try the hardcoded
               default color name dictionary locations.
            */
            openColornameFileSearch(RGB_DB_PATH, &fileP);

            if (fileP == NULL && mustOpen) {
                pm_error("can't open color names dictionary file from the "
                         "path '%s' "
                         "and Environment variable %s not set.  Set %s to "
                         "the pathname of your rgb.txt file or don't use "
                         "color names.",
                         RGB_DB_PATH, RGBENV, RGBENV);
            }
        }
    } else {
        fileP = fopen(fileName, "r");
        if (fileP == NULL && mustOpen)
            pm_error("Can't open the color names dictionary file '%s'.  "
                     "errno = %d (%s)", fileName, errno, strerror(errno));

    }
    lineNo = 0;
    return fileP;
}



struct colorfile_entry
pm_colorget(FILE * const fileP) {
/*----------------------------------------------------------------------------
   Get next color entry from the color name dictionary file 'f'.

   If eof or error, return a color entry with NULL for the color name.

   Otherwise, return color name in static storage within.
-----------------------------------------------------------------------------*/
    char buf[200];
    static char colorname[200];
    bool gotOne;
    bool eof;
    struct colorfile_entry retval;
    char * rc;

    for (gotOne = false, eof = false; !gotOne && !eof; ) {
        lineNo++;
        rc = fgets(buf, sizeof(buf), fileP);
        if (rc == NULL)
            eof = true;
        else {
            if (buf[0] != '#' && buf[0] != '\n' && buf[0] != '!' &&
                buf[0] != '\0') {
                if (sscanf(buf, "%ld %ld %ld %[^\n]",
                           &retval.r, &retval.g, &retval.b, colorname)
                    == 4 )
                    gotOne = true;
                else {
                    if (buf[strlen(buf)-1] == '\n')
                        buf[strlen(buf)-1] = '\0';
                    pm_message("can't parse color names dictionary Line %d:  "
                               "'%s'",
                               lineNo, buf);
                }
            }
        }
    }
    if (gotOne)
        retval.colorname = colorname;
    else
        retval.colorname = NULL;
    return retval;
}



void
pm_parse_dictionary_namen(char   const colorname[],
                          tuplen const color) {
/*----------------------------------------------------------------------------
   Return as *tuplen a tuple of type RGB that represents the color named
   'colorname'.  This is an actual name, like "pink", not just a color
   specification, like "rgb:0/0/0".

   If the color name is unknown, abort the program.  If there are two entries
   in the dictionary for the same color name, use the first one.

   We use the Netpbm color dictionary used by 'pm_openColornamefile'.

   Caller must ensure there is enough memory at 'tuplen' for at least 3
   samples.  We set the first 3 samples of the tuple value and ignore any
   others.
-----------------------------------------------------------------------------*/
    FILE * fileP;
    bool gotit;
    bool colorfileExhausted;
    struct colorfile_entry colorfileEntry;
    char * canoncolor;

    fileP = pm_openColornameFile(NULL, true);  /* exits if error */
    canoncolor = strdup(colorname);

    if (!canoncolor)
        pm_error("Failed to allocate memory for %u-byte color name",
                 (unsigned)strlen(colorname));

    pm_canonstr(canoncolor);

    for (gotit = false, colorfileExhausted = false;
        !gotit && !colorfileExhausted; ) {

        colorfileEntry = pm_colorget(fileP);
        if (colorfileEntry.colorname) {
            pm_canonstr(colorfileEntry.colorname);
            if (streq(canoncolor, colorfileEntry.colorname))
                gotit = true;
        } else
            colorfileExhausted = true;
    }
    fclose(fileP);

    if (!gotit)
        pm_error("unknown color '%s'", colorname);

    color[PAM_RED_PLANE] = (samplen)colorfileEntry.r / PAM_COLORFILE_MAXVAL;
    color[PAM_GRN_PLANE] = (samplen)colorfileEntry.g / PAM_COLORFILE_MAXVAL;
    color[PAM_BLU_PLANE] = (samplen)colorfileEntry.b / PAM_COLORFILE_MAXVAL;

    free(canoncolor);
}



void
pm_parse_dictionary_name(char    const colorname[],
                         pixval  const maxval,
                         int     const closeOk,
                         pixel * const colorP) {
/*----------------------------------------------------------------------------
   Same as 'pm_parse_dictionary_name' except return the color as a
   pixel value of type 'pixel', with a maxval of 'maxval'.

   We round the color to the nearest one that can be represented with the
   resolution indicated by 'maxval' (rounding each component independently).
   Iff rounding is necessary and 'closeOK' is false, we issue an informational
   message about the rounding.
-----------------------------------------------------------------------------*/
    double const epsilon = 1.0/65536.0;

    tuplen color;
    pixval r, g, b;

    MALLOCARRAY_NOFAIL(color, 3);

    pm_parse_dictionary_namen(colorname, color);

    r = ppm_unnormalize(color[PAM_RED_PLANE], maxval);
    g = ppm_unnormalize(color[PAM_GRN_PLANE], maxval);
    b = ppm_unnormalize(color[PAM_BLU_PLANE], maxval);

    if (!closeOk) {
        if (maxval != PAM_COLORFILE_MAXVAL) {
            if (fabs((double)r / maxval - color[PAM_RED_PLANE]) > epsilon ||
                fabs((double)g / maxval - color[PAM_GRN_PLANE]) > epsilon ||
                fabs((double)b / maxval - color[PAM_BLU_PLANE]) > epsilon) {
                pm_message("WARNING: color '%s' cannot be represented "
                           "exactly with a maxval of %u.  "
                           "Approximating as (%u,%u,%u).  "
                           "(The color dictionary uses maxval %u, so that "
                           "maxval will always work).",
                           colorname, maxval, r, g, b,
                           PAM_COLORFILE_MAXVAL);
            }
        }
    }

    PPM_ASSIGN(*colorP, r, g, b);
}


