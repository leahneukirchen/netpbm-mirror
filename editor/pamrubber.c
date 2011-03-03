/*----------------------------------------------------------------------------*/

/* pamrubber.c - transform images using Rubber Sheeting algorithm
**               see: http://www.schaik.com/netpbm/rubber/
**
** Copyright (C) 2011 by Willem van Schaik (willem@schaik.com)
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

/*----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <time.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pam.h"
#include "pamdraw.h"

/*----------------------------------------------------------------------------*/


struct cmdlineInfo {
    const char * filename;
    unsigned int quad;
    unsigned int tri;
    unsigned int frame;
    unsigned int linear;
    unsigned int verbose;
};


/*----------------------------------------------------------------------------*/

typedef struct point {
  double x;
  double y;
} point;

typedef struct line {
  point p1;
  point p2;
} line;

typedef struct triangle {
  point p1;
  point p2;
  point p3;
} triangle;


/* global variables */

static int nCP;
static point oldCP[4] = {{-1.0,-1.0},{-1.0,-1.0},{-1.0,-1.0},{-1.0,-1.0}};
static point newCP[4] = {{-1.0,-1.0},{-1.0,-1.0},{-1.0,-1.0},{-1.0,-1.0}};
static int nTri;
static triangle tri1s[10];
static triangle tri2s[10];
static point quad1[4];
static point quad2[4];
static tuple black;

/*----------------------------------------------------------------------------*/

static void
makepoint(point * const p1P,
          double  const x,
          double  const y) {

    p1P->x = x;
    p1P->y = y;
}



static void
copypoint(point * const p1P,
          point * const p2P) {

    p1P->x = p2P->x;
    p1P->y = p2P->y;
}



static double
distance(point * const p1P,
         point * const p2P) {

    return sqrt((p1P->x - p2P->x) * (p1P->x - p2P->x) + 
                (p1P->y - p2P->y) * (p1P->y - p2P->y));
}



static void
makeline(line *  const lP,
         point * const p1P,
         point * const p2P) {

    lP->p1.x = p1P->x;
    lP->p1.y = p1P->y;
    lP->p2.x = p2P->x;
    lP->p2.y = p2P->y;
}



static int
intersect(line *  const l1P,
          line *  const l2P,
          point * const pP) {

    int cross;
    double ua, ub;

    cross = 0;  /* initial value */

    if (((l2P->p2.y - l2P->p1.y) * (l1P->p2.x - l1P->p1.x) -
         (l2P->p2.x - l2P->p1.x) * (l1P->p2.y - l1P->p1.y)) == 0) {
        /* parallel lines */
        if ((l1P->p1.x == l1P->p2.x) && (l2P->p1.x == l2P->p2.x)) {
            /* two vertical lines */
            pP->x = (l1P->p1.x + l2P->p1.x) / 2.0;
            pP->y = 1e10;
        } else if ((l1P->p1.y == l1P->p2.y) && (l2P->p1.y == l2P->p2.y)) {
            /* two horizontal lines */
            pP->x = 1e10;
            pP->y = (l1P->p1.y + l2P->p1.y) / 2.0;
        } else {
            if (fabs(l1P->p2.y - l1P->p1.y) > fabs(l1P->p2.x - l1P->p1.x)) {
                /* steep slope */
                pP->y = 1e10;
                pP->x = (l1P->p2.x - l1P->p1.x) / (l1P->p2.y - l1P->p1.y)
                    * 1e10;
            } else {
                /* even slope */
                pP->x = 1e10;
                pP->y = (l1P->p2.y - l1P->p1.y) / (l1P->p2.x - l1P->p1.x)
                    * 1e10;
            }
        }
    } else {
        /* intersecting lines */
        ua = ((l2P->p2.x - l2P->p1.x) * (l1P->p1.y - l2P->p1.y)
              - (l2P->p2.y - l2P->p1.y) * (l1P->p1.x - l2P->p1.x))
            / ((l2P->p2.y - l2P->p1.y)
               * (l1P->p2.x - l1P->p1.x) - (l2P->p2.x - l2P->p1.x)
               * (l1P->p2.y - l1P->p1.y));
        ub = ((l1P->p2.x - l1P->p1.x) * (l1P->p1.y - l2P->p1.y)
              - (l1P->p2.y - l1P->p1.y) * (l1P->p1.x - l2P->p1.x))
            / ((l2P->p2.y - l2P->p1.y)
               * (l1P->p2.x - l1P->p1.x) - (l2P->p2.x - l2P->p1.x)
               * (l1P->p2.y - l1P->p1.y));

        pP->x = l1P->p1.x + ua * (l1P->p2.x - l1P->p1.x);
        pP->y = l1P->p1.y + ua * (l1P->p2.y - l1P->p1.y);

        if ((ua >= 0.0) && (ua <= 1.0) && (ub >= 0.0) && (ub <= 1.0))
            cross = 1;
    }

    return cross;
}



static void
maketriangle(triangle * const tP,
             point *    const p1P,
             point *    const p2P,
             point *    const p3P) {

    tP->p1.x = p1P->x;
    tP->p1.y = p1P->y;
    tP->p2.x = p2P->x;
    tP->p2.y = p2P->y;
    tP->p3.x = p3P->x;
    tP->p3.y = p3P->y;
}



static int
insidetri(triangle * const triP,
          point *    const pP) {

    int cnt;

    cnt = 0;  /* initial value */

    if ((((triP->p1.y <= pP->y) && (pP->y < triP->p3.y))
         || ((triP->p3.y <= pP->y) && (pP->y < triP->p1.y)))
        &&
        (pP->x < (triP->p3.x - triP->p1.x) * (pP->y - triP->p1.y)
         / (triP->p3.y - triP->p1.y) + triP->p1.x))
        cnt = !cnt;

    if ((((triP->p2.y <= pP->y) && (pP->y < triP->p1.y))
         || ((triP->p1.y <= pP->y) && (pP->y < triP->p2.y)))
        &&
        (pP->x < (triP->p1.x - triP->p2.x) * (pP->y - triP->p2.y)
         / (triP->p1.y - triP->p2.y) + triP->p2.x))
        cnt = !cnt;

    if ((((triP->p3.y <= pP->y) && (pP->y < triP->p2.y))
         || ((triP->p2.y <= pP->y) && (pP->y < triP->p3.y)))
        &&
        (pP->x < (triP->p2.x - triP->p3.x) * (pP->y - triP->p3.y)
         / (triP->p2.y - triP->p3.y) + triP->p3.x))
        cnt = !cnt;

    return cnt;
}



static int
windtriangle(triangle * const tP,
             point *    const p1P,
             point *    const p2P,
             point *    const p3P) {
    point f, c;
    line le, lv;
    int cw;

    /* find cross of vertical through p3 and the edge p1-p2 */
    f.x = p3P->x;
    f.y = -1.0;
    makeline (&lv, p3P, &f);
    makeline (&le, p1P, p2P);
    intersect (&le, &lv, &c);

    /* check for clockwise winding triangle */
    if (((p1P->x > p2P->x) && (p3P->y < c.y)) ||
        ((p1P->x < p2P->x) && (p3P->y > c.y))) {
        maketriangle(tP, p1P, p2P, p3P);
        cw = 1;
    } else { /* p1/2/3 were counter clockwise */
        maketriangle (tP, p1P, p3P, p2P);
        cw = 0;
    }

    return cw;
}



static double
tiny(void) {

    if (rand() % 2)
        return +1E-6 * (double) ((rand() % 90) + 9);
    else
        return -1E-6 * (double) ((rand() % 90) + 9);
}



static void
angle(point * const p1P,
      point * const p2P) {

    if (p1P->x == p2P->x) { /* vertical line */
        p2P->x += tiny();
    }
    if (p1P->y == p2P->y) { /* horizontal line */
        p2P->y += tiny();
    }
}



/*----------------------------------------------------------------------------*/



#define TL 0
#define TR 1
#define BL 2
#define BR 3

/*----------------------------------------------------------------------------*/

#define SIDETRIANGLE(N,TRIG1,P11,P12,P13,P14,R11,R12,TRIG2,P21,P22,P23,P24,R21,R22) { \
  if (fabs (R11.x - R12.x) < 1.0) { /* vertical edge */ \
    if (((N >= 4) && (R11.x < P11.x) && (P14.x < P13.x) && (P14.x < P12.x) && (P14.x < P11.x)) || /* left edge */ \
        ((N >= 4) && (R11.x > P11.x) && (P14.x > P13.x) && (P14.x > P12.x) && (P14.x > P11.x))) { /* right edge */ \
      maketriangle (&TRIG1, &R11, &R12, &P14); \
      maketriangle (&TRIG2, &R21, &R22, &P24); \
    } \
    else if (((N >= 3) && (R11.x < P11.x) && (P13.x < P12.x) && (P13.x < P11.x)) || /* left edge */ \
             ((N >= 3) && (R11.x > P11.x) && (P13.x > P12.x) && (P13.x > P11.x))) { /* right edge */ \
      maketriangle (&TRIG1, &R11, &R12, &P13); \
      maketriangle (&TRIG2, &R21, &R22, &P23); \
    } \
    else if (((N >= 2) && (R11.x < P11.x) && (P12.x < P11.x)) || /* left edge */ \
             ((N >= 2) && (R11.x > P11.x) && (P12.x > P11.x))) { /* right edge */ \
      maketriangle (&TRIG1, &R11, &R12, &P12); \
      maketriangle (&TRIG2, &R21, &R22, &P22); \
    } \
    else if (N >= 1) { \
      maketriangle (&TRIG1, &R11, &R12, &P11); \
      maketriangle (&TRIG2, &R21, &R22, &P21); \
    } \
  } else \
  if (fabs (R11.y - R12.y) < 1.0) { /* horizontal edge */ \
    if (((N >= 4) && (R11.y < P11.y) && (P14.y < P13.y) && (P14.y < P12.y) && (P14.y < P11.y)) || /* top edge */ \
        ((N >= 4) && (R11.y > P11.y) && (P14.y > P13.y) && (P14.y > P12.y) && (P14.y > P11.y))) { /* bottom edge */ \
      maketriangle (&TRIG1, &R11, &R12, &P14); \
      maketriangle (&TRIG2, &R21, &R22, &P24); \
    } \
    else if (((N >= 3) && (R11.y < P11.y) && (P13.y < P12.y) && (P13.y < P11.y)) || /* top edge */ \
             ((N >= 3) && (R11.y > P11.y) && (P13.y > P12.y) && (P13.y > P11.y))) { /* bottom edge */ \
      maketriangle (&TRIG1, &R11, &R12, &P13); \
      maketriangle (&TRIG2, &R21, &R22, &P23); \
    } \
    else if (((N >= 2) && (R11.y < P11.y) && (P12.y < P11.y)) || /* top edge */ \
             ((N >= 2) && (R11.y > P11.y) && (P12.y > P11.y))) { /* bottom edge */ \
      maketriangle (&TRIG1, &R11, &R12, &P12); \
      maketriangle (&TRIG2, &R21, &R22, &P22); \
    } \
    else if (N >= 1) { \
      maketriangle (&TRIG1, &R11, &R12, &P11); \
      maketriangle (&TRIG2, &R21, &R22, &P21); \
    } \
  } \
}

#define EDGETRIANGLE(TRIG1,P11,P12,TL1,TR1,BL1,BR1,TRIG2,P21,P22,TL2,TR2,BL2,BR2) { \
    if ((P11.x < P12.x) && (P11.y < P12.y)) { /* up/left to down/right */ \
      maketriangle (&TRIG1, &TR1, &P12, &P11); \
      maketriangle (&TRIG2, &TR2, &P22, &P21); \
    } \
    else if ((P11.x > P12.x) && (P11.y > P12.y)) { /* down/right to up/left */ \
      maketriangle (&TRIG1, &BL1, &P12, &P11); \
      maketriangle (&TRIG2, &BL2, &P22, &P21); \
    } \
    else if ((P11.x < P12.x) && (P11.y > P12.y)) { /* down/left to up/right */ \
      maketriangle (&TRIG1, &TL1, &P12, &P11); \
      maketriangle (&TRIG2, &TL2, &P22, &P21); \
    } \
    else if ((P11.x > P12.x) && (P11.y < P12.y)) { /* up/right to down/left */ \
      maketriangle (&TRIG1, &BR1, &P12, &P11); \
      maketriangle (&TRIG2, &BR2, &P22, &P21); \
    } \
}

#define QUAD_RECT(QUAD,LFT,RGT,TOP,BOT) { \
  makepoint (&QUAD[0], LFT, TOP); \
  makepoint (&QUAD[1], RGT, TOP); \
  makepoint (&QUAD[2], LFT, BOT); \
  makepoint (&QUAD[3], RGT, BOT); \
}

#define QUAD_CORNER(QUAD,P0,P1,P2,P3,MID,TRI) { \
/* P0-P1 and P2-P3 are the diagonals */ \
  if (fabs(P0.x - P1.x) + fabs(P0.y - P1.y) >= fabs(P2.x - P3.x) + fabs(P2.y - P3.y)) { \
    QUAD_CORNER_SIZED(QUAD,P0,P1,P2,P3,MID,TRI); \
  } else { \
    QUAD_CORNER_SIZED(QUAD,P2,P3,P0,P1,MID,TRI); \
  } \
}

#define QUAD_CORNER_SIZED(QUAD,P0,P1,P2,P3,MID,TRI) { \
/* P0-P1 and P2-P3 are the diagonals */ \
/* P0-P1 are further apart than P2-P3 */ \
    if ((P0.x < P1.x) && (P0.y < P1.y)) { /* P0 is top-left */ \
      copypoint (&QUAD[0], &P0); \
      copypoint (&QUAD[3], &P1); \
      if (windtriangle (&TRI, &P0, &P2, &P1)) { /* P2 is top-right */ \
        copypoint (&QUAD[1], &P2); \
        copypoint (&QUAD[2], &P3); \
      } \
      else { /* P3 is top-right */ \
        copypoint (&QUAD[1], &P3); \
        copypoint (&QUAD[2], &P2); \
      } \
    } \
    else \
    if ((P0.x > P1.x) && (P0.y < P1.y)) { /* P0 is top-right */ \
      copypoint (&QUAD[1], &P0); \
      copypoint (&QUAD[2], &P1); \
      if (windtriangle (&TRI, &P0, &P2, &P1)) { /* P2 is bottom-right */ \
        copypoint (&QUAD[3], &P2); \
        copypoint (&QUAD[0], &P3); \
      } \
      else { /* P3 is bottom-right */ \
        copypoint (&QUAD[3], &P3); \
        copypoint (&QUAD[0], &P2); \
      } \
    } \
    else \
    if ((P0.x < P1.x) && (P0.y > P1.y)) { /* P0 is bottom-left */ \
      copypoint (&QUAD[2], &P0); \
      copypoint (&QUAD[1], &P1); \
      if (windtriangle (&TRI, &P0, &P2, &P1)) { /* P2 is top-left */ \
        copypoint (&QUAD[0], &P2); \
        copypoint (&QUAD[3], &P3); \
      } \
      else { /* P3 is top-left */ \
        copypoint (&QUAD[0], &P3); \
        copypoint (&QUAD[3], &P2); \
      } \
    } \
    else \
    if ((P0.x > P1.x) && (P0.y > P1.y)) { /* P0 is bottom-right */ \
      copypoint (&QUAD[3], &P0); \
      copypoint (&QUAD[0], &P1); \
      if (windtriangle (&TRI, &P0, &P2, &P1)) { /* P2 is bottom-left */ \
        copypoint (&QUAD[2], &P2); \
        copypoint (&QUAD[1], &P3); \
      } \
      else { /* P3 is bottom-left */ \
        copypoint (&QUAD[2], &P3); \
        copypoint (&QUAD[1], &P2); \
      } \
    } \
}



static pamd_drawproc frameDrawproc;

static void
frameDrawproc (tuple **     const tuples,
               unsigned int const cols,
               unsigned int const rows,
               unsigned int const depth,
               sample       const maxval,
               pamd_point   const p,
               const void * const clientdata) {
    
    int yy;

    for (yy = p.y - 1; yy <= p.y + 1; ++yy) {
        int xx;
        for (xx = p.x - 1; xx <= p.x + 1; ++xx) {
            if (xx >= 0 && xx < cols && yy >= 0 && yy < rows) {
                unsigned int i;
                for (i = 0; i < depth; ++i)
                    tuples[yy][xx][i] = (sample) *((tuple *) clientdata + i);
            }
        }
    }
}



static void
drawExtendedLine(const struct pam * const pamP,
                 tuple **           const wrTuples,
                 point              const p1,
                 point              const p2) {

    pamd_point const p1ext =
        pamd_makePoint(p1.x - 10 * (p2.x - p1.x),
                       p1.y - 10 * (p2.y - p1.y));

    pamd_point const p2ext =
        pamd_makePoint(p2.x + 10 * (p2.x - p1.x),
                       p2.y + 10 * (p2.y - p1.y));

    pamd_line(wrTuples, pamP->width, pamP->height, pamP->depth, pamP->maxval,
              p1ext, p2ext, frameDrawproc, black);
}



static pamd_point
clippedPoint(const struct pam * const pamP,
             point              const p) {

    int const roundedX = ROUND(p.x);
    int const roundedY = ROUND(p.x);

    int clippedX, clippedY;

    assert(pamP->width  >= 2);
    assert(pamP->height >= 2);

    if (roundedX <= 0)
        clippedX = 1;
    else if (roundedX > pamP->width - 1)
        clippedX = pamP->width - 2;
    else
        clippedX = roundedX;
        
    if (roundedY <= 0)
        clippedY = 1;
    else if (roundedY > pamP->width - 1)
        clippedY = pamP->width - 2;
    else
        clippedY = roundedY;
        
    return pamd_makePoint(clippedX, clippedY);
}



static void drawClippedTriangle(const struct pam * const pamP,
                                tuple **           const tuples,
                                triangle           const tri) {

    pamd_point const p1 = clippedPoint(pamP, tri.p1);
    pamd_point const p2 = clippedPoint(pamP, tri.p2);
    pamd_point const p3 = clippedPoint(pamP, tri.p3);

    pamd_line(tuples, pamP->width, pamP->height, pamP->depth, pamP->maxval,
              p1, p2, frameDrawproc, black);
    pamd_line(tuples, pamP->width, pamP->height, pamP->depth, pamP->maxval,
              p2, p3, frameDrawproc, black);
    pamd_line(tuples, pamP->width, pamP->height, pamP->depth, pamP->maxval,
              p3, p1, frameDrawproc, black);
}



/*----------------------------------------------------------------------------*/

static void
parseCmdline(int argc, const char ** argv,
             struct cmdlineInfo * const pCmdline) {

/* parse all parameters from the command line */

    unsigned int option_def_index;
    char * endptr;
    int i;

    /* instructions to optParseOptions3 on how to parse our options. */
    optEntry * option_def;
    optStruct3 opt;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "quad", OPT_FLAG, NULL, &pCmdline->quad, 0);
    OPTENT3(0, "tri", OPT_FLAG, NULL, &pCmdline->tri, 0);
    OPTENT3(0, "frame", OPT_FLAG, NULL, &pCmdline->frame, 0);
    OPTENT3(0, "linear", OPT_FLAG, NULL, &pCmdline->linear, 0);
    OPTENT3(0, "verbose", OPT_FLAG, NULL, &pCmdline->verbose, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* we have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;    /* we have no parms that are negative numbers */

    /* uses and sets argc, argv, and some of *pCmdline and others. */
    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);

    /* check if one of -tri and -quad are given */
    if (pCmdline->tri == pCmdline->quad) {
        pm_error ("Either '-tri' or '-quad' are required.");
    }

    /* remaining are the control points (in qty of 4) and possibly a filename */
    nCP = (argc - 1) / 4;
    for (i = 0; i < nCP; i++) {
      oldCP[i].x = strtol(argv[i * 2 + 1], &endptr, 10);
      oldCP[i].y = strtol(argv[i * 2 + 2], &endptr, 10);
      newCP[i].x = strtol(argv[4 * nCP / 2 + i * 2 + 1], &endptr, 10);
      newCP[i].y = strtol(argv[4 * nCP / 2 + i * 2 + 2], &endptr, 10);
    }

    /* only parameter allowed is optional filespec */
    if (argc - 1 == 4 * nCP) {
        pCmdline->filename = "-";
    } else
    if (argc - 2 == 4 * nCP) {
        pCmdline->filename = argv[nCP * 4 + 1];
    } else {
        pm_error ("Incorrect number of coordinates.");
    }
}



static void
prepTrig(int const wd,
         int const ht) {

/* create triangles using control points */

  point rtl1, rtr1, rbl1, rbr1;
  point rtl2, rtr2, rbl2, rbr2;
  point c1p1, c1p2, c1p3, c1p4;
  point c2p1, c2p2, c2p3, c2p4;
  line l1, l2;
  point p0;

  makepoint (&rtl1, 0.0 + tiny(), 0.0 + tiny());
  makepoint (&rtr1, (double) wd - 1.0 + tiny(), 0.0 + tiny());
  makepoint (&rbl1, 0.0 + tiny(), (double) ht - 1.0 + tiny());
  makepoint (&rbr1, (double) wd - 1.0 + tiny(), (double) ht - 1.0 + tiny());

  makepoint (&rtl2, 0.0 + tiny(), 0.0 + tiny());
  makepoint (&rtr2, (double) wd - 1.0 + tiny(), 0.0 + tiny());
  makepoint (&rbl2, 0.0 + tiny(), (double) ht - 1.0 + tiny());
  makepoint (&rbr2, (double) wd - 1.0 + tiny(), (double) ht - 1.0 + tiny());

  if (nCP == 1) {
    copypoint (&c1p1, &oldCP[0]);
    copypoint (&c2p1, &newCP[0]);

    /* connect control point to all corners to get 4 triangles */
    SIDETRIANGLE(nCP,tri1s[0],c1p1,p0,p0,p0,rbl1,rtl1,tri2s[0],c2p1,p0,p0,p0,rbl2,rtl2); /* left side triangle */
    SIDETRIANGLE(nCP,tri1s[1],c1p1,p0,p0,p0,rtl1,rtr1,tri2s[1],c2p1,p0,p0,p0,rtl2,rtr2); /* top side triangle */
    SIDETRIANGLE(nCP,tri1s[2],c1p1,p0,p0,p0,rtr1,rbr1,tri2s[2],c2p1,p0,p0,p0,rtr2,rbr2); /* right side triangle */
    SIDETRIANGLE(nCP,tri1s[3],c1p1,p0,p0,p0,rbr1,rbl1,tri2s[3],c2p1,p0,p0,p0,rbr2,rbl2); /* bottom side triangle */

    nTri = 4;
  } else

  if (nCP == 2) {
    copypoint (&c1p1, &oldCP[0]);
    copypoint (&c1p2, &oldCP[1]);
    copypoint (&c2p1, &newCP[0]);
    copypoint (&c2p2, &newCP[1]);

    /* check for hor/ver edges */
    angle (&c1p1, &c1p2);
    angle (&c2p1, &c2p2);

    /* connect two control points to corners to get 6 triangles */
    SIDETRIANGLE(nCP,tri1s[0],c1p1,c1p2,p0,p0,rbl1,rtl1,tri2s[0],c2p1,c2p2,p0,p0,rbl2,rtl2); /* left side */
    SIDETRIANGLE(nCP,tri1s[1],c1p1,c1p2,p0,p0,rtl1,rtr1,tri2s[1],c2p1,c2p2,p0,p0,rtl2,rtr2); /* top side */
    SIDETRIANGLE(nCP,tri1s[2],c1p1,c1p2,p0,p0,rtr1,rbr1,tri2s[2],c2p1,c2p2,p0,p0,rtr2,rbr2); /* right side */
    SIDETRIANGLE(nCP,tri1s[3],c1p1,c1p2,p0,p0,rbr1,rbl1,tri2s[3],c2p1,c2p2,p0,p0,rbr2,rbl2); /* bottom side */

    /* edge to corner triangles */
    EDGETRIANGLE(tri1s[4],c1p1,c1p2,rtl1,rtr1,rbl1,rbr1,tri2s[4],c2p1,c2p2,rtl2,rtr2,rbl2,rbr2);
    EDGETRIANGLE(tri1s[5],c1p2,c1p1,rtl1,rtr1,rbl1,rbr1,tri2s[5],c2p2,c2p1,rtl2,rtr2,rbl2,rbr2);

    nTri = 6;
  } else

  if (nCP == 3) {
    copypoint (&c1p1, &oldCP[0]);
    copypoint (&c1p2, &oldCP[1]);
    copypoint (&c1p3, &oldCP[2]);

    copypoint (&c2p1, &newCP[0]);
    copypoint (&c2p2, &newCP[1]);
    copypoint (&c2p3, &newCP[2]);

    /* check for hor/ver edges */
    angle (&c1p1, &c1p2);
    angle (&c1p2, &c1p3);
    angle (&c1p3, &c1p1);

    angle (&c2p1, &c2p2);
    angle (&c2p2, &c2p3);
    angle (&c2p3, &c2p1);

    if (windtriangle (&tri1s[0], &c1p1, &c1p2, &c1p3)) {
        maketriangle (&tri2s[0], &c2p1, &c2p2, &c2p3);
    } else {
        maketriangle (&tri2s[0], &c2p1, &c2p3, &c2p2);
    }

    copypoint (&c1p1, &tri1s[0].p1);
    copypoint (&c1p2, &tri1s[0].p2);
    copypoint (&c1p3, &tri1s[0].p3);

    copypoint (&c2p1, &tri2s[0].p1);
    copypoint (&c2p2, &tri2s[0].p2);
    copypoint (&c2p3, &tri2s[0].p3);

    /* point to side triangles */
    SIDETRIANGLE(nCP,tri1s[1],c1p1,c1p2,c1p3,p0,rbl1,rtl1,tri2s[1],c2p1,c2p2,c2p3,p0,rbl2,rtl2); /* left side */
    SIDETRIANGLE(nCP,tri1s[2],c1p1,c1p2,c1p3,p0,rtl1,rtr1,tri2s[2],c2p1,c2p2,c2p3,p0,rtl2,rtr2); /* top side */
    SIDETRIANGLE(nCP,tri1s[3],c1p1,c1p2,c1p3,p0,rtr1,rbr1,tri2s[3],c2p1,c2p2,c2p3,p0,rtr2,rbr2); /* right side */
    SIDETRIANGLE(nCP,tri1s[4],c1p1,c1p2,c1p3,p0,rbr1,rbl1,tri2s[4],c2p1,c2p2,c2p3,p0,rbr2,rbl2); /* bottom side */

    /* edge to corner triangles */
    EDGETRIANGLE(tri1s[5],c1p1,c1p2,rtl1,rtr1,rbl1,rbr1,tri2s[5],c2p1,c2p2,rtl2,rtr2,rbl2,rbr2);
    EDGETRIANGLE(tri1s[6],c1p2,c1p3,rtl1,rtr1,rbl1,rbr1,tri2s[6],c2p2,c2p3,rtl2,rtr2,rbl2,rbr2);
    EDGETRIANGLE(tri1s[7],c1p3,c1p1,rtl1,rtr1,rbl1,rbr1,tri2s[7],c2p3,c2p1,rtl2,rtr2,rbl2,rbr2);

    nTri = 8;
  } else

  if (nCP == 4) {
    copypoint (&c1p1, &oldCP[0]);
    copypoint (&c1p2, &oldCP[1]);
    copypoint (&c1p3, &oldCP[2]);
    copypoint (&c1p4, &oldCP[3]);

    copypoint (&c2p1, &newCP[0]);
    copypoint (&c2p2, &newCP[1]);
    copypoint (&c2p3, &newCP[2]);
    copypoint (&c2p4, &newCP[3]);

    /* check for hor/ver edges */
    angle (&c1p1, &c1p2);
    angle (&c1p2, &c1p3);
    angle (&c1p3, &c1p4);
    angle (&c1p4, &c1p1);
    angle (&c1p1, &c1p3);
    angle (&c1p2, &c1p4);

    angle (&c2p1, &c2p2);
    angle (&c2p2, &c2p3);
    angle (&c2p3, &c2p4);
    angle (&c2p4, &c2p1);
    angle (&c2p1, &c2p3);
    angle (&c2p2, &c2p4);

    /*--------------------------------------------------------------------*/
    /*        -1-      -2-        -3-      -4-        -5-      -6-        */
    /*       1   2    1   3      1   2    1   4      1   3    1   4       */
    /*         X        X          X        X          X        X         */
    /*       3   4    2   4      4   3    2   3      4   2    3   2       */
    /*--------------------------------------------------------------------*/

    /* center two triangles */
    makeline (&l1, &c1p1, &c1p4);
    makeline (&l2, &c1p2, &c1p3);
    if (intersect(&l1, &l2, &p0)) {
      if (windtriangle (&tri1s[0], &c1p1, &c1p2, &c1p3)) {
        maketriangle (&tri1s[1], &c1p4, &c1p3, &c1p2);
        maketriangle (&tri2s[0], &c2p1, &c2p2, &c2p3);
        maketriangle (&tri2s[1], &c2p4, &c2p3, &c2p2);
      } else {
        maketriangle (&tri1s[1], &c1p4, &c1p2, &c1p3);
        maketriangle (&tri2s[0], &c2p1, &c2p3, &c2p2);
        maketriangle (&tri2s[1], &c2p4, &c2p2, &c2p3);
      }
    }
    makeline (&l1, &c1p1, &c1p3);
    makeline (&l2, &c1p2, &c1p4);
    if (intersect(&l1, &l2, &p0)) {
      if (windtriangle (&tri1s[0], &c1p1, &c1p2, &c1p4)) {
        maketriangle (&tri1s[1], &c1p3, &c1p4, &c1p2);
        maketriangle (&tri2s[0], &c2p1, &c2p2, &c2p4);
        maketriangle (&tri2s[1], &c2p3, &c2p4, &c2p2);
      } else {
        maketriangle (&tri1s[1], &c1p3, &c1p2, &c1p4);
        maketriangle (&tri2s[0], &c2p1, &c2p4, &c2p2);
        maketriangle (&tri2s[1], &c2p3, &c2p2, &c2p4);
      }
    }
    makeline (&l1, &c1p1, &c1p2);
    makeline (&l2, &c1p3, &c1p4);
    if (intersect(&l1, &l2, &p0)) {
      if (windtriangle (&tri1s[0], &c1p1, &c1p3, &c1p4)) {
        maketriangle (&tri1s[1], &c1p2, &c1p4, &c1p3);
        maketriangle (&tri2s[0], &c2p1, &c2p3, &c2p4);
        maketriangle (&tri2s[1], &c2p2, &c2p4, &c2p3);
      } else {
        maketriangle (&tri1s[1], &c1p2, &c1p3, &c1p4);
        maketriangle (&tri2s[0], &c2p1, &c2p4, &c2p3);
        maketriangle (&tri2s[1], &c2p2, &c2p3, &c2p4);
      }
    }

    /* control points in correct orientation */
    copypoint (&c1p1, &tri1s[0].p1);
    copypoint (&c1p2, &tri1s[0].p2);
    copypoint (&c1p3, &tri1s[0].p3);
    copypoint (&c1p4, &tri1s[1].p1);
    copypoint (&c2p1, &tri2s[0].p1);
    copypoint (&c2p2, &tri2s[0].p2);
    copypoint (&c2p3, &tri2s[0].p3);
    copypoint (&c2p4, &tri2s[1].p1);

    /* triangle from triangle point to side of image */
    SIDETRIANGLE(nCP,tri1s[2],c1p1,c1p2,c1p3,c1p4,rbl1,rtl1,tri2s[2],c2p1,c2p2,c2p3,c2p4,rbl2,rtl2); /* left side triangle */
    SIDETRIANGLE(nCP,tri1s[3],c1p1,c1p2,c1p3,c1p4,rtl1,rtr1,tri2s[3],c2p1,c2p2,c2p3,c2p4,rtl2,rtr2); /* top side triangle */
    SIDETRIANGLE(nCP,tri1s[4],c1p1,c1p2,c1p3,c1p4,rtr1,rbr1,tri2s[4],c2p1,c2p2,c2p3,c2p4,rtr2,rbr2); /* right side triangle */
    SIDETRIANGLE(nCP,tri1s[5],c1p1,c1p2,c1p3,c1p4,rbr1,rbl1,tri2s[5],c2p1,c2p2,c2p3,c2p4,rbr2,rbl2); /* bottom side triangle */

    /*--------------------------------------------------------------------*/
    /*        -1-      -2-        -3-      -4-        -5-      -6-        */
    /*       1   2    1   3      1   2    1   4      1   3    1   4       */
    /*         X        X          X        X          X        X         */
    /*       3   4    2   4      4   3    2   3      4   2    3   2       */
    /*--------------------------------------------------------------------*/

    /* edge-corner triangles */
    EDGETRIANGLE(tri1s[6],c1p1,c1p2,rtl1,rtr1,rbl1,rbr1,tri2s[6],c2p1,c2p2,rtl2,rtr2,rbl2,rbr2);
    EDGETRIANGLE(tri1s[7],c1p2,c1p4,rtl1,rtr1,rbl1,rbr1,tri2s[7],c2p2,c2p4,rtl2,rtr2,rbl2,rbr2);
    EDGETRIANGLE(tri1s[8],c1p4,c1p3,rtl1,rtr1,rbl1,rbr1,tri2s[8],c2p4,c2p3,rtl2,rtr2,rbl2,rbr2);
    EDGETRIANGLE(tri1s[9],c1p3,c1p1,rtl1,rtr1,rbl1,rbr1,tri2s[9],c2p3,c2p1,rtl2,rtr2,rbl2,rbr2);

    nTri = 10;
  }

  return;
}



static void
prepQuad(void) {

/* order quad control points */

  double d01, d12, d20;
  line l1, l2;
  point mid;
  triangle tri;

  if (nCP == 1) {

    /* create a rectangle from top-left corner of image and control point */
    QUAD_RECT(quad1,0.0,oldCP[0].x,0.0,oldCP[0].y);
    QUAD_RECT(quad2,0.0,newCP[0].x,0.0,newCP[0].y);

  } else

  if (nCP == 2) {

    /* create a rectangle with the two points as opposite corners */
    if ((oldCP[0].x < oldCP[1].x) && (oldCP[0].y < oldCP[1].y)) { /* top-left and bottom-right */
      QUAD_RECT(quad1,oldCP[0].x,oldCP[1].x,oldCP[0].y,oldCP[1].y);
    } else
    if ((oldCP[0].x > oldCP[1].x) && (oldCP[0].y < oldCP[1].y)) { /* top-right and bottom-left */
      QUAD_RECT(quad1,oldCP[1].x,oldCP[0].x,oldCP[0].y,oldCP[1].y);
    } else
    if ((oldCP[0].x < oldCP[1].x) && (oldCP[0].y < oldCP[1].y)) { /* bottom-left and top-right */
      QUAD_RECT(quad1,oldCP[0].x,oldCP[1].x,oldCP[1].y,oldCP[0].y);
    } else
    if ((oldCP[0].x > oldCP[1].x) && (oldCP[0].y < oldCP[1].y)) { /* bottom-right and top-left */
      QUAD_RECT(quad1,oldCP[1].x,oldCP[0].x,oldCP[1].y,oldCP[0].y);
    }

    if ((newCP[0].x < newCP[1].x) && (newCP[0].y < newCP[1].y)) { /* top-left and bottom-right */
      QUAD_RECT(quad2,newCP[0].x,newCP[1].x,newCP[0].y,newCP[1].y);
    } else
    if ((newCP[0].x > newCP[1].x) && (newCP[0].y < newCP[1].y)) { /* top-right and bottom-left */
      QUAD_RECT(quad2,newCP[1].x,newCP[0].x,newCP[0].y,newCP[1].y);
    } else
    if ((newCP[0].x < newCP[1].x) && (newCP[0].y < newCP[1].y)) { /* bottom-left and top-right */
      QUAD_RECT(quad2,newCP[0].x,newCP[1].x,newCP[1].y,newCP[0].y);
    } else
    if ((newCP[0].x > newCP[1].x) && (newCP[0].y < newCP[1].y)) { /* bottom-right and top-left */
      QUAD_RECT(quad2,newCP[1].x,newCP[0].x,newCP[1].y,newCP[0].y);
    }

  } else {

    if (nCP == 3) {

      /* add the fourth corner of a parallelogram */
      /* diagonal of the parallelogram is the two control points furthest apart */

      d01 = distance (&newCP[0], &newCP[1]);
      d12 = distance (&newCP[1], &newCP[2]);
      d20 = distance (&newCP[2], &newCP[0]);

      if ((d01 > d12) && (d01 > d20)) {
        oldCP[3].x = oldCP[0].x + oldCP[1].x - oldCP[2].x;
        oldCP[3].y = oldCP[0].y + oldCP[1].y - oldCP[2].y;
      } else
      if (d12 > d20) {
        oldCP[3].x = oldCP[1].x + oldCP[2].x - oldCP[0].x;
        oldCP[3].y = oldCP[1].y + oldCP[2].y - oldCP[0].y;
      } else {
        oldCP[3].x = oldCP[2].x + oldCP[0].x - oldCP[1].x;
        oldCP[3].y = oldCP[2].y + oldCP[0].y - oldCP[1].y;
      }

      if ((d01 > d12) && (d01 > d20)) {
        newCP[3].x = newCP[0].x + newCP[1].x - newCP[2].x;
        newCP[3].y = newCP[0].y + newCP[1].y - newCP[2].y;
      } else
      if (d12 > d20) {
        newCP[3].x = newCP[1].x + newCP[2].x - newCP[0].x;
        newCP[3].y = newCP[1].y + newCP[2].y - newCP[0].y;
      } else {
        newCP[3].x = newCP[2].x + newCP[0].x - newCP[1].x;
        newCP[3].y = newCP[2].y + newCP[0].y - newCP[1].y;
      }

    } /* end nCP = 3 */

    /* nCP = 3 or 4 */

    /* find the intersection of the diagonals */
    makeline (&l1, &oldCP[0], &oldCP[1]);
    makeline (&l2, &oldCP[2], &oldCP[3]);
    if (intersect(&l1, &l2, &mid)) {
      QUAD_CORNER(quad1,oldCP[0],oldCP[1],oldCP[2],oldCP[3],mid,tri);
    }
    else {
      makeline (&l1, &oldCP[0], &oldCP[2]);
      makeline (&l2, &oldCP[1], &oldCP[3]);
      if (intersect(&l1, &l2, &mid)) {
        QUAD_CORNER(quad1,oldCP[0],oldCP[2],oldCP[1],oldCP[3],mid,tri);
      }
      else {
        makeline (&l1, &oldCP[0], &oldCP[3]);
        makeline (&l2, &oldCP[1], &oldCP[2]);
        if (intersect(&l1, &l2, &mid)) {
          QUAD_CORNER(quad1,oldCP[0],oldCP[3],oldCP[1],oldCP[2],mid,tri);
        }
        else {
          pm_error ("pamrubber: The four old control points don't seem to be corners.");
        }
      }
    }

    /* repeat for the "to-be" control points */
    makeline (&l1, &newCP[0], &newCP[1]);
    makeline (&l2, &newCP[2], &newCP[3]);
    if (intersect(&l1, &l2, &mid)) {
      QUAD_CORNER(quad2,newCP[0],newCP[1],newCP[2],newCP[3],mid,tri);
    }
    else {
      makeline (&l1, &newCP[0], &newCP[2]);
      makeline (&l2, &newCP[1], &newCP[3]);
      if (intersect(&l1, &l2, &mid)) {
        QUAD_CORNER(quad2,newCP[0],newCP[2],newCP[1],newCP[3],mid,tri);
      }
      else {
        makeline (&l1, &newCP[0], &newCP[3]);
        makeline (&l2, &newCP[1], &newCP[2]);
        if (intersect(&l1, &l2, &mid)) {
          QUAD_CORNER(quad2,newCP[0],newCP[3],newCP[1],newCP[2],mid,tri);
        }
        else {
          pm_error ("pamrubber: The four new control points don't seem to be corners.");
        }
      }
    }
  }
}



static void
warpTrig(point * const p2,
         point * const p1) {

/* map target to source by triangulation */

  point e1p1, e1p2, e1p3;
  point e2p1, e2p2, e2p3;
  line lp, le;
  line l1, l2, l3;
  double d1, d2, d3;
  int i;

  /* find in which triangle p2 lies */
  for (i = 0; i < nTri; i++) {
    if (insidetri (&tri2s[i], p2))
      break;
  }

  if (i == nTri) {
    p1->x = 0.0;
    p1->y = 0.0;
    return;
  }

  /* where in triangle is point */
  d1 = fabs (p2->x - tri2s[i].p1.x) + fabs (p2->y - tri2s[i].p1.y);
  d2 = fabs (p2->x - tri2s[i].p2.x) + fabs (p2->y - tri2s[i].p2.y);
  d3 = fabs (p2->x - tri2s[i].p3.x) + fabs (p2->y - tri2s[i].p3.y);

  /* line through p1 and p intersecting with edge p2-p3 */
  makeline (&lp, &tri2s[i].p1, p2);
  makeline (&le, &tri2s[i].p2, &tri2s[i].p3);
  intersect (&lp, &le, &e2p1);

  /* line through p2 and p intersecting with edge p3-p1 */
  makeline (&lp, &tri2s[i].p2, p2);
  makeline (&le, &tri2s[i].p3, &tri2s[i].p1);
  intersect (&lp, &le, &e2p2);

  /* line through p3 and p intersecting with edge p1-p2 */
  makeline (&lp, &tri2s[i].p3, p2);
  makeline (&le, &tri2s[i].p1, &tri2s[i].p2);
  intersect (&lp, &le, &e2p3);

  /* map target control points to source control points */
  e1p1.x = tri1s[i].p2.x + (e2p1.x - tri2s[i].p2.x)/(tri2s[i].p3.x - tri2s[i].p2.x) * (tri1s[i].p3.x - tri1s[i].p2.x);
  e1p1.y = tri1s[i].p2.y + (e2p1.y - tri2s[i].p2.y)/(tri2s[i].p3.y - tri2s[i].p2.y) * (tri1s[i].p3.y - tri1s[i].p2.y);
  e1p2.x = tri1s[i].p3.x + (e2p2.x - tri2s[i].p3.x)/(tri2s[i].p1.x - tri2s[i].p3.x) * (tri1s[i].p1.x - tri1s[i].p3.x);
  e1p2.y = tri1s[i].p3.y + (e2p2.y - tri2s[i].p3.y)/(tri2s[i].p1.y - tri2s[i].p3.y) * (tri1s[i].p1.y - tri1s[i].p3.y);
  e1p3.x = tri1s[i].p1.x + (e2p3.x - tri2s[i].p1.x)/(tri2s[i].p2.x - tri2s[i].p1.x) * (tri1s[i].p2.x - tri1s[i].p1.x);
  e1p3.y = tri1s[i].p1.y + (e2p3.y - tri2s[i].p1.y)/(tri2s[i].p2.y - tri2s[i].p1.y) * (tri1s[i].p2.y - tri1s[i].p1.y);

  /* intersect grid lines in source */
  makeline (&l1, &tri1s[i].p1, &e1p1);
  makeline (&l2, &tri1s[i].p2, &e1p2);
  makeline (&l3, &tri1s[i].p3, &e1p3);

  if ((d1 < d2) && (d1 < d3)) {
    intersect (&l2, &l3, p1);
  } else
  if (d2 < d3) {
    intersect (&l1, &l3, p1);
  } else {
    intersect (&l1, &l2, p1);
  }
}



static void
warpQuad(point * const p2,
         point * const p1) {

/* map target to source for quad control points */

  point h2, v2;
  point c1tl, c1tr, c1bl, c1br;
  point c2tl, c2tr, c2bl, c2br;
  point e1t, e1b, e1l, e1r;
  point e2t, e2b, e2l, e2r;
  line l2t, l2b, l2l, l2r;
  line lh, lv;

  copypoint (&c1tl, &quad1[TL]);
  copypoint (&c1tr, &quad1[TR]);
  copypoint (&c1bl, &quad1[BL]);
  copypoint (&c1br, &quad1[BR]);

  copypoint (&c2tl, &quad2[TL]);
  copypoint (&c2tr, &quad2[TR]);
  copypoint (&c2bl, &quad2[BL]);
  copypoint (&c2br, &quad2[BR]);

  makeline (&l2t, &c2tl, &c2tr);
  makeline (&l2b, &c2bl, &c2br);
  makeline (&l2l, &c2tl, &c2bl);
  makeline (&l2r, &c2tr, &c2br);

  /* find intersections of lines through control points */
  intersect (&l2t, &l2b, &h2);
  intersect (&l2l, &l2r, &v2);

  /* find intersections of axes through P with control point box */
  makeline (&lv, p2, &v2);
  intersect (&l2t, &lv, &e2t);
  intersect (&l2b, &lv, &e2b);

  makeline (&lh, p2, &h2);
  intersect (&l2l, &lh, &e2l);
  intersect (&l2r, &lh, &e2r);

  /* map target control points to source control points */
  e1t.x = c1tl.x + (e2t.x - c2tl.x)/(c2tr.x - c2tl.x) * (c1tr.x - c1tl.x);
  if (c1tl.y == c1tr.y)
    e1t.y = c1tl.y;
  else
    e1t.y = c1tl.y + (e2t.x - c2tl.x)/(c2tr.x - c2tl.x) * (c1tr.y - c1tl.y);

  e1b.x = c1bl.x + (e2b.x - c2bl.x)/(c2br.x - c2bl.x) * (c1br.x - c1bl.x);
  if (c1bl.y == c1br.y)
    e1b.y = c1bl.y;
  else
    e1b.y = c1bl.y + (e2b.x - c2bl.x)/(c2br.x - c2bl.x) * (c1br.y - c1bl.y);

  if (c1tl.x == c1bl.x)
    e1l.x = c1tl.x;
  else
    e1l.x = c1tl.x + (e2l.y - c2tl.y)/(c2bl.y - c2tl.y) * (c1bl.x - c1tl.x);
  e1l.y = c1tl.y + (e2l.y - c2tl.y)/(c2bl.y - c2tl.y) * (c1bl.y - c1tl.y);

  if (c1tr.x == c1br.x)
    e1r.x = c1tr.x;
  else
    e1r.x = c1tr.x + (e2r.y - c2tr.y)/(c2br.y - c2tr.y) * (c1br.x - c1tr.x);
  e1r.y = c1tr.y + (e2r.y - c2tr.y)/(c2br.y - c2tr.y) * (c1br.y - c1tr.y);

  /* intersect grid lines in source */
  makeline (&lv, &e1t, &e1b);
  makeline (&lh, &e1l, &e1r);
  intersect (&lh, &lv, p1);
}



int
main(int argc, const char ** const argv) {

  struct cmdlineInfo cmdline;
  FILE * rdFile;
  struct pam inpam, outpam;
  tuple ** rdTuples;
  tuple ** wrTuples;
  tuple white;
  int row, col;

  point p1, p2;
  int p2x, p2y;
  double rx, ry;
  double pix;
  int i;

  /* initialize random generator */
  srand ( time(NULL) );

  /* get parameters, open input file, read header */
  pm_proginit(&argc, argv);
  parseCmdline(argc, argv, &cmdline);

  rdFile = pm_openr(cmdline.filename);

  rdTuples = pnm_readpam (rdFile, &inpam, PAM_STRUCT_SIZE(tuple_type));
  if (rdTuples == NULL) {
    pm_error ("Out of memory.");
  }
  outpam = inpam;  /* initial value */
  outpam.file = stdout;

  if (cmdline.verbose) {
    fprintf (stderr, "type = %s\n", inpam.tuple_type);
    fprintf (stderr, "width = %d\n", inpam.width);
    fprintf (stderr, "height = %d\n", inpam.height);
    fprintf (stderr, "depth = %d\n", inpam.depth);
    fprintf (stderr, "maxval = %d\n", (int) inpam.maxval);
  }

  wrTuples = pnm_allocpamarray(&outpam);

  white = pnm_allocpamtuple(&outpam);
  for (i = 0; i < inpam.depth; i++)
    white[i] = outpam.maxval;
  for (row = 0; row < inpam.height; row++)  {
    for (col = 0; col < inpam.width; col++)  {
      pnm_assigntuple(&outpam, wrTuples[row][col], white);
    }
  }

  if (cmdline.tri)
    prepTrig(inpam.width, inpam.height);
  if (cmdline.quad)
    prepQuad();

  for (p2y = 0; p2y < inpam.height; p2y++)
  {
    for (p2x = 0; p2x < inpam.width; p2x++)
    {
      makepoint (&p2, (double) p2x, (double) p2y);
      if (cmdline.quad)
        warpQuad (&p2, &p1);
      if (cmdline.tri)
        warpTrig (&p2, &p1);

      p1.x += 1E-3;
      p1.y += 1E-3;
      if ((p1.x >= 0.0) && (p1.x < (double) inpam.width - 0.5) &&
          (p1.y >= 0.0) && (p1.y < (double) inpam.height - 0.5))
      {
        for (i = 0; i < inpam.depth; i++) {
          if (!cmdline.linear) {
            pix = rdTuples[(int) floor(p1.y + 0.5)][(int) floor(p1.x + 0.5)][i];
          } else {
            rx = p1.x - floor(p1.x);
            ry = p1.y - floor(p1.y);
            pix = 0.0;
            pix += (1.0 - rx) * (1.0 - ry) * rdTuples[(int) floor(p1.y)][(int) floor(p1.x)][i];
            pix += rx * (1.0 - ry) * rdTuples[(int) floor(p1.y)][(int) floor(p1.x) + 1][i];
            pix += (1.0 - rx) * ry * rdTuples[(int) floor(p1.y) + 1][(int) floor(p1.x)][i];
            pix += rx * ry * rdTuples[(int) floor(p1.y) + 1][(int) floor(p1.x) + 1][i];
          }
          wrTuples[p2y][p2x][i] = (int) floor(pix);
        } /* end for */
      }
    }
  }

  pnm_createBlackTuple(&outpam, &black);

  if (cmdline.frame) {
    if (cmdline.quad) {
        drawExtendedLine(&outpam, wrTuples, quad2[0], quad2[1]);
        drawExtendedLine(&outpam, wrTuples, quad2[2], quad2[3]);
        drawExtendedLine(&outpam, wrTuples, quad2[0], quad2[2]);
        drawExtendedLine(&outpam, wrTuples, quad2[1], quad2[3]);
    }
    if (cmdline.tri) {
        unsigned int i;
        for (i = 0; i < nTri; ++i) {
            drawClippedTriangle(&outpam, wrTuples, tri2s[i]);
        }
    }
  }

  pnm_writepam( &outpam, wrTuples );
  pnm_freepamarray(wrTuples, &outpam);
  pnm_freepamarray(rdTuples, &inpam);

  pm_close( inpam.file );
  pm_close( outpam.file );

  return 0;
}
