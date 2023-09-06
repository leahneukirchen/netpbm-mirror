#ifndef POINT_H_INCLUDED
#define POINT_H_INCLUDED

#include <stdbool.h>

typedef struct {
  float x, y, z;
} float_coord;

float_coord
makePoint(float const x,
          float const y,
          float const z);

bool
pointsEqual(float_coord const comparand,
            float_coord const comparator);

#endif
