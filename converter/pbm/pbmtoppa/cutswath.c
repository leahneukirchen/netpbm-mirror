/* cutswath.c
 * functions to cut a swath of a PBM file for PPA printers
 * Copyright (c) 1998 Tim Norman.  See LICENSE for details.
 * 3-15-98
 *
 * Mar 15, 1998  Jim Peterson  <jspeter@birch.ee.vt.edu>
 *
 *     Structured to accommodate both the HP820/1000, and HP720 series.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mallocvar.h"
#include "pm.h"

#include "ppa.h"
#include "ppapbm.h"
#include "cutswath.h"

extern int Width;
extern int Height;
extern int Pwidth;



int
cut_pbm_swath(pbm_stat *       const pbmP,
              ppa_stat *       const prnP,
              int              const maxlines,
              ppa_sweep_data * const sweepDataP) {
/*----------------------------------------------------------------------------
   sweepDataP->direction must be set already

   Upon successful completion, sweepDataP->image_data and
   sweepDataP->nozzle_data have been set to pointers which this routine
   malloc()'d.

   Upon successful completion, all members of *sweepDataP have been set
   except direction, vertical_pos, and next.

   Returns: 0 if unsuccessful
            1 if successful, but with non-printing result (end of page)
            2 if successful, with printing result
-----------------------------------------------------------------------------*/
    int const shift = prnP->DPI == 300 ? 6 : /* DPI=600 */ 12;

    unsigned char *data, *ppa, *place, *maxplace;
    int pWidth, width8, pWidth8;
    int i, left, right, gotNonblank, numlines;
    int horzpos, hp2;
    ppa_nozzle_data nozzles[2];

    ppa = NULL;

    /* Safeguard against the user freeing these */
    sweepDataP->image_data  = NULL;
    sweepDataP->nozzle_data = NULL;

    /* Read the data from the input file */
    width8 = (pbmP->width + 7) / 8;

    MALLOCARRAY(data, width8 * maxlines);
    if (!data) {
        pm_message("could not malloc data storage");
        return 0;
    }

    /* Ignore lines that are above the upper margin */
    while (pbmP->current_line < prnP->top_margin) {
        if (!pbm_readline(pbmP, data)) {
            pm_message("could not read top margin");
            free(data); data=NULL;
            return 0;
        }
    }

    /* Eat all lines that are below the lower margin */
    if (pbmP->current_line >= Height - prnP->bottom_margin) {
        while (pbmP->current_line < pbmP->height) {
            if (!pbm_readline(pbmP, data)) {
                pm_message("could not clear bottom margin");
                free(data); data=NULL;
                return 0;
            }
        }
        free(data); data=NULL;
        return 1;
    }

    left  = Pwidth - prnP->right_margin / 8;
    right = prnP->left_margin / 8;

    /* Eat all beginning blank lines and then up to maxlines or lower margin */
    gotNonblank = numlines = 0;
    while ((pbmP->current_line < Height-prnP->bottom_margin) &&
           (numlines < maxlines)) {
        if (!pbm_readline(pbmP, data + width8 * numlines)) {
            pm_message("could not read next line");
            free(data); data=NULL;
            return 0;
        }
        if (!gotNonblank) {
            unsigned int j;
            for (j = prnP->left_margin / 8; j < left; ++j) {
                if (data[j]) {
                  left = j;
                  gotNonblank=1;
                  break;
                }
            }
        }
        if (gotNonblank) {
            int newleft, newright;
            unsigned int i;

            /* Find left-most nonblank */
            for (i = prnP->left_margin / 8, newleft = left; i < left; ++i) {
                if (data[width8 * numlines + i]) {
                    newleft = i;
                    break;
                }
            }
            /* Find right-most nonblank */
            for (i = Pwidth - prnP->right_margin / 8 - 1, newright = right;
                 i >= right;
                 --i) {
              if (data[width8 * numlines + i]) {
                  newright = i;
                  break;
              }
            }
            ++numlines;

            if (newright < newleft) {
                pm_message("Ack! newleft=%d, newright=%d, left=%d, right=%d",
                           newleft, newright, left, right);
                free(data); data=NULL;
                return 0;
            }

            /* If the next line might push us over the buffer size, stop here!
               ignore this test for the 720 right now.  Will add better
               size-guessing for compressed data in the near future!
            */
            if (numlines % 2 == 1 && prnP->version != HP720) {
                int l = newleft;
                int r = newright;
                int w;

                --l;
                r += 2;
                l *= 8;
                r *= 8;
                w = r-l;
                w = (w + 7) / 8;

                if ((w + 2 * shift) * numlines > prnP->bufsize) {
                    --numlines;
                    pbm_unreadline(pbmP, data + width8 * numlines);
                    break;
                } else {
                    left  = newleft;
                    right = newright;
                }
            } else {
                left  = newleft;
                right = newright;
            }
        }
    }

    if (!gotNonblank) {
        /* Eat all lines that are below the lower margin */
        if (pbmP->current_line >= Height - prnP->bottom_margin) {
            while (pbmP->current_line < pbmP->height) {
                if (!pbm_readline(pbmP, data)) {
                    pm_message("could not clear bottom margin");
                    free(data); data = NULL;
                    return 0;
                }
            }
            free(data); data = NULL;
            return 1;
        }
        free(data); data = NULL;
        return 0; /* error, since didn't get to lower margin, yet blank */
    }

    /* Make sure numlines is even and >= 2 (b/c we have to pass the printer
       HALF of the number of pins used
    */
    if (numlines == 1) {
        /* There's no way that we only have 1 line and not enough memory, so
           we're safe to increase numlines here.  Also, the bottom margin
           should be > 0 so we have some lines to read
        */
        if (!pbm_readline(pbmP, data + width8 * numlines)) {
            pm_message("could not read next line");
            free(data); data = NULL;
            return 0;
        }
        ++numlines;
    }
    if (numlines % 2 == 1) {
      /* Decrease instead of increasing so we don't max out the buffer */
        --numlines;
        pbm_unreadline(pbmP, data + width8 * numlines);
    }

    sweepDataP->vertical_pos = pbmP->current_line;

    /* Change sweep params */
    left  -= 1;
    right += 2;
    left  *= 8;
    right *= 8;

    /* Construct the sweep data */
    pWidth = right - left;
    pWidth8 = (pWidth + 7) / 8;

    MALLOCARRAY(ppa, (pWidth8 + 2 * shift) * numlines);
    if (!ppa) {
        pm_message("could not malloc ppa storage");
        free(data); data = NULL;
        return 0;
    }

    place = ppa;

    /* Place 0's in the first 12 columns */
    memset(place, 0, numlines/2 * shift);
    place += numlines/2 * shift;

    if (sweepDataP->direction == right_to_left) { /* right-to-left */
        int i;

        for (i = pWidth8 + shift - 1; i >= 0; --i) {
            if (i >= shift) {
                unsigned int j;

                for (j = 0; j < numlines/2; ++j)
                    *place++ = data[j * 2 * width8 + i + left / 8 - shift];
            } else {
                memset(place, 0, numlines/2);
                place += numlines/2;
            }

            if (i < pWidth8) {
                unsigned int j;

                for (j = 0; j < numlines/2; ++j)
                    *place++ = data[(j * 2 + 1) * width8 + i + left / 8];
            } else {
                memset(place, 0, numlines/2);
                place += numlines/2;
            }
        }
    } else {
        /* sweep_data->direction == left_to_right */
        unsigned int i;
        for (i = 0; i < pWidth8 + shift; ++i) {
            if (i < pWidth8) {
                unsigned int j;
                for (j = 0; j < numlines/2; ++j)
                    *place++ = data[(j * 2 + 1) * width8 + i + left / 8];
            } else {
                memset(place, 0, numlines/2);
                place += numlines/2;
            }

            if (i >= shift) {
                unsigned int j;
                for (j = 0; j < numlines/2; ++j)
                    *place++ = data[j * 2 * width8 + i + left / 8 - shift];
            } else {
                memset(place, 0, numlines/2);
                place += numlines/2;
            }
        }
    }

    /* Done with data */
    free(data); data=NULL;

    /* Place 0's in the last 12 columns */
    memset(place, 0, numlines/2 * shift);
    place += numlines/2 * shift;
    maxplace = place;

    /* Create sweep data */
    sweepDataP->image_data = ppa;
    sweepDataP->data_size  = maxplace-ppa;
    sweepDataP->in_color   = False;

    /*
      horzpos = left*600/prn->DPI + (sweep_data->direction==left_to_right ? 0*600/prn->DPI : 0);
    */
    horzpos = left * 600 / prnP->DPI;

    hp2 = horzpos + (pWidth8 + 2 * shift) * 8 * 600 /prnP->DPI;

    sweepDataP->left_margin  = horzpos;
    sweepDataP->right_margin = hp2 + prnP->marg_diff;

    for (i = 0; i < 2; ++i) {
        nozzles[i].DPI = prnP->DPI;

        nozzles[i].pins_used_d2 = numlines/2;
        nozzles[i].unused_pins_p1 = 301 - numlines;
        nozzles[i].first_pin = 1;

        if (i == 0) {
            nozzles[i].left_margin  = horzpos + prnP->marg_diff;
            nozzles[i].right_margin = hp2 + prnP->marg_diff;
            if (sweepDataP->direction == right_to_left)  /* 0 */

                nozzles[i].nozzle_delay = prnP->right_to_left_delay[0];
            else  /* 6 */
                nozzles[i].nozzle_delay = prnP->left_to_right_delay[0];
        } else {
            nozzles[i].left_margin  = horzpos;
            nozzles[i].right_margin = hp2;

            if (sweepDataP->direction == right_to_left)  /* 2 */
                nozzles[i].nozzle_delay = prnP->right_to_left_delay[1];
            else  /* 0 */
                nozzles[i].nozzle_delay = prnP->left_to_right_delay[1];
        }
    }

    sweepDataP->nozzle_data_size = 2;
    MALLOCARRAY_NOFAIL(sweepDataP->nozzle_data, 2);
    sweepDataP->nozzle_data[0] = nozzles[0];
    sweepDataP->nozzle_data[1] = nozzles[1];

    return 2;
}



