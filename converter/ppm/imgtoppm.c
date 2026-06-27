/* imgtoppm.c - read an Img-whatnot file and produce a portable pixmap
**
** Based on a simple conversion program posted to comp.graphics by Ed Falk.
**
** Copyright (C) 1989 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdbool.h>
#include <limits.h>
#include <string.h>

#include "nstring.h"
#include "ppm.h"



static void
readUint8(FILE *         const ifP,
          unsigned int * const valueP,
          const char **  const errorP) {
/*----------------------------------------------------------------------------
   Read an unsigned integer from the current position of file *ifP.
   It is coded in ASCII decimal, 8 characters, right justified.
-----------------------------------------------------------------------------*/
    unsigned char buf[8+1];
    size_t itemsReadCt;

    itemsReadCt =  fread(buf, 8, 1, ifP);
    if (itemsReadCt != 1)
        pm_asprintf(errorP, "Failed to read 8-bytes from file");
    else {
        int intValue;

        buf[8] = '\0';  /* string terminator */

        intValue = atoi((char*) buf);

        *errorP = NULL;
        *valueP = intValue;
    }
}



static void
readVariableLengthField(FILE *           const ifP,
                        unsigned char ** const dataP,
                        unsigned int *   const lengthP,
                        const char **    const errorP) {
/*----------------------------------------------------------------------------
   Read from the current position of *ifP a variable length field, which is
   encoded as an 8-character decimal text length, followed by the number of
   bytes indicated by that length.

   We append an ASCII NUL, just in case the data is text, so that we return
   it as an ASCIIZ string.

   Return the data portion in newly malloced storage and return a pointer to
   it as *dataP.  Return the length of the data (not including the NUL) as
   *lengthP.
-----------------------------------------------------------------------------*/
    unsigned int len;
    const char * error;

    readUint8(ifP, &len, &error);
    if (error) {
        pm_asprintf(errorP, "Failed to read length field.  %s", error);
        pm_strfree(error);
    } else {
        unsigned char * dataBuf;  /* malloced */

        dataBuf = malloc(len+1);

        if (!dataBuf)
            pm_asprintf(errorP, "Unable to allocate %u byte read buffer", len);
        else {
            size_t itemsReadCt;

            itemsReadCt = fread(dataBuf, len, 1, ifP);

            if (itemsReadCt != 1)
                pm_asprintf(errorP, "failed to read %u bytes from image", len);
            else {
                *errorP = NULL;

                dataBuf[len] = '\0';

                *dataP   = dataBuf;
                *lengthP = len;
            }
            if (*errorP)
                free(dataBuf);
        }
    }
}



static void
doAtChunk(FILE *         const ifP,
          unsigned int * const colsP,
          unsigned int * const rowsP,
          unsigned int * const cmaplenP) {

    unsigned int len;
    unsigned int cols;
    unsigned int rows;
    unsigned int cmaplen;
    unsigned char * data;  /* malloced */
    const char * error;  /* malloced */

    readVariableLengthField(ifP, &data, &len, &error);

    if (error) {
        pm_error("Failed to read attributes chunk.  %s", error);
        pm_strfree(error);
    } else {
        sscanf((char*) data, "%4u%4u%4u", &cols, &rows, &cmaplen);

        if (cols > UINT_MAX/rows)
            pm_message("height (%u) and width (%u) in header are "
                       "uncomputably large", rows, cols);

        if (cmaplen > UINT_MAX/3)
            pm_message("colormap length (%u) in header is "
                       "uncomputably large", cmaplen);

        *colsP    = cols;
        *rowsP    = rows;
        *cmaplenP = cmaplen;

        free(data);
    }
}



static void
doCmChunk(FILE *         const ifP,
          unsigned int   const cmaplen,
          pixel *        const colormap,
          unsigned int * const adjustedCmaplenP) {

    unsigned int i;
    unsigned int len;
    unsigned int adjustedCmaplen;
        /* Colormap length adjusted to account for how much data is actually
           in the CM chunk.

           'cmaplen' is how many entries the header says are supposed to be in
           the colormap, but we recognize that the CM chunk may not have the
           number of characters that implies.  We probably should just abort
           because the image is corrupted in that case, but original didn't,
           and we don't mess with that.
        */
    unsigned char * data;  /* malloced */
    const char * error;  /* malloced */

    readVariableLengthField(ifP, &data, &len, &error);

    if (error) {
        pm_error("Failed to read colormap chunk.  %s", error);
        pm_strfree(error);
    } else {
        if (len != cmaplen * 3) {
            pm_message(
                "cmaplen (%u) and colormap buf length (%u) do not match",
                cmaplen, len);
            if (len < cmaplen * 3)
                adjustedCmaplen = len / 3;
            else
                adjustedCmaplen = cmaplen;
        } else
            adjustedCmaplen = cmaplen;

        for (i = 0; i < adjustedCmaplen; ++i) {
            PPM_ASSIGN(colormap[i],
                       data[3*i + 0], data[3*i + 1], data[3*i + 2]);
        }

        *adjustedCmaplenP = adjustedCmaplen;

        free(data);
    }
}



static void
doPdChunk(FILE *        const ifP,
          unsigned int  const cols,
          unsigned int  const rows,
          pixval        const maxval,
          bool          const haveColormap,
          const pixel * const colormap,
          unsigned int  const cmaplen,
          FILE *        const ofP) {

    unsigned int len;
    const char * error;

    readUint8(ifP, &len, &error);
    if (error) {
        pm_error("Failed to read pixel data length field.  %s", error);
        pm_strfree(error);
    } else {
        unsigned char * buf; /* malloced */
        pixel * pixelrow; /* malloced */
        unsigned int row;

        if (len != cols * rows)
            pm_message(
                "pixel data length (%u) does not match image size (%u)",
                len, cols * rows);

        buf = malloc(cols);

        if (!buf)
            pm_error("Failed to allocate a buffer to read %u columns", cols);

        ppm_writeppminit(ofP, cols, rows, maxval, 0);
        pixelrow = ppm_allocrow(cols);

        for (row = 0; row < rows; ++row) {
            unsigned int col;
            size_t itemsReadCt;

            itemsReadCt = fread(buf, 1, cols, ifP);
            if (itemsReadCt != cols)
                pm_error("EOF / read error");

            for (col = 0; col < cols; ++col) {
                if (haveColormap)
                    pixelrow[col] = colormap[buf[col]];
                else
                    PPM_ASSIGN(pixelrow[col],
                               buf[col], buf[col], buf[col]);
            }
            ppm_writeppmrow(ofP, pixelrow, cols, maxval, 0);
        }
        free(buf);
    }
}



int
main(int argc, const char ** argv) {

    pixval const maxval = 255;

    FILE * ifP;
    pixel colormap[256];
    unsigned int cols;
    unsigned int rows;
    unsigned int cmaplen;
        /* Length of colormap, according to image header */
    unsigned int adjustedCmaplen;
        /* Length of colormap, taking into account how much data is actually
           in the CM chunk (see 'doCmChunk' for details)
        */
    bool gotAt, gotCm, gotPd, eof;
    unsigned char buf[4096];

    pm_proginit(&argc, argv);

    if (argc-1 >= 1)
        ifP = pm_openr(argv[1]);
    else
        ifP = stdin;

    if (argc-1 > 1)
        pm_error("Too many arguments (%u).  "
                 "The only possible argument is the input file name", argc-1);

    /* Get signature. */
    fread(buf, 8, 1, ifP);
    buf[8] = '\0';

    for (gotAt = false, gotCm = false, gotPd = false, eof = false; !eof; ) {
        size_t itemsReadCt;

        itemsReadCt = fread(buf, 2, 1, ifP);

        if (itemsReadCt != 1)
            eof = true;
        else {
            if (strneq((char*) buf, "AT", 2)) {
                doAtChunk(ifP, &cols, &rows, &cmaplen);

                gotAt = true;
            } else if (strneq((char*) buf, "CM", 2)) {
                if (!gotAt)
                    pm_error("missing attributes header");

                doCmChunk(ifP, cmaplen, colormap, &adjustedCmaplen);

                gotCm = true;
            } else if (strneq((char*) buf, "PD", 2)) {
                doPdChunk(ifP, cols, rows, maxval, gotCm,
                          colormap, adjustedCmaplen, stdout);

                gotPd = true;
            }
        }
    }
    if (!gotPd)
        pm_error("missing pixel data header");

    pm_close(ifP);
    /* If the program failed, it previously aborted with nonzero completion
       code, via various function calls.
    */
    return 0;
}



