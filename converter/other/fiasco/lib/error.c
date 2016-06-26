/*
 *  error.c:		Error handling
 *
 *  Written by:		Stefan Frank
 *			Ullrich Hafner
 *  
 *  Credits:	Modelled after variable argument routines from Jef
 *		Poskanzer's pbmplus package. 
 *
 *  This file is part of FIASCO (Fractal Image And Sequence COdec)
 *  Copyright (C) 1994-2000 Ullrich Hafner

    "int dummy = " change to int dummy; dummy =" for Netpbm to avoid 
    unused variable warning.

 */

/*
 *  $Date: 2000/06/14 20:49:37 $
 *  $Author: hafner $
 *  $Revision: 5.1 $
 *  $State: Exp $
 */

#define _ERROR_C

#include "config.h"

#include <stdio.h>
#include <errno.h>

#include <stdarg.h>
#define VA_START(args, lastarg) va_start(args, lastarg)
#include <string.h>

#if HAVE_SETJMP_H
#	include <setjmp.h>
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
static char 	      	  *error_message = NULL;

#if HAVE_SETJMP_H
jmp_buf env;
#endif /* HAVE_SETJMP_H */

/*****************************************************************************

			       public code
  
*****************************************************************************/

void
set_error(const char *format, ...) {
/*----------------------------------------------------------------------------
  Set error text to given string.
-----------------------------------------------------------------------------*/
    va_list      args;
    unsigned     len;
    const char * str;

    len = 0;  /* initial value */
    str = format;  /* initial value */

    VA_START (args, format);

    len = strlen (format);
    while ((str = strchr (str, '%'))) {
        ++str;
        if (*str == 's') {
            char * const vstring = va_arg (args, char *);
            len += strlen(vstring);
        } else if (*str == 'd') {
            va_arg (args, int);
            len += 10;
        } else if (*str == 'c') {
            va_arg (args, int);
            len += 1;
        } else
            return;
        ++str;
    }
    va_end(args);

    VA_START(args, format);

    if (error_message)
        Free(error_message);
    error_message = Calloc(len, sizeof (char));
   
    vsprintf(error_message, format, args);

    va_end(args);
}



void
error(const char *format, ...) {
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
            va_arg (args, int);
            len += 10;
        } else if (*str == 'c') {
            va_arg (args, int);
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
fiasco_get_error_message (void)
/*
 *  Return value:
 *	Last error message of FIASCO library.
 */
{
   return error_message ? error_message : "";
}

const char *
get_system_error (void)
{
   return strerror (errno);
}

void
file_error (const char *filename)
/*
 *  Print file error message and exit.
 *
 *  No return value.
 */
{
   error ("File `%s': I/O Error - %s.", filename, get_system_error ());
}

void 
warning (const char *format, ...)
/*
 *  Issue a warning and continue execution.
 *
 *  No return value.
 */
{
   va_list	args;

   VA_START (args, format);

   if (verboselevel == FIASCO_NO_VERBOSITY)
      return;
	
   fprintf (stderr, "Warning: ");
   vfprintf (stderr, format, args);
   fputc ('\n', stderr);

   va_end (args);
}

void 
message (const char *format, ...)
/*
 *  Print a message to stderr.
 */
{
   va_list args;

   VA_START (args, format);

   if (verboselevel == FIASCO_NO_VERBOSITY)
      return;

   vfprintf (stderr, format, args);
   fputc ('\n', stderr);
   va_end (args);
}

void 
debug_message (const char *format, ...)
/*
 *  Print a message to stderr.
 */
{
   va_list args;

   VA_START (args, format);

   if (verboselevel < FIASCO_ULTIMATE_VERBOSITY)
      return;

   fprintf (stderr, "*** ");
   vfprintf (stderr, format, args);
   fputc ('\n', stderr);
   va_end (args);
}

void
info (const char *format, ...)
/*
 *  Print a message to stderr. Do not append a newline.
 */
{
   va_list args;

   VA_START (args, format);

   if (verboselevel == FIASCO_NO_VERBOSITY)
      return;

   vfprintf (stderr, format, args);
   fflush (stderr);
   va_end (args);
}

void
fiasco_set_verbosity (fiasco_verbosity_e level)
{
   verboselevel = level;
}

fiasco_verbosity_e
fiasco_get_verbosity (void)
{
   return verboselevel;
}
