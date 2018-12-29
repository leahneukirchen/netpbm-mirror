#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED

#include "varying.h"

void
prepare_for_interpolation(const varying * const begin,
                          const varying * const end,
                          varying *       const out,
                          int32_t               num_steps,
                          uint8_t         const elements);

varying
compute_varying_z(int32_t const input_z);

void
multiply_varying_array_by_varying(varying * const vars,
                                  varying   const divisor,
                                  uint8_t   const elements);

void
divide_varying_array_by_varying(varying * const vars,
                                varying   const divisor,
                                uint8_t   const elements);

varying
inverse_varying(varying const var);

varying
multiply_varyings(varying const a,
                  varying const b);

void
step_up(varying * const vars,
       uint8_t    const elements);

void
multi_step_up(varying * const vars,
             int32_t    const times,
             uint8_t    const elements);

void
int32_to_varying_array(const int32_t * const in,
                       varying *       const out,
                       uint8_t         const elements);

int32_t
round_varying(varying const var);

int64_t
geq_mask64(int64_t a, int64_t b);

void
sort3(uint8_t *       const index_array,
      const int32_t * const y_array,
      const int32_t * const x_array);

#endif
