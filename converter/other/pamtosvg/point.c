#include <stdbool.h>

#include "epsilon-equal.h"

#include "point.h"

float_coord
makePoint(float const x,
          float const y,
          float const z) {

    float_coord retval;

    retval.x = x;
    retval.y = y;
    retval.z = z;

    return retval;
}



bool
pointsEqual(float_coord const comparand,
            float_coord const comparator) {

    return
        epsilon_equal(comparand.x, comparator.x)
        &&
        epsilon_equal(comparand.y, comparator.y)
        &&
        epsilon_equal(comparand.z, comparator.z)
        ;
}



