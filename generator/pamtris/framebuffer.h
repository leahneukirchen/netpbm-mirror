#ifndef FRAMEBUFFER_H_INCLUDED
#define FRAMEBUFFER_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

#include "varying.h"
#include "netpbm/pam.h"

typedef struct framebuffer_info {
/*----------------------------------------------------------------------------
   Information about the frame buffer and PAM output
-----------------------------------------------------------------------------*/
    /* These fields are initialized once by reading the command line
       arguments. "maxval" and "num_attribs" may be modified later
       through "realloc_image_buffer"; "correct" may also be modified
       if the eponymous command is given.
    */
    int32_t width;
    int32_t height;
    int32_t maxval;
    int32_t num_attribs;

    /* The fields below must be initialized by "init_framebuffer" and
       freed by "free_framebuffer", except for the tuple_type field in
       "outpam" which is initialized once by reading the command line
       arguments and may be modified later through "set_tupletype".
    */
    struct {
        uint16_t * buffer;
        uint32_t   bytes;
    } img; /* Image buffer */

    struct {
        uint32_t * buffer;
        uint32_t   bytes;
    } z;  /* Z-buffer */

    struct pam outpam;

    tuple * pamrow;
} framebuffer_info;



int
set_tupletype(const char * const str,
              char *       const tupletype);

int
init_framebuffer(framebuffer_info * const fbi);

void
free_framebuffer(framebuffer_info * const fbi);

void
print_framebuffer(framebuffer_info * const fbi);

void
clear_framebuffer(bool               const clear_image_buffer,
                  bool               const clear_z_buffer,
                  framebuffer_info * const fbi);

int
realloc_image_buffer(int32_t            const new_maxval,
                     int32_t            const new_num_attribs,
                     framebuffer_info * const fbi);

void
draw_span(uint32_t           const base,
          uint16_t           const length,
          varying *          const attribs,
          framebuffer_info * const fbi);

#endif
