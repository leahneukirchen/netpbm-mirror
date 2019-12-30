/*=============================================================================
                              framebuffer.c
===============================================================================
  Frame buffer functions

  Every drawing operation is applied on an internal "frame buffer", which is
  simply an "image buffer" which represents the picture currently being drawn,
  along with a "Z-Buffer" which contains the depth values for every pixel in
  the image buffer. Once all desired drawing operations for a particular
  picture are effected, a function is provided to print the current contents
  of the image buffer as a PAM image on standard output.  Another function is
  provided to clear the contents of the frame buffer (i. e. set all image
  samples and Z-Buffer entries to 0), with the option of only clearing either
  the image buffer or the Z-Buffer individually.

  The Z-Buffer works as follows: Every pixel in the image buffer has a
  corresponding entry in the Z-Buffer. Initially, every entry in the Z-Buffer
  is set to 0. Every time we desire to plot a pixel at some particular
  position in the frame buffer, the current value of the corresponding entry
  in the Z-Buffer is compared against the the Z component of the incoming
  pixel. If MAX_Z minus the value of the Z component of the incoming pixel is
  equal to or greater than the current value of the corresponding entry in the
  Z-Buffer, the frame buffer is changed as follows:

    1. All the samples but the last of the corresponding position in the
       image buffer are set to equal those of the incoming pixel.

    2. The last sample, that is, the A-component of the corresponding position
       in the image buffer is set to equal the maxval.

    3. The corresponding entry in the Z-Buffer is set to equal MAX_Z minus the
       value of the Z component of the incoming pixel.

    Otherwise, no changes are made on the frame buffer.
=============================================================================*/
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "utils.h"
#include "varying.h"
#include "limits_pamtris.h"

#include "framebuffer.h"



int
set_tupletype(const char * const str,
              char *       const tupletype) {
/*----------------------------------------------------------------------------
  Set the tuple type for the output PAM images given a string ("str") of 255
  characters or less. If the string has more than 255 characters, the function
  returns 0. Otherwise, it returns 1. If NULL is given for the "str" argument,
  the tuple type is set to a null string. This function is called during
  program initialization and whenever a "r" command is executed. The second
  argument must point to the tuple_type member of the "outpam" field in the
  framebuffer_info struct.
-----------------------------------------------------------------------------*/
    if (str == NULL) {
        memset(tupletype, 0, 256);
    } else {
        size_t len;

        len = strlen(str);   /* initial value */

        if (len > 255) {
            return 0;
        }

        if (len > 0) {
            memcpy(tupletype, str, len);
        }

        tupletype[len--] = '\0';

        while(len > 0 && isspace(tupletype[len])) {
            tupletype[len--] = '\0';
        }
    }

    return 1;
}



int
init_framebuffer(framebuffer_info * const fbi) {

    uint8_t const num_planes = fbi->num_attribs + 1;

    uint32_t const elements = fbi->width * fbi->height;

    fbi->img.bytes = elements * (num_planes * sizeof(uint16_t));
    fbi->z.bytes = elements * sizeof(uint32_t);

    fbi->img.buffer =
        calloc(fbi->img.bytes / sizeof(uint16_t), sizeof(uint16_t));
    fbi->z.buffer =
        calloc(fbi->z.bytes / sizeof(uint32_t), sizeof(uint32_t));

    if(fbi->img.buffer == NULL || fbi->z.buffer == NULL) {
        free(fbi->img.buffer);
        free(fbi->z.buffer);

        return 0;
    }

    fbi->outpam.size = sizeof(struct pam);
    fbi->outpam.len = sizeof(struct pam);
    fbi->outpam.file = stdout;
    fbi->outpam.format = PAM_FORMAT;
    fbi->outpam.plainformat = 0;
    fbi->outpam.height = fbi->height;
    fbi->outpam.width = fbi->width;
    fbi->outpam.depth = num_planes;
    fbi->outpam.maxval = fbi->maxval;
    fbi->outpam.allocation_depth = 0;
    fbi->outpam.comment_p = NULL;

    fbi->pamrow = NULL;
    fbi->pamrow = pnm_allocpamrow(&fbi->outpam);

    if (fbi->pamrow == NULL) {
        free(fbi->img.buffer);
        free(fbi->z.buffer);

        return 0;
    }

    return 1;
}



void
free_framebuffer(framebuffer_info * const fbi) {

    free(fbi->img.buffer);
    free(fbi->z.buffer);

    pnm_freepamrow(fbi->pamrow);
}



int
realloc_image_buffer(int32_t            const new_maxval,
                     int32_t            const new_num_attribs,
                     framebuffer_info * const fbi) {
/*----------------------------------------------------------------------------
  Reallocate the image buffer with a new maxval and depth, given the struct
  with information about the framebuffer. The fields variables "maxval" and
  "num_attribs".

  From the point this function is called onwards, new PAM images printed on
  standard output will have the new maxval for the maxval and num_attribs + 1
  for the depth.

  This function does *not* check whether the new maxval and num_attribs are
  within the proper allowed limits. That is done inside the input processing
  function "process_next_command", which is the only function that calls this
  one.

  If the function succeeds, the image buffer is left in cleared state. The
  Z-Buffer, however, is not touched at all.

  If the new depth is equal to the previous one, no actual reallocation is
  performed: only the global variable "maxval" is changed. But the image
  buffer is nonetheless left in cleared state regardless.
-----------------------------------------------------------------------------*/
    uint8_t num_planes;

    pnm_freepamrow(fbi->pamrow);
    fbi->pamrow = NULL;

    num_planes = fbi->num_attribs + 1;  /* initial value */

    if (new_num_attribs != fbi->num_attribs) {
        fbi->num_attribs = new_num_attribs;
        num_planes = fbi->num_attribs + 1;

        fbi->img.bytes =
            fbi->width * fbi->height * (num_planes * sizeof(uint16_t));

        {
            uint16_t * const new_ptr =
                realloc(fbi->img.buffer, fbi->img.bytes);

            if (new_ptr == NULL) {
                free(fbi->img.buffer);
                fbi->img.buffer = NULL;

                return 0;
            }
            fbi->img.buffer = new_ptr;
        }
    }

    fbi->maxval = new_maxval;

    fbi->outpam.size             = sizeof(struct pam);
    fbi->outpam.len              = sizeof(struct pam);
    fbi->outpam.file             = stdout;
    fbi->outpam.format           = PAM_FORMAT;
    fbi->outpam.plainformat      = 0;
    fbi->outpam.height           = fbi->height;
    fbi->outpam.width            = fbi->width;
    fbi->outpam.depth            = num_planes;
    fbi->outpam.maxval           = fbi->maxval;
    fbi->outpam.allocation_depth = 0;
    fbi->outpam.comment_p        = NULL;

    fbi->pamrow = pnm_allocpamrow(&fbi->outpam);

    if (fbi->pamrow == NULL) {
        free(fbi->img.buffer);
        fbi->img.buffer = NULL;

        return 0;
    }

    memset(fbi->img.buffer, 0, fbi->img.bytes);

    return 1;
}



void
print_framebuffer(framebuffer_info * const fbi) {

    uint8_t  const num_planes = fbi->num_attribs + 1;
    uint32_t const end        = fbi->width * fbi->height;

    uint32_t i;

    pnm_writepaminit(&fbi->outpam);

    for (i = 0; i != end; ) {
        int j;
        for (j = 0; j < fbi->width; j++) {
            uint32_t const k = (i + j) * num_planes;

            unsigned int l;

            for (l = 0; l < num_planes; l++) {
                fbi->pamrow[j][l] = fbi->img.buffer[k + l];
            }
        }

        pnm_writepamrow(&fbi->outpam, fbi->pamrow);

        i += fbi->width;
    }
}



void
clear_framebuffer(bool               const clear_image_buffer,
                  bool               const clear_z_buffer,
                  framebuffer_info * const fbi) {

    if (clear_image_buffer) {
        memset(fbi->img.buffer, 0, fbi->img.bytes);
    }

    if (clear_z_buffer) {
        memset(fbi->z.buffer, 0, fbi->z.bytes);
    }
}



void
draw_span(uint32_t           const base,
          uint16_t           const length,
          varying *          const attribs,
          framebuffer_info * const fbi) {
/*----------------------------------------------------------------------------
  Draw a horizontal span of "length" pixels into the frame buffer, performing
  the appropriate depth tests. "base" must equal the row of the frame buffer
  where one desires to draw the span *times* the image width, plus the column
  of the first pixel in the span.

  This function does not perform any kind of bounds checking.
-----------------------------------------------------------------------------*/
    static double const depth_range = MAX_Z;

    uint16_t const maxval = fbi->maxval;
    uint8_t  const z      = fbi->num_attribs;
    uint8_t  const w      = z + 1;
    uint8_t  const n      = w + 1;

    uint8_t  const num_planes = w;

    unsigned int i;

    /* Process each pixel in the span: */

    for (i = 0; i < length; i++) {
        int32_t  const d      = round(depth_range * attribs[z].v);
        uint32_t const d_mask = geq_mask64(d, fbi->z.buffer[base + i]);

        uint32_t const j = base + i;
        uint32_t const k = j * num_planes;

        varying const inverse_w = inverse_varying(attribs[w]);

        unsigned int l;

        /* The following statements will only have any effect if the depth
           test, performed above, has succeeded. I. e. if the depth test fails,
           no changes will be made on the frame buffer; otherwise, the
           frame buffer will be updated with the new values.
        */
        fbi->z.buffer[j] = (fbi->z.buffer[j] & ~d_mask) | (d & d_mask);

        for (l = 0; l < z; l++) {
	    varying const newval = multiply_varyings(attribs[l], inverse_w);

            fbi->img.buffer[k + l] =
                (fbi->img.buffer[k + l] & ~d_mask) |
                (round_varying(newval) &  d_mask);
        }

        fbi->img.buffer[k + z] |= (maxval & d_mask);

        /* Compute the attribute values for the next pixel: */

        step_up(attribs, n);
    }
}



