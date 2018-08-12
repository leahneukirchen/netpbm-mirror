#ifndef FRACT_H_INCLUDED
#define FRACT_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>


typedef struct {
/*----------------------------------------------------------------------------
    This struct and the functions that manipulate variables of this type act
    as a substitute for floating point computations. Here, whenever we need a
    value with a fractional component, we represent it using two parts: 1. An
    integer part, called the "quotient", and 2. A fractional part, which is
    itself composed of a "remainder" (or "numerator") and a "divisor" (or
    "denominator"). The fract struct provides storage for the quotient and the
    remainder, but the divisor must be given separately (because it often
    happens in this program that whenever we are dealing with one variable of
    type fract, we are dealing with more of them at the same time, and they
    all have the same divisor).

    To be more precise, the way we actually use variables of this type works
    like this: We read integer values through standard input; When drawing
    triangles, we need need to calculate differences between some pairs of
    these input values and divide such differences by some other integer,
    which is the above mentioned divisor. That result is then used to compute
    successive interpolations between the two values for which we had
    originally calculated the difference, and is therefore called the
    "interpolation step". The values between which we wish to take successive
    interpolations are called the "initial value" and the "final value". The
    interpolation procedure works like this: First, we transform the initial
    value into a fract variable by equating the quotient of that variable to
    the initial value and assigning 0 to its remainder. Then, we successivelly
    apply the interpolation step to that variable through successive calls to
    step_up and/or multi_step_up until the quotient of the variable equals the
    final value. Each application of step_up or multi_step_up yields a
    particular linear interpolation between the initial and final values.

    If and only if a particular fract variable represents an interpolation
    step, the "negative_flag" field indicates whether the step is negative
    (i. e. negative_flag == true) or not (negative_flag == false). This is
    necessary in order to make sure that variables are "stepped up" in the
    appropriate direction, so to speak, as the field which stores the
    remainder in any fract variable, "r", is always equal to or above 0, and
    the quotient of a step may be 0, so the actual sign of the step value is
    not always discoverable through a simple examination of the sign of the
    quotient. On the other hand, if the variable does not represent an
    interpolation step, the negative_flag is meaningless.
-----------------------------------------------------------------------------*/
    int32_t q;     /* Quotient */
    int32_t r: 31; /* Remainder */
    bool    negative_flag: 1;
} fract;

#endif
