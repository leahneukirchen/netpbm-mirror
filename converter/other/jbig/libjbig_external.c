/*=============================================================================
                              libjbig_external
===============================================================================
  This adapts external libjbig to programs written for the Netpbm internal
  libjbig.  Link this with a program in addition to the external libjbig.
  Don't link this if you're linking the internal libjbig.
=============================================================================*/

#include <jbig.h>

#include "libjbig_external.h"

void
jbg_enc_set_order(struct jbg_enc_state * const s,
                  int                    const order) {

    jbg_enc_options(s, order, -1, -1, -1, -1);
}



void
jbg_enc_set_algorithm(struct jbg_enc_state * const sP,
                      int                    const algorithm) {

    jbg_enc_options(sP, -1, algorithm, -1, -1, -1);
}



void
jbg_enc_set_stripes(struct jbg_enc_state * const sP,
                    int                    const stripes) {

    jbg_enc_options(sP, -1, -1, stripes, -1, -1);
}



void
jbg_enc_set_maxoffset(struct jbg_enc_state * const sP,
                      int                    const maxoffset) {

    jbg_enc_options(sP, -1, -1, -1, maxoffset, -1);
}

const char *
jbg_strerrorext(int errnum) {
    return jbg_strerror(errnum);
}


