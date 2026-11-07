#include <assert.h>
#include <unistd.h>

#include "mallocvar.h"

#include "buffer.h"


typedef struct pm_Buffer {
    unsigned char * buffer;
    unsigned int    size;  /* allocated size */
    unsigned int    length;  /* occupied size */
} pm_Buffer;


pm_Buffer *
pm_buf_new(unsigned int const size) {

    pm_Buffer * retval;
    pm_Buffer * bufferP;

    MALLOCVAR_NOFAIL(bufferP);

    MALLOCARRAY(bufferP->buffer, size);

    if (bufferP->buffer) {
        bufferP->size = size;

        bufferP->length = 0;

        retval = bufferP;
    } else {
        free(bufferP);
        retval = NULL;
    }
    return retval;
}



void
pm_buf_destroy(pm_Buffer * const bufferP) {

    free(bufferP->buffer);
    free(bufferP);
}



void
pm_buf_addByte(pm_Buffer *   const bufferP,
               unsigned char const c) {


    assert(bufferP->length < bufferP->size);

    bufferP->buffer[bufferP->length++] = c;
}



const unsigned char *
pm_buf_buffer(const pm_Buffer * const bufferP) {

    return bufferP->buffer;
}



unsigned int
pm_buf_length(const pm_Buffer * const bufferP) {

    return bufferP->length;
}



