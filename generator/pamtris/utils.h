#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED

#include "fract.h"

void
gen_steps(const int32_t * const begin,
          const int32_t * const end,
          fract *         const out,
          uint8_t         const elements,
          int32_t         const divisor);

void
step_up(fract *       const vars,
        const fract * const steps,
        uint8_t       const elements,
        int32_t       const divisor);

void
multi_step_up(fract *       const vars,
              const fract * const steps,
              uint8_t       const elements,
              int32_t       const times,
              int32_t       const divisor);

void
fract_to_int32_array(const fract * const in,
                     int32_t     * const out,
                     uint8_t       const elements);

void
int32_to_fract_array(const int32_t * const in,
                     fract *         const out,
                     uint8_t         const elements);

void
sort3(uint8_t *       const index_array,
      const int32_t * const y_array,
      const int32_t * const x_array);

#endif
