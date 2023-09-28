#ifndef POINT_H_INCLUDED
#define POINT_H_INCLUDED

#include <stdbool.h>

typedef struct {
  float x, y, z;
} Point;

Point
point_make(float const x,
           float const y,
           float const z);

bool
point_equal(Point const comparand,
            Point const comparator);

Point
point_sum(Point const coord1,
          Point const coord2);

Point
point_scaled(Point const coord,
             float const r);

float
point_distance(Point const p1,
               Point const p2);

#endif
