#ifndef RLE_CONFIG_H_INCLUDED
#define RLE_CONFIG_H_INCLUDED
#include "pm_config.h"
#if MSVCRT
#define NO_OPEN_PIPES
#endif

#define ABEKASA60 ABEKASA60
#define ABEKASA62 ABEKASA62
#define ALIAS ALIAS
#define CUBICOMP CUBICOMP
#define GIF GIF
#define GRAYFILES GRAYFILES
#define MACPAINT MACPAINT
#define POSTSCRIPT POSTSCRIPT
#define TARGA TARGA
#define TIFF2p4 TIFF2p4
#define VICAR VICAR
#define WASATCH WASATCH
#define WAVEFRONT WAVEFRONT
#define GCC GCC
#define CONST_DECL CONST_DECL
#define NO_MAKE_MAKEFILE NO_MAKE_MAKEFILE
#define NO_TOOLS NO_TOOLS
#define USE_TIME_H USE_TIME_H
#define USE_RANDOM USE_RANDOM
#define USE_STDARG USE_STDARG
#define USE_STDLIB_H USE_STDLIB_H
#define USE_UNISTD_H USE_UNISTD_H
#define USE_STRING_H USE_STRING_H
#define VOID_STAR VOID_STAR
/* -*-C-*- */
/***************** From rle_config.tlr *****************/

/* CONST_DECL must be defined as 'const' or nothing. */
#ifdef CONST_DECL
#undef CONST_DECL
#define CONST_DECL const

#else
#define CONST_DECL

#endif

/* A define for getx11. */
#ifndef USE_XLIBINT_H
#define XLIBINT_H_NOT_AVAILABLE
#endif

/* Typedef for void * so we can use it consistently. */
#ifdef VOID_STAR
typedef	void *void_star;
#else
typedef char *void_star;
#endif

/* Some programs include files from other packages that also declare
 * malloc.  Avoid double declaration by #define NO_DECLARE_MALLOC
 * before including this file.
 */
#ifndef NO_DECLARE_MALLOC
#   include <sys/types.h>	/* For size_t. */
    extern void_star malloc( size_t );
    extern void_star calloc( size_t, size_t );
    extern void_star realloc( void_star, size_t );
    extern void free( void_star );
#endif /* NO_DECLARE_MALLOC */

extern char *getenv( CONST_DECL char *name );


#ifdef NEED_BSTRING
    /* From bstring.c. */
    /*****************************************************************
     * TAG( bstring bzero )
     * 'Byte string' functions.
     */
#   define bzero( _str, _n )		memset( _str, '\0', _n )
#   define bcopy( _from, _to, _count )	memcpy( _to, _from, _count )
#endif

#ifdef NEED_SETLINEBUF
#   define setlinebuf( _s )	setvbuf( (_s), NULL, _IOLBF, 0 )
#endif

#endif
