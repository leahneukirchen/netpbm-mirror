#ifndef G3_PREFAB_H_INCLUDED
#define G3_PREFAB_H_INCLUDED

struct PrefabCode {
    unsigned int leadBits;
    unsigned int trailBits;
    struct BitString activeBits;
};


extern struct PrefabCode const g3prefab_code[256];

#endif

