/* These are some dynamic memory allocation facilities.  They are essentially
   an extension to C, as they do allocations with a cognizance of C
   variables.  You can use them to make C read more like a high level
   language.

   Before including this, you must define an __inline__ macro if your
   compiler doesn't recognize it as a keyword.
*/

#ifndef MALLOCVAR_INCLUDED
#define MALLOCVAR_INCLUDED

#include "pm_config.h"

#include <limits.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
#if 0
} /* to fake out automatic code indenters */
#endif

static __inline__ void
mallocProduct(void **      const resultP,
              size_t       const factor1,
              unsigned int const factor2) {
/*----------------------------------------------------------------------------
   malloc a space whose size in bytes is the product of 'factor1' and
   'factor2'.  But if the malloc fails, or that size is too large even to
   request from malloc, return NULL without allocating anything.

   If either factor is zero, malloc a single byte.
-----------------------------------------------------------------------------*/
    /* C99 introduces SIZE_MAX, the maximum size_t value.

       Pre-C99, we do the best we can, assuming conventional encoding of
       numbers and that size_t is unsigned.
    */
    size_t const sizeMax =
#if defined(SIZE_MAX)
        SIZE_MAX
#else
        ~((size_t)0)
#endif
        ;

    if (factor1 == 0 || factor2 == 0)
        *resultP = malloc(1);
    else {
        /* N.B. The type of malloc's argument is size_t */
        if ((size_t)factor2 != factor2)
            *resultP = NULL;
        else {
            if (sizeMax / factor2 < factor1)
                *resultP = NULL;
            else
                *resultP = malloc(factor1 * factor2);
        }
    }
}



static __inline__ void
reallocProduct(void **      const blockP,
               size_t       const factor1,
               unsigned int const factor2) {

    size_t const sizeMax =
#if defined(SIZE_MAX)
        SIZE_MAX
#else
        ~((size_t)0)
#endif
        ;

    void * const oldBlockP = *blockP;

    void * newBlockP;

    /* N.B. The type of realloc's argument is size_t */
    if ((size_t)factor2 != factor2)
        newBlockP = NULL;
    else {
        if (sizeMax / factor2 < factor1)
            newBlockP = NULL;
        else
            newBlockP = realloc(oldBlockP, factor1 * factor2);
    }
    if (newBlockP)
        *blockP = newBlockP;
    else {
        free(oldBlockP);
        *blockP = NULL;
    }
}



#define MALLOCARRAY(arrayName, nElements) do { \
    void * array; \
    mallocProduct(&array, nElements, sizeof(arrayName[0])); \
    arrayName = array; \
} while (0)

#define REALLOCARRAY(arrayName, nElements) do { \
    void * array; \
    array = arrayName; \
    reallocProduct(&array, nElements, sizeof(arrayName[0])); \
    if (!array && arrayName) \
        free(arrayName); \
    arrayName = array; \
} while (0)


#define MALLOCARRAY_NOFAIL(arrayName, nElements) \
do { \
    MALLOCARRAY(arrayName, nElements); \
    if ((arrayName) == NULL) \
        abort(); \
} while(0)

#define REALLOCARRAY_NOFAIL(arrayName, nElements) \
do { \
    REALLOCARRAY(arrayName, nElements); \
    if ((arrayName) == NULL) \
        abort(); \
} while(0)

#define MALLOCARRAY2(arrayName, nRows, nCols) do { \
    void * array; \
    pm_mallocarray2(&array, nRows, nCols, sizeof(arrayName[0][0]));  \
    arrayName = array; \
} while (0)

#define MALLOCARRAY2_NOFAIL(arrayName, nRows, nCols) do { \
    MALLOCARRAY2(arrayName, nRows, nCols);       \
    if ((arrayName) == NULL) \
        abort(); \
} while (0)

void
pm_freearray2(void ** const rowIndex);


#define MALLOCVAR(varName) \
    varName = malloc(sizeof(*varName))

#define MALLOCVAR_NOFAIL(varName) \
    do {if ((varName = malloc(sizeof(*varName))) == NULL) abort();} while(0)

void
pm_mallocarray2(void **      const resultP,
                unsigned int const cols,
                unsigned int const rows,
                unsigned int const elementSize);

#ifdef __cplusplus
}
#endif
#endif
