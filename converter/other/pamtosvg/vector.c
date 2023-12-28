/* vector.c: vector/point operations. */

#define _XOPEN_SOURCE 500  /* get M_PI in math.h */
#include <math.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#include "pm_c_util.h"

#include "vector.h"
#include "message.h"
#include "epsilon.h"

static float
acosD(float               const v,
      at_exception_type * const excepP) {

    float vAdj;
    float a;
    float retval;

    if (epsilon_equal(v, 1.0))
        vAdj = 1.0;
    else if (epsilon_equal(v, -1.0))
        vAdj = -1.0;
    else
        vAdj = v;

    errno = 0;
    a = acos(vAdj);
    if (errno == ERANGE || errno == EDOM) {
        at_exception_fatal(excepP, strerror(errno));
        retval = 0.0;
    } else
        retval = a * 180.0 / M_PI;

    return retval;
}



Vector
vector_fromPoint(Point const c) {
/* Vector corresponding to point 'c', taken as a vector from the origin.  */

    Vector v;

    v.dx = c.x;
    v.dy = c.y;
    v.dz = c.z;

    return v;
}



Vector
vector_fromTwoPoints(Point const c1,
                     Point const c2) {

    Vector retval;

    retval.dx = c1.x - c2.x;
    retval.dy = c1.y - c2.y;
    retval.dz = c1.z - c2.z;

    return retval;
}



/* And the converse: given a vector, return the corresponding point.  */

Point
vector_toPoint_point(Vector const v) {
/* vector as a point, i.e., a displacement from the origin.  */

    Point coord;

    coord.x = v.dx;
    coord.y = v.dy;
    coord.z = v.dz;

    return coord;
}



float
vector_magnitude(Vector const v) {

    return sqrt(SQR(v.dx) + SQR(v.dy) + SQR(v.dz));
}



Vector
vector_normalized(Vector const v) {

    Vector new_v;
    float const m = vector_magnitude(v);

    if (m > 0.0) {
        new_v.dx = v.dx / m;
        new_v.dy = v.dy / m;
        new_v.dz = v.dz / m;
    } else {
        new_v.dx = v.dx;
        new_v.dy = v.dy;
        new_v.dz = v.dz;
    }

    return new_v;
}



Vector
vector_sum(Vector const addend,
           Vector const adder) {

    Vector retval;

    retval.dx = addend.dx + adder.dx;
    retval.dy = addend.dy + adder.dy;
    retval.dz = addend.dz + adder.dz;

    return retval;
}



float
vector_dotProduct(Vector const v1,
                  Vector const v2) {

    return v1.dx * v2.dx + v1.dy * v2.dy + v1.dz * v2.dz;
}



Vector
vector_scaled(Vector const v,
              float  const r) {

    Vector retval;

    retval.dx = v.dx * r;
    retval.dy = v.dy * r;
    retval.dz = v.dz * r;

    return retval;
}



float
vector_angle(Vector              const inVector,
             Vector              const outVector,
             at_exception_type * const exP) {

/* The angle between 'inVector' and 'outVector' in degrees, in the range zero
   to 180.
*/

    Vector const v1 = vector_normalized(inVector);
    Vector const v2 = vector_normalized(outVector);

    return acosD(vector_dotProduct(v2, v1), exP);
}



Point
vector_sumPoint(Point const c,
                Vector      const v) {

    Point retval;

    retval.x = c.x + v.dx;
    retval.y = c.y + v.dy;
    retval.z = c.z + v.dz;

    return retval;
}



Point
vector_diffPoint(Point  const c,
                 Vector const v) {

    Point retval;

    retval.x = c.x - v.dx;
    retval.y = c.y - v.dy;
    retval.z = c.z - v.dz;

    return retval;
}



Vector
vector_IPointDiff(pm_pixelcoord const coord1,
                  pm_pixelcoord const coord2) {

    Vector retval;

    retval.dx = (int) (coord1.col - coord2.col);
    retval.dy = (int) (coord1.row - coord2.row);
    retval.dz = 0.0;

    return retval;
}



pm_pixelcoord
vector_sumIntPoint(pm_pixelcoord const c,
                   Vector        const v) {

/* This returns the rounded sum.  */

    pm_pixelcoord retval;

    retval.col = ROUND ((float) c.col + v.dx);
    retval.row = ROUND ((float) c.row + v.dy);

    return retval;
}



Vector
vector_abs(Vector const v) {

/* First-quadrant mirror of 'v' (both components unsigned) */

    Vector retval;

    retval.dx = (float) fabs (v.dx);
    retval.dy = (float) fabs (v.dy);
    retval.dz = (float) fabs (v.dz);

    return retval;
}



Vector
vector_pointDirection(Point const final,
                      Point const initial) {

    return vector_normalized(vector_fromTwoPoints(final, initial));
}



Vector
vector_horizontal(void) {

    Vector retval;

    retval.dx = 1.0;
    retval.dy = 0.0;
    retval.dz = 0.0;

    return retval;
}



Vector
vector_zero(void) {

    Vector retval;

    retval.dx = 0.0;
    retval.dy = 0.0;
    retval.dz = 0.0;

    return retval;
}



bool
vector_equal(Vector const comparand,
             Vector const comparator) {

    return
        epsilon_equal(comparand.dx, comparator.dx)
        &&
        epsilon_equal(comparand.dy, comparator.dy)
        &&
        epsilon_equal(comparand.dz, comparator.dz)
        ;
}



