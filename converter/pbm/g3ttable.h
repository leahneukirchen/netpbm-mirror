#ifndef G3TTABLE_H_INCLUDED
#define G3TTABLE_H_INCLUDED

typedef struct G3TableEntry {
    unsigned short int code;
    unsigned short int length;
} G3TableEntry;

extern struct G3TableEntry g3ttable_table[];

#define g3ttable_mtable ((g3ttable_table)+64*2)

#endif
