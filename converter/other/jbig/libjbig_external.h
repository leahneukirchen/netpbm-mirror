#ifndef LIBJBIG_EXTERNAL_INCLUDED
#define LIBJBIG_EXTERNAL_INCLUDED

void
jbg_enc_set_order(struct jbg_enc_state * const s,
                  int                    const order);

void
jbg_enc_set_algorithm(struct jbg_enc_state * const s,
                      int                    const algorithm);

void
jbg_enc_set_stripes(struct jbg_enc_state * const s,
                    int                    const l0);

void
jbg_enc_set_maxoffset(struct jbg_enc_state * const s,
                      int                    const mx);

const char *
jbg_strerrorext(int errnum);

#endif
