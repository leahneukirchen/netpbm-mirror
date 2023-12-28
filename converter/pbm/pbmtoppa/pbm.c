/* pbm.c
 * code for reading the header of an ASCII PBM file
 * Copyright (c) 1998 Tim Norman.  See LICENSE for details
 * 2-25-98
 *
 * Mar 18, 1998  Jim Peterson  <jspeter@birch.ee.vt.edu>
 *
 *     Restructured to encapsulate more of the PBM handling.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "pm.h"
#include "nstring.h"

#include "ppapbm.h"



int
make_pbm_stat(pbm_stat * const pbmStatP,
              FILE *     const ifP) {

    char line[1024];
    char * rc;
    int retval;

    pbmStatP->fptr         = ifP;
    pbmStatP->version      = none;
    pbmStatP->current_line = 0;
    pbmStatP->unread       = 0;

    rc = fgets(line, 1024, ifP);
    if (rc == NULL)
        retval = 0;
    else {
        line[strlen(line)-1] = 0;

        if (streq(line,"P1"))
            pbmStatP->version=P1;
        if (streq(line,"P4"))
            pbmStatP->version=P4;

        if (pbmStatP->version == none) {
            pm_message("unknown PBM magic '%s'", line);
            retval = 0;
        } else {
            do {
                char * rc;
                rc = fgets(line, 1024, ifP);
                if (rc == NULL)
                    return 0;
            } while (line[0] == '#');
            {
                int rc;
                rc = sscanf(line, "%d %d",
                            &pbmStatP->width, &pbmStatP->height);
                if (rc != 2)
                    retval = 0;
                else {
                    if (pbmStatP->width < 0) {
                        pm_message("Image has negative width");
                        retval = 0;
                    } else if (pbmStatP->width > INT_MAX/2 - 10) {
                        pm_message("Uncomputably large width: %d",
                                   pbmStatP->width);
                        retval = 0;
                    } else if (pbmStatP->height < 0) {
                        pm_message("Image has negative height");
                        retval = 0;
                    } else if (pbmStatP->height > INT_MAX/2 - 10) {
                        pm_message("Uncomputably large height: %d",
                                   pbmStatP->height);
                        retval = 0;
                    } else
                        retval = 1;
                }
            }
        }
    }
    return retval;
}



static int
getbytes(FILE *          const ifP,
         unsigned int    const width,
         unsigned char * const data) {

    unsigned char mask;
    unsigned char acc;
    unsigned char * place;
    unsigned int num;
    int retval;

    if (width == 0)
        retval = 0;
    else {
        for (mask = 0x80, acc = 0, num = 0, place = data; num < width; ) {
            switch (getc(ifP)) {
            case EOF:
                return 0;
            case '1':
                acc |= mask;
                /* fall through */
            case '0':
                mask >>= 1;
                ++num;
                if (mask == 0x00) { /* if (num % 8 == 0) */
                    *place++ = acc;
                    acc = 0;
                    mask = 0x80;
                }
            }
        }
        if (width % 8 != 0)
            *place = acc;

        retval = 1;
    }
    return retval;
}



int
pbm_readline(pbm_stat *      const pbmStatP,
             unsigned char * const data) {
/*----------------------------------------------------------------------------
  Read a single line into data which must be at least (pbmStatP->width+7)/8
  bytes of storage.
-----------------------------------------------------------------------------*/
    int retval;

    if (pbmStatP->current_line >= pbmStatP->height)
        retval = 0;
    else {
        if (pbmStatP->unread) {
            memcpy(data, pbmStatP->revdata, (pbmStatP->width+7)/8);
            ++pbmStatP->current_line;
            pbmStatP->unread = 0;
            free(pbmStatP->revdata);
            pbmStatP->revdata = NULL;
            retval = 1;
        } else {
            switch (pbmStatP->version) {
            case P1:
                if (getbytes(pbmStatP->fptr, pbmStatP->width, data)) {
                    pbmStatP->current_line++;
                    retval = 1;
                } else
                    retval = 0;
                break;
            case P4: {
                int tmp, tmp2;
                tmp = (pbmStatP->width+7)/8;
                tmp2 = fread(data,1,tmp,pbmStatP->fptr);
                if (tmp2 == tmp) {
                    ++pbmStatP->current_line;
                    retval = 1;
                } else {
                    pm_message("error reading line data (%d)", tmp2);
                    retval = 0;
                }
            } break;

            default:
                pm_message("unknown PBM version");
                retval = 0;
            }
        }
    }
    return retval;
}



void
pbm_unreadline(pbm_stat * const pbmStatP,
               void *     const data) {
/*----------------------------------------------------------------------------
  Push a line back into the buffer; we read too much!
-----------------------------------------------------------------------------*/
    /* can store only one line in the unread buffer */

    if (!pbmStatP->unread) {
        pbmStatP->unread = 1;
        pbmStatP->revdata = malloc ((pbmStatP->width+7)/8);
        memcpy(pbmStatP->revdata, data, (pbmStatP->width+7)/8);
        --pbmStatP->current_line;
    }
}



