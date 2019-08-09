/*
 * This software is copyrighted as noted below.  It may be freely copied,
 * modified, and redistributed, provided that the copyright notice is
 * preserved on all copies.
 *
 * There is no warranty or other guarantee of fitness for this software,
 * it is provided solely "as is".  Bug reports or fixes may be sent
 * to the author, who may or may not act on them as he desires.
 *
 * You may not include this software in a program or other software product
 * without supplying the source, or without informing the end-user that the
 * source is available for no extra charge.
 *
 * If you modify this software, you should include a notice giving the
 * name of the person performing the modification, the date of modification,
 * and the reason for such modification.
 */
/*
 * rle_hdr.c - Functions to manipulate rle_hdr structures.
 *
 * Author:  Spencer W. Thomas
 *      EECS Dept.
 *      University of Michigan
 * Date:    Mon May 20 1991
 * Copyright (c) 1991, University of Michigan
 */

#include <string.h>

#include "nstring.h"
#include "mallocvar.h"

#include "rle_config.h"
#include "rle.h"



void
rle_names(rle_hdr *    const hdrP,
          const char * const pgmname,
          const char * const fname,
          int          const imgNum) {
/*---------------------------------------------------------------------------- 
 * Load program and file names into header.
 * Inputs:
 *  hdrP:      Header to modify.
 *  pgmname:   The program name.
 *  fname:     The file name.
 *  imgNum:    Number of the image within the file.
 * Outputs:
 *  *hdrP:     Modified header.
-----------------------------------------------------------------------------*/

    /* Algorithm:
       If values previously filled in (by testing is_init field),
       free them.  Make copies of file name and program name,
       modifying file name for standard i/o.  Set is_init field.
    */
    const char * newFname;
    const char * newPgmname;

    /* Mark as filled in. */
    hdrP->is_init = RLE_INIT_MAGIC;

    /* Default file name for stdin/stdout. */
    if (!fname || streq(fname, "-") || strlen(fname) == 0)
        newFname = "Standard I/O";
    else
        newFname = fname;

    if (pgmname)
        newPgmname = pgmname;
    else
        newPgmname = rle_dflt_hdr.cmd;

    /* Fill in with copies of the strings. */
    if (hdrP->cmd != newPgmname)
        hdrP->cmd = pm_strdup(newPgmname);

    if (hdrP->file_name != newFname)
        hdrP->cmd = pm_strdup(newFname);

    hdrP->img_num = imgNum;
}



/* Used by rle_hdr_cp and rle_hdr_init to avoid recursion loops. */
static int noRecurse = 0;



rle_hdr *
rle_hdr_cp(rle_hdr * const fromHdrP,
           rle_hdr * const toHdrArgP) {
/*----------------------------------------------------------------------------
 * Make a "safe" copy of a rle_hdr structure.
 * Inputs:
 *  *fromHdrP:   Header to be copied.
 * Outputs:
 *  *toHdrPd:    Copy of from_hdr, with all memory referred to
 *               by pointers copied.  Also returned as function
 *               value.  If NULL, a static header is used.
 * Assumptions:
 *  It is safe to call rle_hdr_init on *toHdrP.
-----------------------------------------------------------------------------*/
    /* Algorithm:
       Initialize *toHdrP, copy *fromHdrP to it, then copy the memory
       referred to by all non-null pointers.
    */
    static rle_hdr dfltHdr;
    rle_hdr * toHdrP;
    const char * cmd;
    const char * file;
    unsigned int num;

    /* Save command, file name, and image number if already initialized. */
    if (toHdrArgP &&  toHdrArgP->is_init == RLE_INIT_MAGIC) {
        cmd  = toHdrArgP->cmd;
        file = toHdrArgP->file_name;
        num  = toHdrArgP->img_num;
    } else {
        cmd = file = NULL;
        num = 0;
    }

    if (!noRecurse) {
        ++noRecurse;
        rle_hdr_init(toHdrArgP);
        --noRecurse;
    }

    toHdrP = toHdrArgP ? toHdrArgP : &dfltHdr;

    *toHdrP = *fromHdrP;

    if (toHdrP->bg_color) {
        unsigned int i;

        MALLOCARRAY(toHdrP->bg_color, toHdrP->ncolors);
        if (!toHdrP->bg_color)
            pm_error("Failed to allocate array for %u background colors",
                     toHdrP->ncolors);
        for (i = 0; i < toHdrP->ncolors; ++i)
            toHdrP->bg_color[i] = fromHdrP->bg_color[i];
    }

    if (toHdrP->cmap) {
        size_t const size =
            toHdrP->ncmap * (1 << toHdrP->cmaplen) * sizeof(rle_map);
        toHdrP->cmap = malloc(size);
        if (!toHdrP->cmap)
            pm_error("Failed to allocate memory for %u color maps "
                     "of length %u", toHdrP->ncmap, 1 << toHdrP->cmaplen);
        memcpy(toHdrP->cmap, fromHdrP->cmap, size);
    }

    /* Only copy array of pointers, as the original comment memory
     * never gets overwritten.
     */
    if (toHdrP->comments) {
        unsigned int  size;
        const char ** cp;

        /* Count the comments. */
        for (cp = toHdrP->comments, size = 0; *cp; ++cp)
            ++size;

        /* Check if there are really any comments. */
        if (size > 0) {
            ++size;     /* Copy the NULL pointer, too. */
            size *= sizeof(char *);
            toHdrP->comments = malloc(size);
            if (!toHdrP->comments)
                pm_error("Failed to allocation %u bytes for comments", size);
            memcpy(toHdrP->comments, fromHdrP->comments, size);
        } else
            toHdrP->comments = NULL;    /* Blow off empty comment list. */
    }

    /* Restore the names to their original values. */
    toHdrP->cmd       = cmd;
    toHdrP->file_name = file;

    /* Lines above mean nothing much happens if cmd and file are != NULL. */
    rle_names(toHdrP, toHdrP->cmd, toHdrP->file_name, num);

    return toHdrP;
}



void
rle_hdr_clear(rle_hdr * const hdrP) {
/*----------------------------------------------------------------------------
 * Clear out the allocated memory pieces of a header.
 *
 * This routine is intended to be used internally by the library, to
 * clear a header before putting new data into it.  It clears all the
 * fields that would be set by reading in a new image header.
 * Therefore, it does not clear the program and file names.
 *
 * Inputs:
 *  hdrP:    To be cleared.
 * Outputs:
 *  *hdrP:   After clearing.
 * Assumptions:
 *  If is_init field is RLE_INIT_MAGIC, the header has been
 *  properly initialized.  This will fail every 2^(-32) times, on
 *  average.
-----------------------------------------------------------------------------*/
    /* Algorithm:
       Free memory and set to zero all pointers, except program and
       file name.
    */

    /* Try to free memory.  Assume if is_init is properly set that this
     * header has been previously initialized, therefore it is safe to
     * free memory.
     */
    if (hdrP && hdrP->is_init == RLE_INIT_MAGIC) {
        if (hdrP->bg_color )
            free(hdrP->bg_color);
        hdrP->bg_color = NULL;
        if (hdrP->cmap )
            free(hdrP->cmap);
        hdrP->cmap = NULL;
        /* Unfortunately, we don't know how to free the comment memory. */
        if (hdrP->comments)
            free(hdrP->comments);
        hdrP->comments = NULL;
    }
}



rle_hdr *
rle_hdr_init(rle_hdr * const hdrP) {
/*----------------------------------------------------------------------------
 * Initialize a rle_hdr structure.
 * Inputs:
 *  hdrP:    Header to be initialized.
 * Outputs:
 *  *hdrP:   Initialized header.
 * Assumptions:
 *  If hdrP->is_init is RLE_INIT_MAGIC, the header has been
 *  previously initialized.
 *  If the_hdr is a copy of another rle_hdr structure, the copy
 *  was made with rle_hdr_cp.
-----------------------------------------------------------------------------*/
    /* Algorithm:
       Fill in fields of rle_dflt_hdr that could not be set by the loader
       If the_hdr is rle_dflt_hdr, do nothing else
       Else:
         If hdrP is NULL, return a copy of rle_dflt_hdr in static storage
         If hdrP->is_init is RLE_INIT_MAGIC, free all memory
            pointed to by non-null pointers.
         If this is a recursive call to rle_hdr_init, clear *hdrP and
           return hdrP.
         Else make a copy of rle_dflt_hdr and return its address.  Make the
           copy in static storage if hdrP is NULL, and in *hdrP otherwise.
    */
    rle_hdr * retval;

    rle_dflt_hdr.rle_file = stdout;

    /* The rest of rle_dflt_hdr is set by the loader's data initialization */

    if (hdrP == &rle_dflt_hdr)
        retval = hdrP;
    else {
        rle_hdr_clear(hdrP);

        /* Call rle_hdr_cp only if not called from there. */
        if (!noRecurse) {
            ++noRecurse;
            retval = rle_hdr_cp(&rle_dflt_hdr, hdrP);
            --noRecurse;
        } else
            retval = hdrP;
    }
    return retval;
}



