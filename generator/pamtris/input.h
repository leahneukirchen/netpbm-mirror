#ifndef INPUT_H_INCLUDED
#define INPUT_H_INCLUDED

#include <stdint.h>

struct boundary_info;
struct framebuffer_info;

typedef struct {
    char *   buffer;
    size_t   length;
    uint64_t number;
} Input;

void
input_init(Input * const inputP);

void
input_term(Input * const inputP);

void
input_process_next_command(Input *                   const inputP,
                           struct boundary_info *    const bdiP,
                           struct framebuffer_info * const fbiP,
                           bool *                    const noMoreCommandsP);

#endif
