#ifndef INPUT_H_INCLUDED
#define INPUT_H_INCLUDED

#include <stdint.h>

struct boundary_info;
struct framebuffer_info;

typedef struct input_info {
/*----------------------------------------------------------------------------
  Information necessary for the "process_next_command" function.  It must be
  initialized through "init_input_processor" and freed by
  "free_input_processor".
-----------------------------------------------------------------------------*/
    char *   buffer;
    size_t   length;
    uint64_t number;
} input_info;

void
init_input_processor(input_info * const ii);

void
free_input_processor(input_info * const ii);

int
process_next_command(input_info *              const ii,
                     struct boundary_info *    const bdi,
                     struct framebuffer_info * const fbi);

#endif
