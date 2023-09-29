/* This is the intra-libnetpbm interface header file for libpbm*.c
*/

#ifndef LIBPBM_H_INCLUDED
#define LIBPBM_H_INCLUDED

#include <stdio.h>

void
pbm_readpbminitrest(FILE * file,
                    int  * colsP,
                    int *  rowsP);

void
pbm_validateComputableSize(unsigned int const cols,
                           unsigned int const rows);

#endif
