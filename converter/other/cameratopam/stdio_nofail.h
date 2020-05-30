#include <stdio.h>

size_t
fread_nofail(void * const ptr,
             size_t const size,
             size_t const nmemb,
             FILE * const streamP);

int
fgetc_nofail(FILE * const streamP);

int
fseek_nofail(FILE * const streamP,
             long   const offset,
             int    const whence);

long
ftell_nofail(FILE * const streamP);
