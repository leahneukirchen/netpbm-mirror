/* pbmtozinc.c - read a PBM image and produce a bitmap file
**               in the format used by the Zinc Interface Library (v1.0)
**               November 1990.
**
** Author: James Darrell McCauley
**         Department of Agricultural Engineering
**         Texas A&M University
**         College Station, Texas 77843-2117 USA
**
** Copyright (C) 1988 by James Darrell McCauley (jdm5548@diamond.tamu.edu)
**                    and Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdio.h>
#include <string.h>

#include "mallocvar.h"
#include "nstring.h"
#include "pbm.h"

static void
parseCommandLine(int           const argc,
                 const char ** const argv,
                 const char ** const inputFileNameP) {

    if (argc-1 > 0) {
        *inputFileNameP = argv[1];

        if (argc-1 > 1)
            pm_error("To many arguments: %u.  "
                     "The only possible argument is the "
                     "name of the input file", argc-1);
    } else
        *inputFileNameP = "-";
}



static const char *
imageName(const char * const inputFileName) {
/*----------------------------------------------------------------------------
   The image name to put in the Zinc file, based on the input file name
   'inputFileName' ("-" to indicate Standard Input).

   Result is newly malloc'ed space that Caller must free.
-----------------------------------------------------------------------------*/
    const char * retval;

    if (streq(inputFileName, "-"))
        pm_asprintf(&retval, "noname");
    else {
        char * nameBuf;
        char * cp;

        MALLOCARRAY_NOFAIL(nameBuf, strlen(inputFileName) + 1);

        strcpy(nameBuf, inputFileName);

        cp = strchr(nameBuf, '.' );
        if (cp)
            *cp = '\0';

        retval = nameBuf;
    }
    return retval;
}



typedef struct {
    unsigned int itemsperline;
    uint16_t     item;
    unsigned int firstitem;
} Packer;



static void
packer_init(Packer * const packerP) {

    packerP->itemsperline = 0;
    packerP->firstitem = 1;
}



static void
packer_putitem(Packer * const packerP) {

    if (packerP->firstitem)
        packerP->firstitem = 0;
    else
        putchar(',');

    if (packerP->itemsperline == 11) {
        putchar('\n');
        packerP->itemsperline = 0;
    }
    if (packerP->itemsperline == 0)
        putchar(' ');

    ++packerP->itemsperline;
    printf ("0x%02x%02x", packerP->item & 255, packerP->item >> 8);

}



static void
writeRaster(FILE *       const ifP,
            unsigned int const rows,
            unsigned int const cols,
            int          const format) {

    bit * const bitrow = pbm_allocrow_packed(cols + 8);

    Packer packer;
    unsigned int row;

    packer_init(&packer);

    bitrow[pbm_packed_bytes(cols+8) -1 ] = 0x00;

    for (row = 0; row < rows; ++row) {
        uint16_t * const itemrow = (uint16_t *) bitrow;
        unsigned int const itemCt = (cols + 15 ) / 16;

        unsigned int i;

        pbm_readpbmrow_packed(ifP, bitrow, cols, format);

        pbm_cleanrowend_packed(bitrow, cols);

        for (i = 0; i < itemCt; ++i) {
            packer.item = itemrow[i];
            packer_putitem(&packer);
        }
    }
    pbm_freerow_packed(bitrow);
}



int
main(int argc, const char * argv[]) {

    const char * inputFileName;
    FILE * ifP;
    int rows, cols;
    int format;
    const char * name;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &inputFileName);

    ifP = pm_openr(inputFileName);

    name = imageName(inputFileName);

    pbm_readpbminit(ifP, &cols, &rows, &format);

    printf("USHORT %s[] = {\n", name);
    printf("  %d\n", cols);
    printf("  %d\n", rows);

    writeRaster(ifP, rows, cols, format);

    printf("};\n");

    pm_close(ifP);

    pm_strfree(name);

    return 0;
}
