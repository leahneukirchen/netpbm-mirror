#ifndef TRIANGLE_H_INCLUDED
#define TRIANGLE_H_INCLUDED

#include <stdint.h>

#include "limits_pamtris.h"

struct boundary_info;
struct framebuffer_info;

typedef struct {
    int32_t _[3][2];
} Xy;

typedef struct {
    int32_t _[3][MAX_NUM_ATTRIBS + 2];
} Attribs;

void
draw_triangle(Xy                        const xy,
              Attribs                   const attribs,
              struct boundary_info *    const bdi,
              struct framebuffer_info * const fbi);

#endif
