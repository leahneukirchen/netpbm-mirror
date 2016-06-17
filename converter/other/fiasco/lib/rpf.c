/*
 *  rpf.c:      Conversion of float to reduced precision format values
 *
 *  Written by:     Stefan Frank
 *          Richard Krampfl
 *          Ullrich Hafner
 *      
 *  This file is part of FIASCO («F»ractal «I»mage «A»nd «S»equence «CO»dec)
 *  Copyright (C) 1994-2000 Ullrich Hafner <hafner@bigfoot.de>
 */

/*
 *  $Date: 2000/06/14 20:49:37 $
 *  $Author: hafner $
 *  $Revision: 5.1 $
 *  $State: Exp $
 */

#include "pm_config.h"
#include "config.h"
#include "mallocvar.h"

#include "types.h"
#include "macros.h"
#include "error.h"

#include "misc.h"
#include "rpf.h"

int const RPF_ZERO = -1;

/*****************************************************************************

                   private code
  
*****************************************************************************/


typedef struct {
    double fraction;
    int    exponent;
}  FracExp;



static FracExp
fracExpFromDouble(double const x) {

    FracExp retval;

    retval.fraction = frexp(x, &retval.exponent);

    return retval;
}



int
rtob (real_t        const f,
      const rpf_t * const rpfP)
/*
 *  Convert real number 'f' into fixed point format.
 *  The real number in [-'range'; +'range'] is scaled to [-1 ; +1].
 *  Sign and the first 'precision' - 1 bits of the mantissa are
 *  packed into one integer.  
 *
 *  Return value:
 *  real value in reduced precision format
 */
{  
    /*
     *  Extract mantissa (23 Bits), exponent (8 Bits) and sign (1 Bit)
     */

    double const normalized = f / rpfP->range;
        /* 'f' scaled to [-1,+1] */    
    FracExp const fracExp = fracExpFromDouble(normalized);
    unsigned int const signedMantissa =
        (unsigned int) (fracExp.fraction * (1<<23));

    unsigned int mantissa;
    unsigned int sign;  /* 0 for positive; 1 for negative */
    
    if (signedMantissa < 0) {
        mantissa = -signedMantissa;
        sign = 1;
    } else {
        mantissa = +signedMantissa;
        sign = 0;
    }

    /*
     *  Generate reduced precision mantissa.
     */
    if (fracExp.exponent > 0) 
        mantissa <<= fracExp.exponent;
    else
        mantissa >>= -fracExp.exponent;  
    
    mantissa >>= (23 - rpfP->mantissa_bits - 1);

    mantissa +=  1;          /* Round last bit. */
    mantissa >>= 1;
   
    if (mantissa == 0)           /* close to zero */
        return RPF_ZERO;
    else if (mantissa >= (1U << rpfP->mantissa_bits)) /* overflow */
        return sign;
    else
        return ((mantissa & ((1U << rpfP->mantissa_bits) - 1)) << 1) | sign;
}



float
btor (int           const binary,
      const rpf_t * const rpfP)
/*
 *  Convert value 'binary' in reduced precision format to a real value.
 *  For more information refer to function rtob() above.
 *
 *  Return value:
 *  converted value
 */
{
    unsigned int mantissa;
    float sign;
    float f;
 
    if (binary == RPF_ZERO)
        return 0;

    if (binary < 0 || binary >= 1 << (rpfP->mantissa_bits + 1))
        error ("Reduced precision format: value %d out of range.", binary);

    /*
     *  Restore IEEE float format:
     *  mantissa (23 Bits), exponent (8 Bits) and sign (1 Bit)
     */
   
    sign       = (binary & 0x1) == 0 ? 1.0 : -1.0;
    mantissa   = (binary & ((0x1 << (rpfP->mantissa_bits + 1)) - 1)) >> 1; 
    mantissa <<= (23 - rpfP->mantissa_bits);

    if (mantissa == 0) 
        f = sign;
    else
        f =  sign * (float) mantissa / 8388608;
   
    return f * rpfP->range;       /* expand [ -1 ; +1 ] to
                                     [ -range ; +range ] */
}




rpf_t *
alloc_rpf (unsigned           const mantissa,
           fiasco_rpf_range_e const range)
/*
 *  Reduced precision format constructor.
 *  Allocate memory for the rpf_t structure.
 *  Number of mantissa bits is given by `mantissa'.
 *  The range of the real values is in the interval [-`range', +`range'].
 *  In case of invalid parameters, a structure with default values is
 *  returned. 
 *
 *  Return value
 *  pointer to the new rpf structure
 */
{
    rpf_t * rpfP;

    MALLOCVAR(rpfP);
   
    if (mantissa < 2) {
        warning (_("Size of RPF mantissa has to be in the interval [2,8]. "
                   "Using minimum value 2.\n"));
        rpfP->mantissa_bits = 2;
    } else if (mantissa > 8) {
        warning (_("Size of RPF mantissa has to be in the interval [2,8]. "
                   "Using maximum value 8.\n"));
        rpfP->mantissa_bits = 2;
    } else
        rpfP->mantissa_bits = mantissa;

    switch (range) {
    case FIASCO_RPF_RANGE_0_75:
        rpfP->range   = 0.75;
        rpfP->range_e = range;
        break;
    case FIASCO_RPF_RANGE_1_50:
        rpfP->range   = 1.50;
        rpfP->range_e = range;
        break;
    case FIASCO_RPF_RANGE_2_00:
        rpfP->range   = 2.00;
        rpfP->range_e = range;
        break;
    case FIASCO_RPF_RANGE_1_00:
        rpfP->range   = 1.00;
        rpfP->range_e = range;
        break;
    default:
        warning (_("Invalid RPF range specified. Using default value 1.0."));
        rpfP->range   = 1.00;
        rpfP->range_e = FIASCO_RPF_RANGE_1_00;
        break;
    }
    return rpfP;
}

