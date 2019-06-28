/* G3 fax format declarations */

#ifndef G3_H_INCLUDED
#define G3_H_INCLUDED

/* G3 is nearly universal as the format for fax transmissions in the
   US.  Its full name is CCITT Group 3 (G3).  It is specified in
   Recommendations T.4 and T.30 and in EIA Standards EIA-465 and
   EIA-466.  It dates to 1993.

   G3 faxes are 204 dots per inch (dpi) horizontally and 98 dpi (196
   dpi optionally, in fine-detail mode) vertically.  Since G3 neither
   assumes error free transmission nor retransmits when errors occur,
   the encoding scheme used is differential only over small segments
   never exceeding 2 lines at standard resolution or 4 lines for
   fine-detail. (The incremental G3 encoding scheme is called
   two-dimensional and the number of lines so encoded is specified by
   a parameter called k.)

   G3 specifies much more than the format of the bit stream, which is
   the subject of this header file.  It also specifies layers
   underneath the bit stream.

   There is also the newer G4.
*/
#include "pm_config.h"  /* uint32_t */

struct BitString {
    /* A string of bits, up to as many fit in 32 bits. */
    uint32_t     intBuffer;
        /* The bits are in the 'bitCount' least significant bit positions
           of this number.  The rest of the bits of this number are always
           zero.
        */
    unsigned int bitCount;
        /* The length of the bit string */

    /* Example:  The bit string 010100 would be represented by
       bitCount = 6, intBuffer = 20
       (N.B. 20 = 00000000 00000000 00000000 00010100 in binary)
    */
};

#endif
