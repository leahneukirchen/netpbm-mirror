#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <netpbm/pm.h>

#include "stdio_nofail.h"



size_t
fread_nofail(void * const ptr,
             size_t const size,
             size_t const nmemb,
             FILE * const streamP) {

    size_t rc;

    rc = fread(ptr, size, nmemb, streamP);

    if (rc < 0)
        pm_error("File read failed.  Errno=%d (%s)", errno, strerror(errno));

    return rc;
}



int
fgetc_nofail(FILE * streamP) {

    int rc;

    rc = fgetc(streamP);

    if (rc < 0)
        pm_error("File read failed.  Errno=%d (%s)", errno, strerror(errno));

    return rc;
}



int
fseek_nofail(FILE * const streamP,
             long   const offset,
             int    const whence) {

    int rc;

    rc = fseek(streamP, offset, whence);

    if (rc < 0)
        pm_error("File seek failed.  Errno=%d (%s)", errno, strerror(errno));

    return rc;
}



long
ftell_nofail(FILE * const streamP) {

    long rc;

    rc = ftell(streamP);

    if (rc < 0)
        pm_error("File position query failed.  Errno=%d (%s)",
                 errno, strerror(errno));

    return rc;
}



char *
fgets_nofail(char * const s,
             int    const size,
             FILE * const streamP) {

    char * rc;

    rc = fgets(s, size, streamP);

    if (ferror(streamP))
        pm_error("File read failed.  Errno=%d (%s)", errno, strerror(errno));

    return rc;
}



