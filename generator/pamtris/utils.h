#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED

#include "fract.h"

void
gen_steps(const int32_t * begin,
          const int32_t * end,
          fract *         out,
          uint8_t         elements,
          int32_t         divisor);

void
step_up(fract *       vars,
        const fract * steps,
        uint8_t       elements,
        int32_t       divisor);

void
multi_step_up(fract *       vars,
              const fract * steps,
              uint8_t       elements,
              int32_t       times,
              int32_t       divisor);

void
fract_to_int32_array(const fract * in,
                     int32_t     * out,
                     uint8_t       elements);

void
int32_to_fract_array(const int32_t * in,
                     fract *         out,
                     uint8_t         elements);

void
sort3(uint8_t *       index_array,
      const int32_t * y_array,
      const int32_t * x_array);

#endif
