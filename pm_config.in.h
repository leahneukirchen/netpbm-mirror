/**************************************************************************
                               NETPBM
                           pm_config.in.h
***************************************************************************
  This file provides platform-dependent definitions for all Netpbm
  libraries and the programs that use them.

  The make files generate pm_config.h by copying this file and adding
  other stuff.  The Netpbm programs #include pm_config.h.

  Wherever possible, Netpbm handles customization via the make files
  instead of via this file.  However, Netpbm's make file philosophy
  discourages lining up a bunch of -D options on every compile, so a 
  #define here would be preferable to a -D compile option.

**************************************************************************/

#if defined(USG) || defined(SVR4) || defined(__SVR4)
#define SYSV
#endif
#if !( defined(BSD) || defined(SYSV) || defined(MSDOS) || defined(__amigaos__))
/* CONFIGURE: If your system is >= 4.2BSD, set the BSD option; if you're a
** System V site, set the SYSV option; if you're IBM-compatible, set MSDOS;
** and if you run on an Amiga, set AMIGA. If your compiler is ANSI C, you're
** probably better off setting SYSV - all it affects is string handling.
*/
#define BSD
/* #define SYSV */
/* #define MSDOS */
#endif

/* Switch macros like _POSIX_SOURCE are supposed to add features from
   the indicated standard to the C library.  A source file defines one
   of these macros to declare that it uses features of that standard
   as opposed to conflicting features of other standards (e.g. the
   POSIX foo() subroutine might do something different from the X/Open
   foo() subroutine).  Plus, this forces the coder to understand upon
   what feature sets his program relies.

   But some C library developers have misunderstood this and think of these
   macros like the old __ansi__ macro, which tells the C library, "Don't 
   have any features that aren't in the ANSI standard."  I.e. it's just
   the opposite -- the macro subtracts features instead of adding them.

   This means that on some platforms, Netpbm programs must define
   _POSIX_SOURCE, and on others, it must not.  Netpbm's POSIX_IS_IMPLIED 
   macro indicates that we're on a platform where we need not define
   _POSIX_SOURCE (and probably must not).

   The problematic C libraries treat _XOPEN_SOURCE the same way.
*/
#if defined(__OpenBSD__) || defined (__NetBSD__) || defined(__bsdi__) || defined(__APPLE__)
#define POSIX_IS_IMPLIED
#endif


/* CONFIGURE: If you have an X11-style rgb color names file, define its
** path here.  This is used by PPM to parse color names into rgb values.
** If you don't have such a file, comment this out and use the alternative
** hex and decimal forms to specify colors (see ppm/pgmtoppm.1 for details).  */

#define RGB_DB_PATH \
"/usr/share/netpbm/rgb.txt:" \
"/usr/lib/X11/rgb.txt:" \
"/usr/share/X11/rgb.txt:" \
"/usr/X11R6/lib/X11/rgb.txt"

/* CONFIGURE: This is the name of an environment variable that tells
** where the color names database is.  If the environment variable isn't
** set, Netpbm tries the hardcoded defaults set above.
*/
#define RGBENV "RGBDEF"    /* name of env-var */

#if (defined(SYSV) || defined(__amigaos__))

#include <string.h>

#ifndef __SASC
#ifndef _DCC    /* Amiga DICE Compiler */
#define bzero(dst,len) memset(dst,0,len)
#define bcopy(src,dst,len) memcpy(dst,src,len)
#define bcmp memcmp
#endif /* _DCC */
#endif /* __SASC */

#endif /*SYSV or Amiga*/

/* CONFIGURE: On most BSD systems, malloc() gets declared in stdlib.h, on
** system V, it gets declared in malloc.h. On some systems, malloc.h
** doesn't declare these, so we have to do it here. On other systems,
** for example HP/UX, it declares them incompatibly.  And some systems,
** for example Dynix, don't have a malloc.h at all.  A sad situation.
** If you have compilation problems that point here, feel free to tweak
** or remove these declarations.
*/
#ifdef BSD
#include <stdlib.h>
#endif
#if defined(SYSV)
#include <malloc.h>
#endif
/* extern char* malloc(); */
/* extern char* realloc(); */
/* extern char* calloc(); */

/* CONFIGURE: Some systems don't have vfprintf(), which we need for the
** error-reporting routines.  If you compile and get a link error about
** this routine, uncomment the first define, which gives you a vfprintf
** that uses the theoretically non-portable but fairly common routine
** _doprnt().  If you then get a link error about _doprnt, or
** message-printing doesn't look like it's working, try the second
** define instead.
*/
/* #define NEED_VFPRINTF1 */
/* #define NEED_VFPRINTF2 */

/* CONFIGURE: Some systems don't have strstr(), which some routines need.
** If you compile and get a link error about this routine, uncomment the
** define, which gives you a strstr.
*/
/* #define NEED_STRSTR */

/* CONFIGURE: Set this option if your compiler uses strerror(errno)
** instead of sys_errlist[errno] for error messages.
*/
#define A_STRERROR

/* CONFIGURE: If your system has the setmode() function, set HAVE_SETMODE.
** If you do, and also the O_BINARY file mode, pm_init() will set the mode
** of stdin and stdout to binary for all Netpbm programs.
** You need this with Cygwin (Windows).
*/
#ifdef __CYGWIN__
#define HAVE_SETMODE
#endif

/* #define HAVE_SETMODE */

#ifdef __amigaos__
#include <clib/exec_protos.h>
#define getpid() ((pid_t)FindTask(NULL))
#endif

#ifdef DJGPP
#define HAVE_SETMODE
#define lstat stat
#endif /* DJGPP */

/*  CONFIGURE: Netpbm uses __inline__ to declare functions that should
    be compiled as inline code.  GNU C recognizes the __inline__ keyword.
    If your compiler recognizes any other keyword for this, you can set
    it here.
*/
#if !defined(__GNUC__)
  #if (!defined(__inline__))
    #if (defined(__sgi) || defined(_AIX))
      #define __inline__ __inline
    #else   
      #define __inline__
    #endif
  #endif
#endif

/* At least one compiler can't handle two declarations of the same function
   that aren't literally identical.  E.g. "static foo_fn_t foo1;" conflicts
   with "static void foo1(int);" even if type 'foo_fn_t' is defined as
   void(int).  (The compiler we saw do this is SGI IDO cc (for IRIX 4.3)).

   LITERAL_FN_DEF_MATCH says that the compiler might have this problem,
   so one must be conservative in redeclaring functions.
*/
#if defined(__GNUC__)
  #define LITERAL_FN_DEF_MATCH 0
#else
  #if (defined(__sgi))
    #define LITERAL_FN_DEF_MATCH 1
  #else   
    #define LITERAL_FN_DEF_MATCH 0
  #endif
#endif

/* CONFIGURE: GNU Compiler extensions are used in performance critical places
   when available.  Test whether they exist.

   Turn off by defining NO_GCC_BUILTINS.

   Note that though these influence the resulting Netpbm machine code, the
   compiler setting ultimately decides what instruction set the compiler uses.
   If you want a generic build, check the manual and adjust CFLAGS in
   config.mk accordingly.

   For example, if you want binaries that run on all Intel x86-32
   family CPUs back to 80386, adding "-march=i386" to CFLAGS in
   config.mk is much better than setting NO_GCC_BUILTINS to 1.
   If you want to be extra sure use:
   "-march=i386 -mno-mmx -mno-sse -DNO_GCC_BUILTINS"

   Gcc uses SSE and SSE2 instructions by default for AMD/Intel x86-64.
   Tinkering with "-mno-sse" is not recommended for these machines.  If you
   don't want SSE code, set NO_GCC_BUILTINS to 1.
*/

#if defined(__GNUC__) && !defined(NO_GCC_BUILTINS)
  #define GCCVERSION __GNUC__*100 + __GNUC_MINOR__
#else
  #define GCCVERSION 0
#endif

/* HAVE_GCC_SSE2 means the compiler has GCC builtins to directly access
   SSE/SSE2 features.  This is different from whether the compiler generates
   code that uses these features at all.
*/

#ifndef HAVE_GCC_SSE2
/* GCC 4.1 ostensibly has the feature, but experiments with 4.1.2 and
   4.1.2 in May 2010 exposed an obscure compiler bug and the compiler got
   stock with pamflip_sse.c.  So we say the feature exists only on 4.2
   and up.
*/
#if GCCVERSION >=402 && defined(__SSE__) && defined(__SSE2__)
  #define HAVE_GCC_SSE2 1
#else
  #define HAVE_GCC_SSE2 0
#endif
#endif

#ifndef HAVE_GCC_BITCOUNT
#if GCCVERSION >=304
  #define HAVE_GCC_BITCOUNT 1
  /* Use __builtin_clz(),  __builtin_ctz() (and variants for long)
     to count leading/trailing 0s in int (and long). */
#else
  #define HAVE_GCC_BITCOUNT 0
#endif
#endif

#ifndef HAVE_GCC_BSWAP
#if GCCVERSION >=403
  #define HAVE_GCC_BSWAP 1
  /* Use __builtin_bswap32(), __builtin_bswap64() for endian conversion.
     Available from GCC v 4.3 onward.
     NOTE: On intel CPUs this may produce the bswap operand which is not
     available on 80386. */
#else
  #define HAVE_GCC_BSWAP 0
#endif
#endif



/* CONFIGURE: Some systems seem to need more than standard program linkage
   to get a data (as opposed to function) item out of a library.

   On Windows mingw systems, it seems you have to #include <import_mingw.h>
   and #define EXTERNDATA DLL_IMPORT  .  2001.05.19
*/
#define EXTERNDATA extern

/* only Pnmstitch uses UNREFERENCED_PARAMETER today (and I'm not sure why),
   but it might come in handy some day.
*/
#if (!defined(UNREFERENCED_PARAMETER))
# if (defined(__GNUC__))
#  define UNREFERENCED_PARAMETER(x)
# elif (defined(__USLC__) || defined(_M_XENIX))
#  define UNREFERENCED_PARAMETER(x) ((x)=(x))
# else
#  define UNREFERENCED_PARAMETER(x) (x)
# endif
#endif

#include <unistd.h>  /* Get _LFS_LARGEFILE defined */
#include <sys/types.h>
/* In GNU, _LFS_LARGEFILE means the "off_t" functions (ftello, etc.) are
   available.  In AIX, _AIXVERSION_430 means it's AIX Version 4.3.0 or
   better, which seems to mean the "off_t" functions are available.
*/
#if defined(_LFS_LARGEFILE) || defined(_AIXVERSION_430)
typedef off_t pm_filepos;
#define FTELLO ftello
#define FSEEKO fseeko
#else
typedef long int pm_filepos;
#define FTELLO ftell
#define FSEEKO fseek
#endif

#if defined(_PLAN9)
#define TMPDIR "/tmp"
#else
/* Use POSIX value P_tmpdir from libc */
#define TMPDIR P_tmpdir
#endif

/* Note that if you _don't_ have mkstemp(), you'd better have a safe
   mktemp() or otherwise not be concerned about its unsafety.  On some
   systems, use of mktemp() makes it possible for a hacker to cause a
   Netpbm program to access a file of the hacker's choosing when the
   Netpbm program means to access its own temporary file.
*/
#ifdef __MINGW32__
  #define HAVE_MKSTEMP 0
#else
  #define HAVE_MKSTEMP 1
#endif

typedef int qsort_comparison_fn(const void *, const void *);
    /* A compare function to pass to <stdlib.h>'s qsort() */

#if defined(WIN32) && !defined(__CYGWIN__)
  #define pm_mkdir(dir, perm) _mkdir(dir)
#else
  #define pm_mkdir(dir, perm) mkdir(dir, perm) 
#endif

