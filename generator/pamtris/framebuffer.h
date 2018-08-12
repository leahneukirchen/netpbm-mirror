#ifndef FRAMEBUFFER_H_INCLUDED
#define FRAMEBUFFER_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include "fract.h"
#include "netpbm/pam.h"

typedef struct framebuffer_info {
/*----------------------------------------------------------------------------
   Information about the frame buffer and PAM output
-----------------------------------------------------------------------------*/
    /* These fields are initialized once by reading the command line
       arguments. "maxval" and "num_attribs" may be modified later
       through "realloc_image_buffer".
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
set_tupletype(const char * str,
              char         tupletype[256]);

int
init_framebuffer(framebuffer_info *);

void
free_framebuffer(framebuffer_info *);

void
print_framebuffer(framebuffer_info *);

void
clear_framebuffer(bool               clear_image_buffer,
                  bool               clear_z_buffer,
                  framebuffer_info *);

int
realloc_image_buffer(int32_t            new_maxval,
                     int32_t            new_num_attribs,
                     framebuffer_info *);

void
draw_span(uint32_t           base,
          uint16_t           length,
          fract *            attribs_start,
          const fract *      attribs_steps,
          int32_t            divisor,
          framebuffer_info *);

#endif
