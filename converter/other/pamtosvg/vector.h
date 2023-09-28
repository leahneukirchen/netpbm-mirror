/* vector.h: operations on vectors and points. */

#ifndef VECTOR_H
#define VECTOR_H

#include "point.h"
#include "exception.h"

/* Our vectors are represented as displacements along the x and y axes.  */

typedef struct
{
  float dx, dy, dz;
} Vector;


Vector
vector_fromPoint(Point const c);

Vector
vector_fromTwoPoints(Point const c1,
                     Point const c2);

Point
vector_toPoint_point(Vector const v);


/* Definitions for these common operations can be found in any decent
   linear algebra book, and most calculus books.
*/

float
vector_magnitude(Vector const v);

Vector
vector_normalized(Vector const v);

Vector
vector_sum(Vector const addend,
           Vector const adder);

float
vector_dotProduct(Vector const v1,
                  Vector const v2);

Vector
vector_scaled(Vector const v,
              float  const r);

float
vector_angle(Vector              const inVector,
             Vector              const outVector,
             at_exception_type * const exP);

Point
vector_sumPoint(Point  const c,
                Vector const v);

Point
vector_diffPoint(Point  const c,
                 Vector const v);

pm_pixelcoord
vector_sumIntPoint(pm_pixelcoord const c,
                   Vector        const v);

Vector
vector_abs(Vector const v);

Vector
vector_pointDirection(Point const final,
                      Point const initial);


Vector
vector_IPointDiff(pm_pixelcoord const coord1,
                  pm_pixelcoord const coord2);

Vector
vector_horizontal(void);

Vector
vector_zero(void);

bool
vector_equal(Vector const comparand,
             Vector const comparator);

#endif
