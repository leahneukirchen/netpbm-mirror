/* 
**
** This library module contains the ppmdraw routines.
**
** Copyright (C) 1989, 1991 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
** 
** The character drawing routines are by John Walker
** Copyright (C) 1994 by John Walker, kelvin@fourmilab.ch
*/

#include <assert.h>
#include <stdlib.h>

#include "pm_config.h"
#include "pm_c_util.h"
#include "mallocvar.h"
#include "ppm.h"
#include "ppmdfont.h"
#include "ppmdraw.h"


#define DDA_SCALE 8192

struct penpos {
    int x;
    int y;
};

struct rectangle {
    /* ((0,0),(0,0)) means empty. */
    /* 'lr' is guaranteed not to be left of or above 'ul' */
    struct penpos ul;
    struct penpos lr;
};

static struct rectangle const emptyRectangle = {
    {0, 0},
    {0, 0},
};


static ppmd_point
makePoint(int const x,
          int const y) {

    return ppmd_makePoint(x, y);
}


static ppmd_point
middlePoint(ppmd_point const a,
            ppmd_point const b) {

    ppmd_point retval;

    retval.x = (a.x + b.x) / 2;
    retval.y = (a.y + b.y) / 2;

    return retval;
}



static bool
pointsEqual(ppmd_point const a,
            ppmd_point const b) {

    return a.x == b.x && a.y == b.y;
}



static ppmd_point
vectorSum(ppmd_point const a,
          ppmd_point const b) {

    return makePoint(a.x + b.x, a.y + b.y);
}



static void
drawPoint(ppmd_drawprocp       drawproc,
          const void *   const clientdata,
          pixel **       const pixels, 
          int            const cols, 
          int            const rows, 
          pixval         const maxval, 
          ppmd_point     const p) {
/*----------------------------------------------------------------------------
   Draw a single point, assuming that it is within the bounds of the
   image.
-----------------------------------------------------------------------------*/
    if (drawproc == PPMD_NULLDRAWPROC) {
        const pixel * const pixelP = clientdata;
        
        assert(p.x >= 0); assert(p.x < cols);
        assert(p.y >= 0); assert(p.y < rows);

        pixels[p.y][p.x] = *pixelP;
    } else
        drawproc(pixels, cols, rows, maxval, p, clientdata);
}



struct drawProcXY {
    ppmd_drawproc * drawProc;
    const void *    clientData;
};

static struct drawProcXY
makeDrawProcXY(ppmd_drawproc * const drawProc,
               const void *    const clientData) {

    struct drawProcXY retval;

    retval.drawProc   = drawProc;
    retval.clientData = clientData;
    
    return retval;
}



static ppmd_drawprocp drawProcPointXY;

static void
drawProcPointXY(pixel **     const pixels,
                unsigned int const cols,
                unsigned int const rows,
                pixval       const maxval,
                ppmd_point   const p,
                const void * const clientdata) {

    const struct drawProcXY * const xyP = clientdata;

    xyP->drawProc(pixels, cols, rows, maxval, p.x, p.y, xyP->clientData);
}



static void
drawPointXY(ppmd_drawproc        drawproc,
            const void *   const clientdata,
            pixel **       const pixels, 
            int            const cols, 
            int            const rows, 
            pixval         const maxval, 
            int            const x,
            int            const y) {

    if (drawproc == PPMD_NULLDRAWPROC)
        drawPoint(PPMD_NULLDRAWPROC, clientdata, pixels, cols, rows, maxval,
                  makePoint(x,y));
    else {
        struct drawProcXY const xy = makeDrawProcXY(drawproc, clientdata);

        drawPoint(drawProcPointXY, &xy, pixels, cols, rows, maxval,
                  makePoint(x, y));
    }
}



void
ppmd_point_drawprocp(pixel **     const pixels, 
                     unsigned int const cols, 
                     unsigned int const rows, 
                     pixval       const maxval, 
                     ppmd_point   const p,
                     const void * const clientdata) {

    if (p.x >= 0 && p.x < cols && p.y >= 0 && p.y < rows)
        pixels[p.y][p.x] = *((pixel*)clientdata);
}



void
ppmd_point_drawproc(pixel**     const pixels, 
                    int         const cols, 
                    int         const rows, 
                    pixval      const maxval, 
                    int         const x, 
                    int         const y, 
                    const void* const clientdata) {

    ppmd_point p;
    
    p.x = x;
    p.y = y;

    ppmd_point_drawprocp(pixels, cols, rows, maxval, p, clientdata);
}



static void
findRectangleIntersection(struct rectangle   const rect1,
                          struct rectangle   const rect2,
                          struct rectangle * const intersectionP) {
/*----------------------------------------------------------------------------
   Find the intersection between rectangles 'rect1' and 'rect2'.
   Return it as *intersectionP.
-----------------------------------------------------------------------------*/
    struct penpos tentativeUl, tentativeLr;

    tentativeUl.x = MAX(rect1.ul.x, rect2.ul.x);
    tentativeUl.y = MAX(rect1.ul.y, rect2.ul.y);
    tentativeLr.x = MIN(rect1.lr.x, rect2.lr.x);
    tentativeLr.y = MIN(rect1.lr.y, rect2.lr.y);

    if (tentativeLr.x <= tentativeUl.x ||
        tentativeLr.y <= tentativeUl.y) {
        /* No intersection */
        *intersectionP = emptyRectangle;
    } else {
        intersectionP->ul = tentativeUl;
        intersectionP->lr = tentativeLr;
    }
}



void
ppmd_filledrectangle(pixel **      const pixels, 
                     int           const cols, 
                     int           const rows, 
                     pixval        const maxval, 
                     int           const x, 
                     int           const y, 
                     int           const width, 
                     int           const height, 
                     ppmd_drawproc       drawProc,
                     const void *  const clientdata) {

    struct rectangle image, request, intersection;
    unsigned int row;

    if (width < 0)
        pm_error("negative width %d passed to ppmd_filledrectangle", width);
    if (height < 0)
        pm_error("negative height %d passed to ppmd_filledrectangle", height);
    if (cols < 0)
        pm_error("negative image width %d passed to ppmd_filledrectangle",
                 cols);
    if (rows < 0)
        pm_error("negative image height %d passed to ppmd_filledrectangle",
                 rows);

    request.ul.x = x;
    request.ul.y = y;
    request.lr.x = x + width;
    request.lr.y = y + height;

    image.ul.x = 0;
    image.ul.y = 0;
    image.lr.x = cols;
    image.lr.y = rows;

    findRectangleIntersection(image, request, &intersection);

    /* Draw. */
    for (row = intersection.ul.y; row < intersection.lr.y; ++row) {
        unsigned int col;
        for (col = intersection.ul.x; col < intersection.lr.x; ++col)
            drawPointXY(drawProc, clientdata,
                      pixels, cols, rows, maxval, col, row);
    }
}


/* Outline drawing stuff. */

static int linetype = PPMD_LINETYPE_NORMAL;

int
ppmd_setlinetype(int const type) {

    int old;

    old = linetype;
    linetype = type;
    return old;
}



static bool lineclip = TRUE;



int
ppmd_setlineclip(int const newSetting) {

    bool previousSetting;

    previousSetting = lineclip;

    lineclip = newSetting;

    return previousSetting;
}



static void
clipEnd0(ppmd_point   const p0,
         ppmd_point   const p1,
         int          const cols,
         int          const rows,
         ppmd_point * const c0P,
         bool *       const noLineP) {
/*----------------------------------------------------------------------------
   Given a line that goes from p0 to p1, where any of these coordinates may be
   anywhere in space -- not just in the frame, clip the p0 end to bring it
   into the frame.  Return the clipped-to location as *c0P.

   Iff this is not possible because the entire line described is
   outside the frame, return *nolineP == true.

   The frame is 'cols' columns starting at 0, by 'rows' rows starting
   at 0.
-----------------------------------------------------------------------------*/
    ppmd_point c0;
    bool noLine;

    c0 = p0;         /* initial value */
    noLine = FALSE;  /* initial value */

    /* Clip End 0 of the line horizontally */
    if (c0.x < 0) {
        if (p1.x < 0)
            noLine = TRUE;
        else {
            c0.y = c0.y + (p1.y - c0.y) * (-c0.x) / (p1.x - c0.x);
            c0.x = 0;
        }
    } else if (c0.x >= cols) {
        if (p1.x >= cols)
            noLine = TRUE;
        else {
            c0.y = c0.y + (p1.y - c0.y) * (cols - 1 - c0.x) / (p1.x - c0.x);
            c0.x = cols - 1;
        }
    }

    /* Clip End 0 of the line vertically */
    if (c0.y < 0) {
        if (p1.y < 0)
            noLine = TRUE;
        else {
            c0.x = c0.x + (p1.x - c0.x) * (-c0.y) / (p1.y - c0.y);
            c0.y = 0;
        }
    } else if (c0.y >= rows) {
        if (p1.y >= rows)
            noLine = TRUE;
        else {
            c0.x = c0.x + (p1.x - c0.x) * (rows - 1 - c0.y) / (p1.y - c0.y);
            c0.y = rows - 1;
        }
    }

    /* Clipping vertically may have moved the endpoint out of frame
       horizontally.  If so, we know the other endpoint is also out of
       frame horizontally and the line misses the frame entirely.
    */
    if (c0.x < 0 || c0.x >= cols) {
        assert(p1.x < 0 || p1.x >= cols);
        noLine = TRUE;
    }
    *c0P = c0;
    *noLineP = noLine;
}



static void
clipEnd1(ppmd_point   const p0,
         ppmd_point   const p1,
         int          const cols,
         int          const rows,
         ppmd_point * const c1P) {
/*----------------------------------------------------------------------------
   Given a line that goes from p0 to p1, where p0 is within the frame, but p1
   can be anywhere in space, clip the p1 end to bring it into the frame.
   Return the clipped-to location as *c1P.

   This is guaranteed to be possible, since we already know at least one point
   (i.e. p0) is in the frame.

   The frame is 'cols' columns starting at 0, by 'rows' rows starting
   at 0.
-----------------------------------------------------------------------------*/
    ppmd_point c1;

    assert(p1.x >= 0 && p0.y < cols);
    assert(p1.y >= 0 && p0.y < rows);
    
    /* Clip End 1 of the line horizontally */
    c1 = p1;  /* initial value */
    
    if (c1.x < 0) {
        /* We know the line isn't vertical, since End 0 is in the frame
           and End 1 is left of frame.
        */
        c1.y = c1.y + (p0.y - c1.y) * (-c1.x) / (p0.x - c1.x);
        c1.x = 0;
    } else if (c1.x >= cols) {
        /* We know the line isn't vertical, since End 0 is in the frame
           and End 1 is right of frame.
        */
        c1.y = c1.y + (p0.y - c1.y) * (cols - 1 - c1.x) / (p0.x - c1.x);
        c1.x = cols - 1;
    }
    
    /* Clip End 1 of the line vertically */
    if (c1.y < 0) {
        /* We know the line isn't horizontal, since End 0 is in the frame
           and End 1 is above frame.
        */
        c1.x = c1.x + (p0.x - c1.x) * (-c1.y) / (p0.y - c1.y);
        c1.y = 0;
    } else if (c1.y >= rows) {
        /* We know the line isn't horizontal, since End 0 is in the frame
           and End 1 is below frame.
        */
        c1.x = c1.x + (p0.x - c1.x) * (rows - 1 - c1.y) / (p0.y - c1.y);
        c1.y = rows - 1;
    }

    *c1P = c1;
}



static void
clipLine(ppmd_point   const p0,
         ppmd_point   const p1,
         int          const cols,
         int          const rows,
         ppmd_point * const c0P,
         ppmd_point * const c1P,
         bool *       const noLineP) {
/*----------------------------------------------------------------------------
   Clip the line that goes from p0 to p1 so that none of it is outside the
   boundaries of the raster with width 'cols' and height 'rows'

   The clipped line goes from *c0P to *c1P.

   But if the entire line is outside the boundaries (i.e. we clip the
   entire line), return *noLineP true and the other values undefined.
-----------------------------------------------------------------------------*/
    ppmd_point c0, c1;
        /* The line we successively modify.  Starts out as the input
           line and ends up as the output line.
        */
    bool noLine;

    clipEnd0(p0, p1, cols, rows, &c0, &noLine);

    if (!noLine) {
        /* p0 is in the frame: */
        assert(c0.x >= 0 && c0.x < cols);
        assert(c0.y >= 0 && c0.y < rows);

        clipEnd1(c0, p1, cols, rows, &c1);
    }

    *c0P = c0;
    *c1P = c1;
    *noLineP = noLine;

    pm_message("input = (%d,%d)-(%d,%d) output = (%d,%d) = (%d,%d)",
               p0.x, p0.y, p1.x, p1.y, c0.x, c0.y, c1.x, c1.y);
}



static void
drawShallowLine(ppmd_drawprocp       drawProc,
                const void *   const clientdata,
                pixel **       const pixels, 
                int            const cols, 
                int            const rows, 
                pixval         const maxval, 
                ppmd_point     const p0,
                ppmd_point     const p1) {
/*----------------------------------------------------------------------------
   Draw a line that is more horizontal than vertical.

   Don't clip.

   Assume the line has distinct start and end points (i.e. it's at least
   two points).
-----------------------------------------------------------------------------*/
    /* Loop over X domain. */
    long dy, srow;
    int dx, col, row, prevrow;

    if (p1.x > p0.x)
        dx = 1;
    else
        dx = -1;
    dy = (p1.y - p0.y) * DDA_SCALE / abs(p1.x - p0.x);
    prevrow = row = p0.y;
    srow = row * DDA_SCALE + DDA_SCALE / 2;
    col = p0.x;
    for ( ; ; ) {
        if (linetype == PPMD_LINETYPE_NODIAGS && row != prevrow) {
            drawPoint(drawProc, clientdata,
                      pixels, cols, rows, maxval, makePoint(col, prevrow));
            prevrow = row;
        }
        drawPoint(drawProc, clientdata, pixels, cols, rows, maxval,
                  makePoint(col, row));
        if (col == p1.x)
            break;
        srow += dy;
        row = srow / DDA_SCALE;
        col += dx;
    }
}



static void
drawSteepLine(ppmd_drawprocp       drawProc,
              const void *   const clientdata,
              pixel **       const pixels, 
              int            const cols, 
              int            const rows, 
              pixval         const maxval, 
              ppmd_point     const p0,
              ppmd_point     const p1) {
/*----------------------------------------------------------------------------
   Draw a line that is more vertical than horizontal.

   Don't clip.

   Assume the line has distinct start and end points (i.e. it's at least
   two points).
-----------------------------------------------------------------------------*/
    /* Loop over Y domain. */

    long dx, scol;
    int dy, col, row, prevcol;

    if (p1.y > p0.y)
        dy = 1;
    else
        dy = -1;
    dx = (p1.x - p0.x) * DDA_SCALE / abs(p1.y - p0.y);
    row = p0.y;
    prevcol = col = p0.x;
    scol = col * DDA_SCALE + DDA_SCALE / 2;
    for ( ; ; ) {
        if (linetype == PPMD_LINETYPE_NODIAGS && col != prevcol) {
            drawPoint(drawProc, clientdata,
                      pixels, cols, rows, maxval, makePoint(prevcol, row));
            prevcol = col;
        }
        drawPoint(drawProc, clientdata, pixels, cols, rows, maxval,
                  makePoint(col, row));
        if (row == p1.y)
            break;
        row += dy;
        scol += dx;
        col = scol / DDA_SCALE;
    }
}



void
ppmd_linep(pixel **       const pixels, 
           int            const cols, 
           int            const rows, 
           pixval         const maxval, 
           ppmd_point     const p0,
           ppmd_point     const p1,
           ppmd_drawprocp       drawProc,
           const void *   const clientdata) {

    ppmd_point c0, c1;
    bool noLine;  /* There's no line left after clipping */

    if (lineclip) {
        clipLine(p0, p1, cols, rows, &c0, &c1, &noLine);
    } else {
        c0 = p0;
        c1 = p1;
        noLine = FALSE;
    }

    if (noLine) {
        /* Nothing to draw */
    } else if (pointsEqual(c0, c1)) {
        /* This line is just a point.  Because there aren't two
           distinct endpoints, we have a special case.
        */
        drawPoint(drawProc, clientdata, pixels, cols, rows, maxval, c0);
    } else {
        /* Draw, using a simple DDA. */
        if (abs(c1.x - c0.x) > abs(c1.y - c0.y))
            drawShallowLine(drawProc, clientdata, pixels, cols, rows, maxval,
                            c0, c1);
        else
            drawSteepLine(drawProc, clientdata, pixels, cols, rows, maxval,
                          c0, c1);
    }
}


void
ppmd_line(pixel **      const pixels, 
          int           const cols, 
          int           const rows, 
          pixval        const maxval, 
          int           const x0, 
          int           const y0, 
          int           const x1, 
          int           const y1, 
          ppmd_drawproc       drawProc,
          const void *  const clientData) {

    struct drawProcXY const xy = makeDrawProcXY(drawProc, clientData);

    ppmd_linep(pixels, cols, rows, maxval,
               makePoint(x0, y0), makePoint(x1, y1), drawProcPointXY, &xy);
}



#define SPLINE_THRESH 3
void
ppmd_spline3p(pixel **       const pixels, 
              int            const cols, 
              int            const rows, 
              pixval         const maxval, 
              ppmd_point     const p0,
              ppmd_point     const p1,
              ppmd_point     const p2,
              ppmd_drawprocp       drawProc,
              const void *   const clientdata) {

    ppmd_point a, b, c, p;

    a = middlePoint(p0, p1);
    c = middlePoint(p1, p2);
    b = middlePoint(a, c);
    p = middlePoint(p0, b);

    if (abs(a.x - p.x) + abs(a.y - p.y) > SPLINE_THRESH)
        ppmd_spline3p(
            pixels, cols, rows, maxval, p0, a, b, drawProc, clientdata);
    else
        ppmd_linep(
            pixels, cols, rows, maxval, p0, b, drawProc, clientdata);

    p = middlePoint(p2, b);

    if (abs(c.x - p.x) + abs(c.y - p.y) > SPLINE_THRESH)
        ppmd_spline3p(
            pixels, cols, rows, maxval, b, c, p2, drawProc, clientdata);
    else
        ppmd_linep(pixels, cols, rows, maxval, b, p2, drawProc, clientdata);
}



void
ppmd_spline3(pixel **      const pixels, 
             int           const cols, 
             int           const rows, 
             pixval        const maxval, 
             int           const x0, 
             int           const y0, 
             int           const x1, 
             int           const y1, 
             int           const x2, 
             int           const y2, 
             ppmd_drawproc       drawProc,
             const void *  const clientdata) {

    struct drawProcXY const xy = makeDrawProcXY(drawProc, clientdata);

    ppmd_spline3p(pixels, cols, rows, maxval,
                  makePoint(x0, y0),
                  makePoint(x1, y1),
                  makePoint(x2, y2),
                  drawProcPointXY, &xy);
}



void
ppmd_polysplinep(pixel **       const pixels, 
                 unsigned int   const cols, 
                 unsigned int   const rows, 
                 pixval         const maxval, 
                 ppmd_point     const p0,
                 unsigned int   const nc,
                 ppmd_point *   const c,
                 ppmd_point     const p1,
                 ppmd_drawprocp       drawProc,
                 const void *   const clientdata) {

    ppmd_point p;
    
    unsigned int i;

    assert(nc > 0);

    p = p0;
    for (i = 0; i < nc - 1; ++i) {
        ppmd_point const n = middlePoint(c[i], c[i+1]);
        ppmd_spline3p(
            pixels, cols, rows, maxval, p, c[i], n, drawProc, clientdata);
        p = n;
    }
    ppmd_spline3p(
        pixels, cols, rows, maxval, p, c[nc - 1], p1, drawProc, clientdata);
}



void
ppmd_polyspline(pixel **      const pixels, 
                int           const cols, 
                int           const rows, 
                pixval        const maxval, 
                int           const x0, 
                int           const y0, 
                int           const nc, 
                int *         const xc, 
                int *         const yc, 
                int           const x1, 
                int           const y1, 
                ppmd_drawproc       drawProc,
                const void *  const clientdata) {

    ppmd_point const p1 = makePoint(x1, y1);
    ppmd_point const p0 = makePoint(x0, y0);
    struct drawProcXY const xy = makeDrawProcXY(drawProc, clientdata);

    ppmd_point p;
    unsigned int i;

    p = p0;  /* initial value */

    assert(nc > 0);

    for (i = 0; i < nc - 1; ++i) {
        ppmd_point const n = middlePoint(makePoint(xc[i], yc[i]),
                                         makePoint(xc[i+1], yc[i+1]));
        ppmd_spline3p(
            pixels, cols, rows, maxval, p, makePoint(xc[i], yc[i]), n,
            drawProcPointXY, &xy);
        p = n;
    }
    ppmd_spline3p(
        pixels, cols, rows, maxval, p, makePoint(xc[nc - 1], yc[nc - 1]), p1,
        drawProcPointXY, &xy);
}



void
ppmd_circlep(pixel **       const pixels, 
             unsigned int   const cols, 
             unsigned int   const rows, 
             pixval         const maxval, 
             ppmd_point     const center,
             unsigned int   const radius, 
             ppmd_drawprocp       drawProc,
             const void *   const clientData) {

    if (radius > 0) {
        long const e = DDA_SCALE / radius;

        ppmd_point p0;  /* The starting point around the circle */
        ppmd_point p;   /* Current drawing position in the circle */
        bool nopointsyet;
        long sx, sy;

        p0.x = radius;
        p0.y = 0;

        p = p0;

        sx = p.x * DDA_SCALE + DDA_SCALE / 2;
        sy = p.y * DDA_SCALE + DDA_SCALE / 2;
        drawPoint(drawProc, clientData,
                  pixels, cols, rows, maxval, vectorSum(center, p));
        nopointsyet = true;

        do {
            ppmd_point const prev = p;
            sx += e * sy / DDA_SCALE;
            sy -= e * sx / DDA_SCALE;
            p = makePoint(sx / DDA_SCALE, sy / DDA_SCALE);
            if (!pointsEqual(p, prev)) {
                nopointsyet = false;
                drawPoint(drawProc, clientData,
                          pixels, cols, rows, maxval, vectorSum(center, p));
            }
        }
        while (nopointsyet || !pointsEqual(p, p0));
    }
}



void
ppmd_circle(pixel **      const pixels, 
            int           const cols, 
            int           const rows, 
            pixval        const maxval, 
            int           const cx, 
            int           const cy, 
            int           const radius, 
            ppmd_drawproc       drawProc,
            const void *  const clientData) {

    struct drawProcXY const xy = makeDrawProcXY(drawProc, clientData);

    ppmd_circlep(pixels, cols, rows, maxval, makePoint(cx, cy), radius,
                 drawProcPointXY, &xy);
}



/* Arbitrary fill stuff. */

typedef struct
{
    short x;
    short y;
    short edge;
} coord;

typedef struct fillobj {
    int n;
    int size;
    int curedge;
    int segstart;
    int ydir;
    int startydir;
    coord * coords;
} fillobj;

#define SOME 1000

static int oldclip;

struct fillobj *
ppmd_fill_create(void) {

    fillobj * fillObjP;

    MALLOCVAR(fillObjP);
    if (fillObjP == NULL)
        pm_error("out of memory allocating a fillhandle");
    fillObjP->n = 0;
    fillObjP->size = SOME;
    MALLOCARRAY(fillObjP->coords, fillObjP->size);
    if (fillObjP->coords == NULL)
        pm_error("out of memory allocating a fillhandle");
    fillObjP->curedge = 0;
    
    /* Turn off line clipping. */
    /* UGGH! We must eliminate this global variable */
    oldclip = ppmd_setlineclip(0);
    
    return fillObjP;
}



char *
ppmd_fill_init(void) {
/*----------------------------------------------------------------------------
   Backward compatibility interface.  This is what was used before
   ppmd_fill_create() existed.

   Note that old programs treat a fill handle as a pointer to char
   rather than a pointer to fillObj, and backward compatibility
   depends upon the fact that these are implemented as identical types
   (an address).
-----------------------------------------------------------------------------*/
    return (char *)ppmd_fill_create();
}



void
ppmd_fill_destroy(struct fillobj * fillObjP) {

    free(fillObjP->coords);
    free(fillObjP);
}



void
ppmd_fill_drawprocp(pixel **     const pixels, 
                    unsigned int const cols, 
                    unsigned int const rows, 
                    pixval       const maxval, 
                    ppmd_point   const p,
                    const void * const clientdata) {

    fillobj * fh;
    coord * cp;
    coord * ocp;

    fh = (fillobj*) clientdata;

    if (fh->n > 0) {
        /* If these are the same coords we saved last time, don't bother. */
        ocp = &(fh->coords[fh->n - 1]);
        if (p.x == ocp->x && p.y == ocp->y)
            return;
    }

    /* Ok, these are new; check if there's room for two more coords. */
    if (fh->n + 1 >= fh->size) {
        fh->size += SOME;
        REALLOCARRAY(fh->coords, fh->size);
        if (fh->coords == NULL)
            pm_error("out of memory enlarging a fillhandle");
    }

    /* Check for extremum and set the edge number. */
    if (fh->n == 0) {
        /* Start first segment. */
        fh->segstart = fh->n;
        fh->ydir = 0;
        fh->startydir = 0;
    } else {
        int dx, dy;

        dx = p.x - ocp->x;
        dy = p.y - ocp->y;
        if (dx < -1 || dx > 1 || dy < -1 || dy > 1) {
            /* Segment break.  Close off old one. */
            if (fh->startydir != 0 && fh->ydir != 0)
                if (fh->startydir == fh->ydir) {
                    /* Oops, first edge and last edge are the same.
                       Renumber the first edge in the old segment.
                    */
                    coord * fcp;
                    int oldedge;

                    fcp = &(fh->coords[fh->segstart]);
                    oldedge = fcp->edge;
                    for ( ; fcp->edge == oldedge; ++fcp )
                        fcp->edge = ocp->edge;
                }
            /* And start new segment. */
            ++fh->curedge;
            fh->segstart = fh->n;
            fh->ydir = 0;
            fh->startydir = 0;
        } else {
            /* Segment continues. */
            if (dy != 0) {
                if (fh->ydir != 0 && fh->ydir != dy) {
                    /* Direction changed.  Insert a fake coord, old
                       position but new edge number.
                    */
                    ++fh->curedge;
                    cp = &fh->coords[fh->n];
                    cp->x = ocp->x;
                    cp->y = ocp->y;
                    cp->edge = fh->curedge;
                    ++fh->n;
                }
                fh->ydir = dy;
                if (fh->startydir == 0)
                    fh->startydir = dy;
            }
        }
    }

    /* Save this coord. */
    cp = &fh->coords[fh->n];
    cp->x = p.x;
    cp->y = p.y;
    cp->edge = fh->curedge;
    ++fh->n;
}




void
ppmd_fill_drawproc(pixel**      const pixels, 
                   int          const cols, 
                   int          const rows, 
                   pixval       const maxval, 
                   int          const x, 
                   int          const y, 
                   const void * const clientData) {

    ppmd_fill_drawprocp(pixels, cols, rows, maxval, makePoint(x, y),
                        clientData);
}




#ifndef LITERAL_FN_DEF_MATCH
static qsort_comparison_fn yx_compare;
#endif

static int
yx_compare(const void * const c1Arg,
           const void * const c2Arg) {

    const coord * const c1P = c1Arg;
    const coord * const c2P = c2Arg;

    int retval;
    
    if (c1P->y > c2P->y)
        retval = 1;
    else if (c1P->y < c2P->y)
        retval = -1;
    else if (c1P->x > c2P->x)
        retval = 1;
    else if (c1P->x < c2P->x)
        retval = -1;
    else
        retval = 0;

    return retval;
}



void
ppmd_fill(pixel **         const pixels, 
          int              const cols, 
          int              const rows, 
          pixval           const maxval, 
          struct fillobj * const fh,
          ppmd_drawproc          drawProc,
          const void *     const clientdata) {

    int pedge;
    int i, edge, lx, rx, py;
    coord * cp;
    bool eq;
    bool leftside;

    /* Close off final segment. */
    if (fh->n > 0 && fh->startydir != 0 && fh->ydir != 0) {
        if (fh->startydir == fh->ydir) {
            /* Oops, first edge and last edge are the same. */
            coord * fcp;
            int lastedge, oldedge;

            lastedge = fh->coords[fh->n - 1].edge;
            fcp = &(fh->coords[fh->segstart]);
            oldedge = fcp->edge;
            for ( ; fcp->edge == oldedge; ++fcp )
                fcp->edge = lastedge;
        }
    }
    /* Restore clipping now. */
    ppmd_setlineclip(oldclip);

    /* Sort the coords by Y, secondarily by X. */
    qsort((char*) fh->coords, fh->n, sizeof(coord), yx_compare);

    /* Find equal coords with different edge numbers, and swap if necessary. */
    edge = -1;
    for (i = 0; i < fh->n; ++i) {
        cp = &fh->coords[i];
        if (i > 1 && eq && cp->edge != edge && cp->edge == pedge) {
            /* Swap .-1 and .-2. */
            coord t;

            t = fh->coords[i-1];
            fh->coords[i-1] = fh->coords[i-2];
            fh->coords[i-2] = t;
        }
        if (i > 0) {
            if (cp->x == lx && cp->y == py) {
                eq = TRUE;
                if (cp->edge != edge && cp->edge == pedge) {
                    /* Swap . and .-1. */
                    coord t;

                    t = *cp;
                    *cp = fh->coords[i-1];
                    fh->coords[i-1] = t;
                }
            } else
                eq = FALSE;
        }
        lx    = cp->x;
        py    = cp->y;
        pedge = edge;
        edge  = cp->edge;
    }

    /* Ok, now run through the coords filling spans. */
    for (i = 0; i < fh->n; ++i) {
        cp = &fh->coords[i];
        if (i == 0) {
            lx       = rx = cp->x;
            py       = cp->y;
            edge     = cp->edge;
            leftside = TRUE;
        } else {
            if (cp->y != py) {
                /* Row changed.  Emit old span and start a new one. */
                ppmd_filledrectangle(
                    pixels, cols, rows, maxval, lx, py, rx - lx + 1, 1,
                    drawProc, clientdata);
                lx       = rx = cp->x;
                py       = cp->y;
                edge     = cp->edge;
                leftside = TRUE;
            } else {
                if (cp->edge == edge) {
                    /* Continuation of side. */
                    rx = cp->x;
                } else {
                    /* Edge changed.  Is it a span? */
                    if (leftside) {
                        rx       = cp->x;
                        leftside = FALSE;
                    } else {
                        /* Got a span to fill. */
                        ppmd_filledrectangle(
                            pixels, cols, rows, maxval, lx, py, rx - lx + 1,
                            1, drawProc, clientdata);
                        lx       = rx = cp->x;
                        leftside = TRUE;
                    }
                    edge = cp->edge;
                }
            }
        }
    }
}



/* Table used to look up sine of angles from 0 through 90 degrees.
   The value returned is the sine * 65536.  Symmetry is used to
   obtain sine and cosine for arbitrary angles using this table. */

static long sintab[] = {
    0, 1143, 2287, 3429, 4571, 5711, 6850, 7986, 9120, 10252, 11380,
    12504, 13625, 14742, 15854, 16961, 18064, 19160, 20251, 21336,
    22414, 23486, 24550, 25606, 26655, 27696, 28729, 29752, 30767,
    31772, 32768, 33753, 34728, 35693, 36647, 37589, 38521, 39440,
    40347, 41243, 42125, 42995, 43852, 44695, 45525, 46340, 47142,
    47929, 48702, 49460, 50203, 50931, 51643, 52339, 53019, 53683,
    54331, 54963, 55577, 56175, 56755, 57319, 57864, 58393, 58903,
    59395, 59870, 60326, 60763, 61183, 61583, 61965, 62328, 62672,
    62997, 63302, 63589, 63856, 64103, 64331, 64540, 64729, 64898,
    65047, 65176, 65286, 65376, 65446, 65496, 65526, 65536
};

static int extleft, exttop, extright, extbottom;  /* To accumulate extents */

/* LINTLIBRARY */

/*  ISIN  --  Return sine of an angle in integral degrees.  The
          value returned is 65536 times the sine.  */

static long isin(int deg)
{
    /* Domain reduce to 0 to 360 degrees. */

    if (deg < 0) {
        deg = (360 - ((- deg) % 360)) % 360;
    } else if (deg >= 360) {
        deg = deg % 360;
    }

    /* Now look up from table according to quadrant. */

    if (deg <= 90) {
        return sintab[deg];
    } else if (deg <= 180) {
        return sintab[180 - deg];
    } else if (deg <= 270) {
        return -sintab[deg - 180];
    }
    return -sintab[360 - deg];
}

/*  ICOS  --  Return cosine of an angle in integral degrees.  The
          value returned is 65536 times the cosine.  */

static long icos(int deg)
{
    return isin(deg + 90);
}  

#define SCHAR(x) (u = (x), (((u) & 0x80) ? ((u) | (-1 ^ 0xFF)) : (u)))

#define Scalef 21       /* Font design size */
#define Descend 9       /* Descender offset */



static void
drawGlyph(const struct ppmd_glyph * const glyphP,
          int *                     const xP,
          int                       const y,
          pixel **                  const pixels,
          unsigned int              const cols,
          unsigned int              const rows,
          pixval                    const maxval,
          int                       const height,
          int                       const xpos,
          int                       const ypos,
          long                      const rotcos,
          long                      const rotsin,
          ppmd_drawproc                   drawProc,
          const void *              const clientdata
          ) {
/*----------------------------------------------------------------------------
   *xP is the column number of the left side of the glyph in the
   output upon entry, and we update it to the left side of the next
   glyph.

   'y' is the row number of either the top or the bottom of the glyph
   (I can't tell which right now) in the output.
-----------------------------------------------------------------------------*/
    struct penpos penPos;
    unsigned int commandNum;
    int x;
    int u;  /* Used by the SCHAR macro */

    x = *xP;  /* initial value */

    x -= SCHAR(glyphP->header.skipBefore);

    penPos.x = x;
    penPos.y = y;

    for (commandNum = 0;
         commandNum < glyphP->header.commandCount;
         ++commandNum) {

        const struct ppmd_glyphCommand * const commandP =
            &glyphP->commandList[commandNum];

        switch (commandP->verb) {
        case CMD_NOOP:
            break;
        case CMD_DRAWLINE:
        {
            int const nx = x + SCHAR(commandP->x);
            int const ny = y + SCHAR(commandP->y);

            int mx1, my1, mx2, my2;
            int tx1, ty1, tx2, ty2;

            /* Note that up until this  moment  we've  been
               working  in  an  arbitrary model co-ordinate
               system with  fixed  size  and  no  rotation.
               Before  drawing  the  stroke,  transform  to
               viewing co-ordinates to  honour  the  height
               and angle specifications.
            */

            mx1 = (penPos.x * height) / Scalef;
            my1 = ((penPos.y - Descend) * height) / Scalef;
            mx2 = (nx * height) / Scalef;
            my2 = ((ny - Descend) * height) / Scalef;
            tx1 = xpos + (mx1 * rotcos - my1 * rotsin) / 65536;
            ty1 = ypos + (mx1 * rotsin + my1 * rotcos) / 65536;
            tx2 = xpos + (mx2 * rotcos - my2 * rotsin) / 65536;
            ty2 = ypos + (mx2 * rotsin + my2 * rotcos) / 65536;
            
            ppmd_line(pixels, cols, rows, maxval, tx1, ty1, tx2, ty2,
                      drawProc, clientdata);

            penPos.x = nx;
            penPos.y = ny;
        }
            break;
        case CMD_MOVEPEN:
            penPos.x = x + SCHAR(commandP->x);
            penPos.y = y + SCHAR(commandP->y);
            break;
        }
    }
    x += glyphP->header.skipAfter; 

    *xP = x;
}


/* PPMD_TEXT  --  Draw the zero-terminated  string  s,  with  its  baseline
          starting  at  point  (x, y), inclined by angle degrees to
          the X axis, with letters height pixels  high  (descenders
          will  extend below the baseline).  The supplied drawproc
          and cliendata are passed to ppmd_line which performs  the
          actual drawing. */

void
ppmd_text(pixel**       const pixels, 
          int           const cols, 
          int           const rows, 
          pixval        const maxval, 
          int           const xpos, 
          int           const ypos, 
          int           const height, 
          int           const angle, 
          const char *  const sArg, 
          ppmd_drawproc       drawProc,
          const void *  const clientdata) {

    const struct ppmd_font * const fontP = ppmd_get_font();
    long rotsin, rotcos;
    int x, y;
    const char * s;

    x = y = 0;
    rotsin = isin(-angle);
    rotcos = icos(-angle);

    s = sArg;
    while (*s) {
        unsigned char const ch = *s++;

        if (ch >= fontP->header.firstCodePoint &&
            ch < fontP->header.firstCodePoint + fontP->header.characterCount) {

            const struct ppmd_glyph * const glyphP =
                &fontP->glyphTable[ch - fontP->header.firstCodePoint];

            drawGlyph(glyphP, &x, y, pixels, cols, rows, maxval,
                      height, xpos, ypos, rotcos, rotsin,
                      drawProc, clientdata);
        } else if (ch == '\n') {
            /* Move to the left edge of the next line down */
            y += Scalef + Descend;
            x = 0;
        }
    }
}

/* EXTENTS_DRAWPROC  --  Drawproc which just accumulates the extents
             rectangle bounding the text. */

static void 
extents_drawproc (pixel**      const pixels, 
                  int          const cols, 
                  int          const rows,
                  pixval       const maxval, 
                  int          const x, 
                  int          const y, 
                  const void * const clientdata)
{
    extleft = MIN(extleft, x);
    exttop = MIN(exttop, y);
    extright = MAX(extright, x);
    extbottom = MAX(extbottom, y);
}


/* PPMD_TEXT_BOX  --  Calculate  extents  rectangle for a given piece of
   text.  For most  applications  where  extents  are
   needed,   angle  should  be  zero  to  obtain  the
   unrotated extents.  If you need  the  extents  box
   for post-rotation text, however, you can set angle
   nonzero and it will be calculated correctly.
*/

void
ppmd_text_box(int const height, 
              int const angle, 
              const char * const s, 
              int * const left, 
              int * const top, 
              int * const right, 
              int * const bottom)
{
    extleft = 32767;
    exttop = 32767;
    extright = -32767;
    extbottom = -32767;
    ppmd_text(NULL, 32767, 32767, 255, 1000, 1000, height, angle, s, 
              extents_drawproc, NULL);
    *left = extleft - 1000; 
    *top = exttop - 1000;
    *right = extright - 1000;
    *bottom = extbottom - 1000;
}
