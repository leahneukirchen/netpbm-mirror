/* curve.c: operations on the lists of pixels and lists of curves.

   The code was partially derived from limn.

   Copyright (C) 1992 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include "mallocvar.h"

#include "logreport.h"
#include "curve.h"


static Point
realCoordFromInt(pm_pixelcoord const int_coord) {
/*----------------------------------------------------------------------------
  Turn an integer point into a real one.
-----------------------------------------------------------------------------*/
    Point real_coord;

    real_coord.x = int_coord.col;
    real_coord.y = int_coord.row;
    real_coord.z = 0.0;

    return real_coord;
}



Curve *
curve_new(void) {
/*----------------------------------------------------------------------------
  A new, entirely empty curve.
-----------------------------------------------------------------------------*/
    Curve * curveP;

    MALLOCVAR_NOFAIL(curveP);

    curveP->pointList       = NULL;
    CURVE_LENGTH(curveP)    = 0;
    CURVE_CYCLIC(curveP)    = false;
    PREVIOUS_CURVE(curveP)  = NULL;
    NEXT_CURVE(curveP)      = NULL;

    return curveP;
}


Curve *
curve_copyMost(Curve * const oldCurveP) {
/*----------------------------------------------------------------------------
  New curve that is the same as *curveP, except without any points.

  Don't copy the points or tangents, but copy everything else.
-----------------------------------------------------------------------------*/
    Curve * curveP = curve_new();

    CURVE_CYCLIC(curveP)   = CURVE_CYCLIC(oldCurveP);
    PREVIOUS_CURVE(curveP) = PREVIOUS_CURVE(oldCurveP);
    NEXT_CURVE(curveP)     = NEXT_CURVE(oldCurveP);

    return curveP;
}

void
curve_move(Curve * const dstP,
           Curve * const srcP) {
/*----------------------------------------------------------------------------
  Move ownership of dynamically allocated memory from source to destination;
  destroy source.
-----------------------------------------------------------------------------*/
   if (CURVE_LENGTH(dstP) > 0)
       free(dstP->pointList);

   *dstP = *srcP;

   free(srcP);
}



void
curve_free(Curve * const curveP) {

    /* The length of CURVE will be zero if we ended up not being able to fit
       it (which in turn implies a problem elsewhere in the program, but at
       any rate, we shouldn't try here to free the nonexistent curve).
    */

     if (CURVE_LENGTH(curveP) > 0)
         free(curveP->pointList);

     free(curveP);
}



void
curve_appendPoint(Curve * const curveP,
                  Point   const coord) {
/*----------------------------------------------------------------------------
  Like `append_pixel', for a point in real coordinates.
-----------------------------------------------------------------------------*/
    CURVE_LENGTH(curveP)++;
    REALLOCARRAY_NOFAIL(curveP->pointList, CURVE_LENGTH(curveP));
    LAST_CURVE_POINT(curveP) = coord;
    /* The t value does not need to be set.  */
}



void
curve_appendPixel(Curve *       const curveP,
                  pm_pixelcoord const coord) {
/*----------------------------------------------------------------------------
  Append the point 'coord' to the end of *curveP's list.
-----------------------------------------------------------------------------*/
    curve_appendPoint(curveP, realCoordFromInt(coord));
}



#define NUM_TO_PRINT 3

#define LOG_CURVE_POINT(c, p, printDistance) \
    do  \
    {                                   \
      LOG2 ("(%.3f,%.3f)", CURVE_POINT (c, p).x, CURVE_POINT (c, p).y); \
      if (printDistance) \
        LOG1 ("/%.2f", CURVE_DIST(c, p)); \
    } \
  while (0)



void
curve_log(Curve * const curveP,
          bool    const printDistance) {
/*----------------------------------------------------------------------------
  Print a curve in human-readable form.  It turns out we never care
  about most of the points on the curve, and so it is pointless to
  print them all out umpteen times.  What matters is that we have some
  from the end and some from the beginning.
-----------------------------------------------------------------------------*/
    if (!log_file)
        return;

    LOG1("curve id = %lx:\n", (unsigned long) curveP);
    LOG1("  length = %u.\n", CURVE_LENGTH(curveP));
    if (CURVE_CYCLIC(curveP))
        LOG("  cyclic.\n");

    LOG("  ");

    /* If the curve is short enough, don't use ellipses.  */
    if (CURVE_LENGTH(curveP) <= NUM_TO_PRINT * 2) {
        unsigned int thisPoint;

        for (thisPoint = 0; thisPoint < CURVE_LENGTH(curveP); ++thisPoint) {
            LOG_CURVE_POINT(curveP, thisPoint, printDistance);
            LOG(" ");

            if (thisPoint != CURVE_LENGTH(curveP) - 1
                && (thisPoint + 1) % NUM_TO_PRINT == 0)
                LOG("\n  ");
        }
    } else {
        unsigned int thisPoint;
        for (thisPoint = 0;
             thisPoint < NUM_TO_PRINT && thisPoint < CURVE_LENGTH(curveP);
             ++thisPoint) {
            LOG_CURVE_POINT(curveP, thisPoint, printDistance);
            LOG(" ");
        }

        LOG("...\n   ...");

        for (thisPoint = CURVE_LENGTH(curveP) - NUM_TO_PRINT;
             thisPoint < CURVE_LENGTH(curveP);
             ++thisPoint) {
            LOG(" ");
            LOG_CURVE_POINT(curveP, thisPoint, printDistance);
        }
    }
    LOG(".\n");
}



void
curve_logEntire(Curve * const curveP) {
/*----------------------------------------------------------------------------
  Like `log_curve', but write the whole thing.
-----------------------------------------------------------------------------*/
    unsigned int thisPoint;

    if (!log_file)
        return;

    LOG1("curve id = %lx:\n", (unsigned long) curveP);
    LOG1("  length = %u.\n", CURVE_LENGTH(curveP));
    if (CURVE_CYCLIC(curveP))
        LOG("  cyclic.\n");

    LOG(" ");

    for (thisPoint = 0; thisPoint < CURVE_LENGTH(curveP); ++thisPoint) {
        LOG(" ");
        LOG_CURVE_POINT(curveP, thisPoint, true);
        /* Compiler warning `Condition is always true' can be ignored */
    }

    LOG(".\n");
}




CurveList
curve_newList(void) {
/*----------------------------------------------------------------------------
  A new initialized but empty curve list.
-----------------------------------------------------------------------------*/
    CurveList curveList;

    curveList.length = 0;
    curveList.data   = NULL;

    return curveList;
}



void
curve_freeList(CurveList * const curveListP) {
/*----------------------------------------------------------------------------
  Free curve list and all the curves it contains.
-----------------------------------------------------------------------------*/
    unsigned int thisCurve;

    for (thisCurve = 0; thisCurve < curveListP->length; ++thisCurve)
        curve_free(curveListP->data[thisCurve]);

    /* If the character was empty, it won't have any curves.  */
    if (curveListP->data != NULL)
        free(curveListP->data);
}



void
curve_appendList(CurveList * const curveListP,
                 Curve *     const curveP) {
/*----------------------------------------------------------------------------
  Add an element to a curve list.
-----------------------------------------------------------------------------*/
    ++curveListP->length;
    REALLOCARRAY_NOFAIL(curveListP->data, curveListP->length);
    curveListP->data[curveListP->length - 1] = curveP;
}



CurveListArray
curve_newListArray(void) {
/*----------------------------------------------------------------------------
    An initialized but empty curve list array.
-----------------------------------------------------------------------------*/
    CurveListArray curveListArray;

    CURVE_LIST_ARRAY_LENGTH(curveListArray) = 0;
    curveListArray.data = NULL;

    return curveListArray;
}



void
curve_freeListArray(const CurveListArray * const curveListArrayP,
                    at_progress_func             notify_progress,
                    void *                 const clientData) {
/*----------------------------------------------------------------------------
  Free all the curve lists curveListArray contains.
-----------------------------------------------------------------------------*/
    unsigned int thisList;

    for (thisList = 0;
         thisList < CURVE_LIST_ARRAY_LENGTH(*curveListArrayP);
         ++thisList) {

      if (notify_progress)
          notify_progress(((float)thisList)/
                          (CURVE_LIST_ARRAY_LENGTH(*curveListArrayP) *
                           (float)3.0)+(float)0.666 ,
                          clientData);
      curve_freeList(&CURVE_LIST_ARRAY_ELT(*curveListArrayP, thisList));
    }

    /* If the character was empty, it won't have any curves.  */
    if (curveListArrayP->data)
        free(curveListArrayP->data);
}



void
curve_appendArray(CurveListArray * const curveListArrayP,
                  CurveList        const curveList) {
/*----------------------------------------------------------------------------
  Add an element to *curveListArrayP.
-----------------------------------------------------------------------------*/
    CURVE_LIST_ARRAY_LENGTH(*curveListArrayP)++;
    REALLOCARRAY_NOFAIL(curveListArrayP->data,
                        CURVE_LIST_ARRAY_LENGTH(*curveListArrayP));
    LAST_CURVE_LIST_ARRAY_ELT(*curveListArrayP) = curveList;
}



