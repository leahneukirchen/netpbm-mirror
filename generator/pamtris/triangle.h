#ifndef TRIANGLE_H_INCLUDED
#define TRIANGLE_H_INCLUDED

#include <stdint.h>

#include "limits_pamtris.h"

struct boundary_info;
struct framebuffer_info;

void
draw_triangle(int32_t            xy[3][2],
              int32_t            attribs[3][MAX_NUM_ATTRIBS + 1],
              struct boundary_info *,
              struct framebuffer_info *);

#endif
