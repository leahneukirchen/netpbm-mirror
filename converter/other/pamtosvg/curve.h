/* curve.h: data structures for the conversion from pixels to splines. */

#ifndef CURVE_H
#define CURVE_H

#include "autotrace.h"
#include "point.h"

/* We are simultaneously manipulating two different representations of
   the same outline: one based on (x,y) positions in the plane, and one
   based on parametric splines.  (We are trying to match the latter to
   the former.)  Although the original (x,y)'s are pixel positions,
   i.e., integers, after filtering they are reals.
*/

typedef struct {
/*----------------------------------------------------------------------------
   A point in a curve (i.e. a component of a curve).
-----------------------------------------------------------------------------*/
    Point coord;
        /* Location in space of the point */
    float distance;
        /* Distance point is along the curve, as a fraction of the
           curve length.  This is invalid until after someone has called
           curve_updateDistance() on the curve.
        */
} CurvePoint;



typedef struct Curve {
/*----------------------------------------------------------------------------
  An ordered list of contiguous points in the raster, with no corners
  in it.  I.e. something that could reasonably be fit to a spline.
-----------------------------------------------------------------------------*/
    CurvePoint *   pointList;
        /* Array of the points in the curve.  Malloc'ed.  Size is 'length'.
           if 'length' is zero, this is meaningless and no memory is
           allocated.
        */
    unsigned int   length;
        /* Number of points in the curve */
    bool           cyclic;
       /* The curve is cyclic, i.e. it didn't have any corners, after all, so
          the last point is adjacent to the first.
       */

    /* 'previous' and 'next' links are for the doubly linked list which is
       a chain of all curves in an outline.  The chain is a cycle for a
       closed outline and linear for an open outline.
    */
    struct Curve * previous;
    struct Curve * next;
} Curve;

#define CURVE_POINT(c, n) ((c)->pointList[n].coord)
#define LAST_CURVE_POINT(c) ((c)->pointList[(c)->length-1].coord)
#define CURVE_DIST(c, n) ((c)->pointList[n].distance)
#define LAST_CURVE_DIST(c) ((c)->pointList[(c)->length-1].distance)
#define CURVE_LENGTH(c)  ((c)->length)

#define CURVE_CYCLIC(c)  ((c)->cyclic)

/* If the curve is cyclic, the next and previous points should wrap
   around; otherwise, if we get to the end, we return CURVE_LENGTH and
   -1, respectively.  */
#define CURVE_NEXT(c, n)                                                \
  ((n) + 1 >= CURVE_LENGTH (c)                                          \
  ? CURVE_CYCLIC (c) ? ((n) + 1) % CURVE_LENGTH (c) : CURVE_LENGTH (c)  \
  : (n) + 1)
#define CURVE_PREV(c, n)                                                \
  ((signed int) (n) - 1 < 0                                                     \
  ? CURVE_CYCLIC (c) ? (signed int) CURVE_LENGTH (c) + (signed int) (n) - 1 : -1\
  : (signed int) (n) - 1)

#define PREVIOUS_CURVE(c) ((c)->previous)
#define NEXT_CURVE(c) ((c)->next)


Curve *
curve_new(void);

Curve *
curve_copyMost(Curve * const curveP);

void
curve_move(Curve * const dstP,
           Curve * const srcP);

void
curve_free(Curve * const curveP);

void
curve_appendPoint(Curve * const curveP,
                  Point   const coord);

void
curve_appendPixel(Curve *       const curveP,
                  pm_pixelcoord const p);

void
curve_setDistance(Curve * const curveP);

void
curve_log(Curve * const curveP,
          bool    const print_t);
void
curve_logEntire(Curve * const curveP);

typedef struct {
/*----------------------------------------------------------------------------
   An ordered list of contiguous curves of a particular color.
-----------------------------------------------------------------------------*/
    Curve ** data;
        /* data[i] is the handle of the ith curve in the list */
    unsigned length;
    bool     clockwise;
    pixel    color;
    bool     open;
        /* The curve list does not form a closed shape;  i.e. the last
           curve doesn't end where the first one starts.
        */
} CurveList;

/* Number of curves in the list.  */
#define CURVE_LIST_LENGTH(c_l)  ((c_l).length)
#define CURVE_LIST_EMPTY(c_l) ((c_l).length == 0)

/* Access the individual curves.  */
#define CURVE_LIST_ELT(c_l, n) ((c_l).data[n])
#define LAST_CURVE_LIST_ELT(c_l) ((c_l).data[CURVE_LIST_LENGTH (c_l) - 1])

/* Says whether the outline that this curve list represents moves
   clockwise or counterclockwise.  */
#define CURVE_LIST_CLOCKWISE(c_l) ((c_l).clockwise)


CurveList
curve_newList(void);

void
curve_freeList(CurveList * const curveListP);

void
curve_appendList(CurveList * const curveListP,
                 Curve *     const curveP);

/* And a character is a list of outlines.  I named this
   `curve_list_array_type' because `curve_list_list_type' seemed pretty
   monstrous.  */
typedef struct {
  CurveList * data;
  unsigned    length;
} CurveListArray;

/* Turns out we can use the same definitions for lists of lists as for
   just lists.  But we define the usual names, just in case.  */
#define CURVE_LIST_ARRAY_LENGTH CURVE_LIST_LENGTH
#define CURVE_LIST_ARRAY_ELT CURVE_LIST_ELT
#define LAST_CURVE_LIST_ARRAY_ELT LAST_CURVE_LIST_ELT

CurveListArray
curve_newListArray(void);

void
curve_freeListArray(const CurveListArray * const curveListArrayP,
                    at_progress_func             notify_progress,
                    void *                 const client_data);

void
curve_appendArray(CurveListArray * const curveListArrayP,
                  CurveList        const curveList);

#endif
