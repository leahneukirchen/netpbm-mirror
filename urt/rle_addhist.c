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
 * rle_addhist.c - Add to the HISTORY comment in header
 *
 * Author:  Andrew Marriott.
 *      School of Computer Science
 *      Curtin University of Technology
 * Date:    Mon Sept 10 1988
 * Copyright (c) 1988, Curtin University of Technology
 */

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "netpbm/mallocvar.h"

#include "rle.h"



static unsigned int
newCommentLen(const char *  const histoire,
              const char *  const old,
              const char ** const argv,
              const char *  const timedate,
              const char *  const padding) {

    unsigned int length;
    unsigned int i;

    length = 0;  /* initial value */

    /* Add length of each arg plus space. */

    for (i = 0; argv[i]; ++i) {
        size_t const thisArgLen = strlen(argv[i]);
        if (thisArgLen < UINT_MAX - length - 100) {
            length += thisArgLen;
            length += 1;  /* For the space */
        }
    }

    /* Add length of date and time in ASCII. */
    length += strlen(timedate);

    /* Add length of padding, "on ", and length of history name plus "="*/
    length += strlen(padding) + 3 + strlen(histoire) + 1;

    if (old && *old)
        length += strlen(old);       /* add length if there. */

    ++length;     /* Add size of terminating NUL. */

    return length;
}



void
rle_addhist(const char ** const argv,
            rle_hdr *     const inHdrP,
            rle_hdr *     const outHdrP) {
/*----------------------------------------------------------------------------
  Put a history comment into the header struct.
  Inputs:
   argv:        Command line history to add to comments.
   *inHdrP:     Incoming header struct to use.
  Outputs:
   *outHdrP:    Outgoing header struct to add to.
  Assumptions:
   If no incoming struct then value is NULL.
  Algorithm:
   Calculate length of all comment strings, malloc and then set via
   rle_putcom.
  If we run out of memory, don't put the history comment in.
-----------------------------------------------------------------------------*/
    if (!getenv("NO_ADD_RLE_HISTORY")) {
        const char * const histoire = "HISTORY";
        const char * const padding = "\t";

        unsigned int length;
            /* length of combined comment - the history comment we are adding
               and any history comment that is already there (to which we
               append)
            */
        time_t  nowTime;
        /* padding must give number of characters in histoire   */
        /*     plus one for "="                 */
        const char * timedate;
        const char * old;
        char * newc;

        if (inHdrP) /* if we are interested in the old comments... */
            old = rle_getcom(histoire, inHdrP);     /* get old comment. */
        else
            old = NULL;

        time(&nowTime);
        timedate = ctime(&nowTime);

        length = newCommentLen(histoire, old, argv, timedate, padding);

        MALLOCARRAY(newc, length);

        if (newc) {
            unsigned int i;

            strcpy(newc, histoire);
            strcat(newc, "=");

            if (old)
                strcat(newc, old); /* add old string if there. */

            for (i = 0; argv[i]; ++i) {
                strcat(newc, argv[i]);
                strcat(newc, " ");
            }
            strcat(newc, "on ");
            strcat(newc, timedate);         /* \n supplied by 'ctime'. */
            strcat(newc, padding);          /* to line up multiple histories.*/

            rle_putcom(newc, outHdrP);
                /* Creates reference to 'newc', may destroy reference to
                   previous comment memory, which will thereby leak.
                */
        }
    }
}



