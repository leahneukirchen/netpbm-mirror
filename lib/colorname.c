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

#include "netpbm/pm_c_util.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

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
        eol = FALSE;    /* initial value */
        *filePP = NULL;  /* initial value */
        while (!eol && !*filePP) {
            const char * token;
            token = pm_strsep(&cursor, ":");
            if (token) {
                *filePP = fopen(token, "r");
            } else
                eol = TRUE;
        }
        free(buffer);
    } else
        *filePP = NULL;
}



FILE *
pm_openColornameFile(const char * const fileName, const int must_open) {
/*----------------------------------------------------------------------------
   Open the colorname dictionary file.  Its file name is 'fileName', unless
   'fileName' is NULL.  In that case, its file name is the value of the
   environment variable whose name is RGB_ENV (e.g. "RGBDEF").  Except
   if that environment variable is not set, it is the first file found,
   if any, in the search path RGB_DB_PATH.
   
   'must_open' is a logical: we must get the file open or die.  If
   'must_open' is true and we can't open the file (e.g. it doesn't
   exist), exit the program with an error message.  If 'must_open' is
   false and we can't open the file, just return a null pointer.
-----------------------------------------------------------------------------*/
    FILE *f;

    if (fileName == NULL) {
        const char * rgbdef = getenv(RGBENV);
        if (rgbdef) {
            /* The environment variable is set */
            f = fopen(rgbdef, "r");
            if (f == NULL && must_open)
                pm_error("Can't open the color names dictionary file "
                         "named %s, per the %s environment variable.  "
                         "errno = %d (%s)",
                         rgbdef, RGBENV, errno, strerror(errno));
        } else {            
            /* The environment variable isn't set, so try the hardcoded
               default color name dictionary locations.
            */
            openColornameFileSearch(RGB_DB_PATH, &f);

            if (f == NULL && must_open) {
                pm_error("can't open color names dictionary file from the "
                         "path '%s' "
                         "and Environment variable %s not set.  Set %s to "
                         "the pathname of your rgb.txt file or don't use "
                         "color names.", 
                         RGB_DB_PATH, RGBENV, RGBENV);
            }
        }
    } else {
        f = fopen(fileName, "r");
        if (f == NULL && must_open)
            pm_error("Can't open the color names dictionary file '%s'.  "
                     "errno = %d (%s)", fileName, errno, strerror(errno));
        
    }
    lineNo = 0;
    return(f);
}



struct colorfile_entry
pm_colorget(FILE * const f) {
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
    
    gotOne = FALSE;  /* initial value */
    eof = FALSE;
    while (!gotOne && !eof) {
        lineNo++;
        rc = fgets(buf, sizeof(buf), f);
        if (rc == NULL)
            eof = TRUE;
        else {
            if (buf[0] != '#' && buf[0] != '\n' && buf[0] != '!' &&
                buf[0] != '\0') {
                if (sscanf(buf, "%ld %ld %ld %[^\n]", 
                           &retval.r, &retval.g, &retval.b, colorname) 
                    == 4 )
                    gotOne = TRUE;
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

    FILE * fP;
    bool gotit;
    bool colorfileExhausted;
    struct colorfile_entry colorfileEntry;
    char * canoncolor;

    fP = pm_openColornameFile(NULL, TRUE);  /* exits if error */
    canoncolor = strdup(colorname);
    pm_canonstr(canoncolor);
    gotit = FALSE;
    colorfileExhausted = FALSE;
    while (!gotit && !colorfileExhausted) {
        colorfileEntry = pm_colorget(fP);
        if (colorfileEntry.colorname) {
            pm_canonstr(colorfileEntry.colorname);
            if (streq(canoncolor, colorfileEntry.colorname))
                gotit = TRUE;
        } else
            colorfileExhausted = TRUE;
    }
    fclose(fP);
    
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

    double const epsilon = 1.0/65536.0;

    tuplen color;
    pixval r, g, b;

    MALLOCARRAY_NOFAIL(color, 3);

    pm_parse_dictionary_namen(colorname, color);

    r = ROUNDU(color[PAM_RED_PLANE] * maxval);
    g = ROUNDU(color[PAM_GRN_PLANE] * maxval);
    b = ROUNDU(color[PAM_BLU_PLANE] * maxval);
    
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



