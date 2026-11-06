/* pm_Buffer is an object oriented class for a buffer for accumulating an
   arbitrariy array of bytes, such as a header or raster of an image.
*/

#ifndef BUFFER_INCLUDED
#define BUFFER_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif
#if 0
} /* to fake out automatic code indenters */
#endif

typedef struct pm_Buffer pm_Buffer;

pm_Buffer *
pm_buf_new(unsigned int const size);

void
pm_buf_destroy(pm_Buffer * const bufferP);

void
pm_buf_addByte(pm_Buffer *   const bufferP,
               unsigned char const c);

const unsigned char *
pm_buf_buffer(const pm_Buffer * const bufferP);

unsigned int
buf_length(const pm_Buffer * const bufferP);


#ifdef __cplusplus
}
#endif
#endif
