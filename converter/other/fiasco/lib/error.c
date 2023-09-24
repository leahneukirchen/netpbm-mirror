/*
 *  error.c:            Error handling
 *
 *  Written by:         Stefan Frank
 *                      Ullrich Hafner
 *
 *  Credits:    Modelled after variable argument routines from Jef
 *              Poskanzer's pbmplus package.
 *
 *  This file is part of FIASCO (Fractal Image And Sequence COdec)
 *  Copyright (C) 1994-2000 Ullrich Hafner
 */

#define _ERROR_C

#include "config.h"

#include <stdbool.h>
#include <stdio.h>
#include <errno.h>

#include <stdarg.h>
#define VA_START(args, lastarg) va_start(args, lastarg)
#include <string.h>

#if HAVE_SETJMP_H
#       include <setjmp.h>
#endif /* HAVE_SETJMP_H */

#include "types.h"
#include "macros.h"
#include "error.h"

#include "misc.h"
#include "fiasco.h"

/*****************************************************************************

                             local variables

*****************************************************************************/

static fiasco_verbosity_e  verboselevel  = FIASCO_SOME_VERBOSITY;
static char               *error_message = NULL;

#if HAVE_SETJMP_H
jmp_buf env;
#endif /* HAVE_SETJMP_H */

/*****************************************************************************

                               public code

*****************************************************************************/

void
set_error(const char * const format, ...) {
/*----------------------------------------------------------------------------
  Set error text to given string.
-----------------------------------------------------------------------------*/
    va_list      args;
    unsigned     len;
    bool         error;
    const char * str;

    VA_START (args, format);

    /* Compute how long the error text will be: 'len' */

    for (len = strlen(format), str = &format[0], error = false;
         *str && !error; ) {

        str = strchr(str, '%');

        if (*str) {
            ++str; /* Move past % */
            if (*str == 's') {
                char * const vstring = va_arg (args, char *);
                len += strlen(vstring);
            } else if (*str == 'd') {
                (void)va_arg(args, int);
                len += 10;
            } else if (*str == 'c') {
                (void)va_arg(args, int);
                len += 1;
            } else
                error = true;
            if (!error)
                ++str;
        }
    }
    va_end(args);

    if (!error) {
        VA_START(args, format);

        if (error_message)
            Free(error_message);
        error_message = Calloc(len, sizeof (char));

        vsprintf(error_message, format, args);

        va_end(args);
    }
}



void
error(const char * const format, ...) {
/*----------------------------------------------------------------------------
  Set error text to given string.
-----------------------------------------------------------------------------*/
    va_list      args;
    unsigned     len;
    const char * str;

    len = 0; /* initial value */
    str = &format[0];  /* initial value */

    VA_START (args, format);

    len = strlen (format);
    while ((str = strchr (str, '%'))) {
        ++str;
        if (*str == 's') {
            char * const vstring = va_arg (args, char *);
            len += strlen(vstring);
        } else if (*str == 'd') {
            (void)va_arg(args, int);
            len += 10;
        } else if (*str == 'c') {
            (void)va_arg(args, int);
            len += 1;
        } else {
#if HAVE_SETJMP_H
            longjmp(env, 1);
#else
            exit(1);
#endif
        };

        ++str;
    }
    va_end(args);

    VA_START(args, format);

    if (error_message)
        Free(error_message);
    error_message = Calloc(len, sizeof (char));

    vsprintf(error_message, format, args);

    va_end(args);

#if HAVE_SETJMP_H
    longjmp(env, 1);
#else
    exit(1);
#endif
}



const char *
fiasco_get_error_message(void) {
/*----------------------------------------------------------------------------
  Last error message of FIASCO library.
-----------------------------------------------------------------------------*/
    return error_message ? error_message : "";
}



const char *
get_system_error(void) {
    return strerror(errno);
}



void
file_error(const char * const filename) {
/*----------------------------------------------------------------------------
   Print file error message and exit.
-----------------------------------------------------------------------------*/
    error("File `%s': I/O Error - %s.", filename, get_system_error ());
}



void
warning(const char * const format, ...) {
/*----------------------------------------------------------------------------
  Issue a warning.
-----------------------------------------------------------------------------*/
    va_list     args;

    VA_START (args, format);

    if (verboselevel == FIASCO_NO_VERBOSITY) {
        /* User doesn't want warnings */
    } else {
        fprintf (stderr, "Warning: ");
        vfprintf (stderr, format, args);
        fputc ('\n', stderr);
    }
    va_end (args);
}



void
message(const char * const format, ...) {
/*----------------------------------------------------------------------------
   Print a message to Standard Error
-----------------------------------------------------------------------------*/
    va_list args;

    VA_START (args, format);

    if (verboselevel == FIASCO_NO_VERBOSITY) {
        /* User doesn't want messages */
    } else {
        vfprintf (stderr, format, args);
        fputc ('\n', stderr);
    }
    va_end (args);
}



void
debug_message(const char * const format, ...) {
/*----------------------------------------------------------------------------
   Print a message to Standard Error if debug messages are enabled.
-----------------------------------------------------------------------------*/
    va_list args;

    VA_START (args, format);

    if (verboselevel >= FIASCO_ULTIMATE_VERBOSITY) {
        fprintf (stderr, "*** ");
        vfprintf (stderr, format, args);
        fputc ('\n', stderr);
    }
    va_end (args);
}



void
info(const char * const format, ...) {
/*----------------------------------------------------------------------------
   Print a message to stderr. Do not append a newline.
-----------------------------------------------------------------------------*/
    va_list args;

    VA_START (args, format);

    if (verboselevel == FIASCO_NO_VERBOSITY) {
        /* User doesn't want informational messages */
    } else {
        vfprintf (stderr, format, args);
        fflush (stderr);
    }
    va_end (args);
}



void
fiasco_set_verbosity(fiasco_verbosity_e const level) {
   verboselevel = level;
}



fiasco_verbosity_e
fiasco_get_verbosity(void) {
   return verboselevel;
}



