#include <stdlib.h>
#include <string.h>

#include "common.h"



int
set_tupletype(const char * str,
              char         tupletype[256]) {

    if (str == NULL) {
        memset(tupletype, 0, 256);

        return 1;
    } else {
        size_t len = strlen(str);

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

        return 1;
    }
}



int
init_framebuffer(framebuffer_info * fbi) {

    uint8_t num_planes = fbi->num_attribs + 1;

    uint32_t elements = fbi->width * fbi->height;

    fbi->img.bytes = elements * (num_planes * sizeof(uint16_t));
    fbi->z.bytes = elements * sizeof(uint32_t);

    fbi->img.buffer =
        calloc(fbi->img.bytes / sizeof(uint16_t), sizeof(uint16_t));
    fbi->z.buffer =
        calloc(fbi->z.bytes / sizeof(uint32_t), sizeof(uint32_t));

    if(fbi->img.buffer == NULL || fbi->z.buffer == NULL)
    {
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
free_framebuffer(framebuffer_info * fbi) {

    free(fbi->img.buffer);
    free(fbi->z.buffer);

    pnm_freepamrow(fbi->pamrow);
}



int
realloc_image_buffer(int32_t            new_maxval,
                     int32_t            new_num_attribs,
                     framebuffer_info * fbi) {

    uint8_t num_planes = fbi->num_attribs + 1;

    pnm_freepamrow(fbi->pamrow);
    fbi->pamrow = NULL;

    if (new_num_attribs != fbi->num_attribs) {
        fbi->num_attribs = new_num_attribs;
        num_planes = fbi->num_attribs + 1;

        fbi->img.bytes =
            fbi->width * fbi->height * (num_planes * sizeof(uint16_t));

        {
            uint16_t* new_ptr = realloc(fbi->img.buffer, fbi->img.bytes);

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
print_framebuffer(framebuffer_info * fbi) {

    uint8_t num_planes = fbi->num_attribs + 1;
    uint32_t i = 0;
    uint32_t end = fbi->width * fbi->height;

    pnm_writepaminit(&fbi->outpam);

    while (i != end) {
        int j;
        for (j = 0; j < fbi->width; j++) {
            uint32_t k = (i + j) * num_planes;

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
clear_framebuffer(bool              clear_image_buffer,
                  bool              clear_z_buffer,
                  framebuffer_info* fbi) {

    if (clear_image_buffer) {
        memset(fbi->img.buffer, 0, fbi->img.bytes);
    }

    if (clear_z_buffer) {
        memset(fbi->z.buffer, 0, fbi->z.bytes);
    }
}



void
draw_span(uint32_t           base,
          uint16_t           length,
          fract *            attribs_start,
          const fract *      attribs_steps,
          int32_t            div,
          framebuffer_info * fbi) {

    uint8_t num_planes = fbi->num_attribs + 1;

    unsigned int i;

    /* Process each pixel in the span: */

    for (i = 0; i < length; i++) {
        int32_t z = MAX_Z - attribs_start[fbi->num_attribs].q;
        uint32_t z_mask = -(~(z - fbi->z.buffer[base + i]) >> 31);
        uint32_t n_z_mask = ~z_mask;

        uint32_t j = base + i;
        uint32_t k = j * num_planes;

        unsigned int l;

        /* The following statements will only have any effect if the depth
           test, performed above, has suceeded. I. e. if the depth test fails,
           no changes will be made on the framebuffer; otherwise, the
           framebuffer will be updated with the new values.
        */
        fbi->z.buffer[j] = (fbi->z.buffer[j] & n_z_mask) | (z & z_mask);

        for (l = 0; l < fbi->num_attribs; l++) {
            fbi->img.buffer[k + l] =
                (fbi->img.buffer[k + l] & n_z_mask) |
                (attribs_start[l].q & z_mask);
        }

        fbi->img.buffer[k + fbi->num_attribs] =
            (fbi->img.buffer[k + fbi->num_attribs] & n_z_mask) |
            (fbi->maxval & z_mask);

        /* Compute the attribute values for the next pixel: */

        step_up(attribs_start, attribs_steps, num_planes, div);
    }
}


