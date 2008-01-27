/* libpbm1.c - pbm utility library part 1
**
** Copyright (C) 1988 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

/* See pmfileio.c for the complicated explanation of this 32/64 bit file
   offset stuff.
*/
#define _FILE_OFFSET_BITS 64
#define _LARGE_FILES  

#include <stdio.h>

#include "mallocvar.h"
#include "shhopt.h"
#include "pbm.h"



bit *
pbm_allocrow(unsigned int const cols) {

    bit * bitrow;

    MALLOCARRAY(bitrow, cols);

    if (bitrow == NULL)
        pm_error("Unable to allocate space for a %u-column bit row", cols);

    return bitrow;
}



void
pbm_init(int *   const argcP,
         char ** const argv) {

    pm_proginit(argcP, (const char **)argv);
}



void
pbm_nextimage(FILE *file, int * const eofP) {
    pm_nextimage(file, eofP);
}



void
pbm_check(FILE * file, const enum pm_check_type check_type, 
          const int format, const int cols, const int rows,
          enum pm_check_code * const retval_p) {

    if (rows < 0)
        pm_error("Invalid number of rows passed to pbm_check(): %d", rows);
    if (cols < 0)
        pm_error("Invalid number of columns passed to pbm_check(): %d", cols);
    
    if (check_type != PM_CHECK_BASIC) {
        if (retval_p) *retval_p = PM_CHECK_UNKNOWN_TYPE;
    } else if (format != RPBM_FORMAT) {
        if (retval_p) *retval_p = PM_CHECK_UNCHECKABLE;
    } else {        
        pm_filepos const bytes_per_row = (cols+7)/8;
        pm_filepos const need_raster_size = rows * bytes_per_row;
#ifdef LARGEFILEDEBUG
        pm_message("pm_filepos passed to pm_check() is %u bytes",
                   sizeof(pm_filepos));
#endif
        pm_check(file, check_type, need_raster_size, retval_p);
    }
}

